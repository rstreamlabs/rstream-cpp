// See LICENSE file in the project root for license information.

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>
#endif
#include <docopt.h>
#include <unistd.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/io/address.hpp>

#ifdef RSTREAM_WITH_IO_STREAMS
using protocol = rstream::io::stream;
#else
using protocol = boost::asio::ip::tcp;
#endif

static const char USAGE[] = R"(
rstream-example-io-http-client

usage:
  rstream-example-io-http-client [options]
  rstream-example-io-http-client (-h|--help)
  rstream-example-io-http-client --version

options:
  -h --help            show this screen
  --version            show version
  --uri=ARG            URI [default: localhost:8080]
)";

const auto version = std::string("rstream-example-io-http-client ") + RSTREAM_VERSION;

boost::asio::awaitable<void> run(const rstream::io::address &address)
{
  // resolve the hostname
  auto executor = co_await boost::asio::this_coro::executor;
#ifdef RSTREAM_WITH_IO_STREAMS
  const auto endpoints = co_await protocol::resolver(executor).async_resolve(address.m_url, boost::asio::use_awaitable);
#else
  const auto endpoints = co_await protocol::resolver(executor).async_resolve(address.host(), address.port(), boost::asio::use_awaitable);
#endif
  // connect to the server
  protocol::socket socket(executor);
  auto endpoint = co_await boost::asio::async_connect(socket, endpoints, boost::asio::use_awaitable);
  // send the request
  boost::beast::http::request<boost::beast::http::empty_body> req;
  req.version(11);
  req.method(boost::beast::http::verb::get);
  req.target("/");
  {
    std::stringstream ss;
    ss << endpoint;
    req.set(boost::beast::http::field::host, ss.str());
  }
  req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
  std::cout << req << std::endl;
  co_await boost::beast::http::async_write(socket, req, boost::asio::use_awaitable);
  // read the response
  boost::beast::http::response<boost::beast::http::string_body> res;
  boost::beast::flat_buffer buffer;
  co_await boost::beast::http::async_read(socket, buffer, res, boost::asio::use_awaitable);
  // write the message to standard out
  std::cout << res << std::endl;
  // gracefully close the socket
  {
    boost::system::error_code tmp;
    socket.close(tmp);
  }
}

int main(int argc, char **argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  boost::asio::io_context io_context;
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  auto handler = [&io_context](const std::exception_ptr exception_ptr) {
    if (exception_ptr) {
      std::cerr << "an error occured: " << rstream::core::throwable::to_string(exception_ptr) << std::endl;
    }
    io_context.stop();
  };
  signal_set.async_wait(std::bind(handler, nullptr));
  boost::asio::co_spawn(io_context, run(args.at("--uri").asString()), handler);
  io_context.run();
}
