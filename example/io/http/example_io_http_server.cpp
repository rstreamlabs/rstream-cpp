// See LICENSE file in the project root for license information.

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/host_name.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/stream.hpp>
#endif
#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/io/address.hpp>

#ifdef RSTREAM_WITH_IO_STREAMS
using protocol = rstream::io::stream;
#else
using protocol = boost::asio::ip::tcp;
#endif

static const char USAGE[] = R"(
rstream-example-io-http-server

usage:
  rstream-example-io-http-server [options]
  rstream-example-io-http-server (-h|--help)
  rstream-example-io-http-server --version

options:
  -h --help            show this screen
  --version            show version
  --uri=ARG            URI [default: 127.0.0.1:8080]
)";

const auto version = std::string("rstream-example-io-http-server ") + RSTREAM_VERSION;

using active_sessions = std::list<std::shared_ptr<protocol::socket>>;

boost::asio::awaitable<void> session(protocol::socket& socket, const bool& stopping)
{
  try {
    std::cout << "new connection from " << socket.remote_endpoint() << std::endl;
    // read the request
    boost::beast::http::request<boost::beast::http::string_body> req;
    boost::beast::flat_buffer buffer;
    co_await boost::beast::http::async_read(socket, buffer, req, boost::asio::use_awaitable);
    // prepare the response
    boost::beast::http::response<boost::beast::http::string_body> res;
    res = {boost::beast::http::status::ok, req.version()};
    res.set(boost::beast::http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = boost::asio::ip::host_name();
    res.prepare_payload();
    // write the response back to the client
    co_await boost::beast::http::async_write(socket, res, boost::asio::use_awaitable);
  }
  catch (std::exception const &e) {
    if (!stopping) {
      std::cerr << "request failed: " << e.what() << std::endl;
    }
  }
  boost::system::error_code ignored;
  socket.close(ignored);
}

boost::asio::awaitable<void> listener(rstream::io::address address, std::optional<protocol::acceptor>& acceptor,
                                      active_sessions& sessions, const bool& stopping)
{
  auto executor = co_await boost::asio::this_coro::executor;
#ifdef RSTREAM_WITH_IO_STREAMS
  const auto endpoints = co_await protocol::resolver(executor).async_resolve(address.m_url, boost::asio::use_awaitable);
#else
  const auto endpoints = co_await protocol::resolver(executor).async_resolve(address.host(), address.port(), boost::asio::use_awaitable);
#endif
  if (stopping) {
    co_return;
  }
  if (endpoints.empty()) {
    throw std::runtime_error("no valid endpoints found");
  }
  acceptor.emplace(executor, endpoints.begin()->endpoint());
  std::cout << "server started on " << acceptor->local_endpoint() << std::endl;
  while (!stopping) {
    auto socket = co_await acceptor->async_accept(boost::asio::use_awaitable);
    if (stopping) {
      boost::system::error_code ignored;
      socket.close(ignored);
      co_return;
    }
    auto stream = std::make_shared<protocol::socket>(std::move(socket));
    auto active = sessions.insert(sessions.end(), stream);
    boost::asio::co_spawn(executor, session(*stream, stopping), [&sessions, active](std::exception_ptr) {
      sessions.erase(active);
    });
  }
}

int main(int argc, char **argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  boost::asio::io_context io_context;
  std::optional<protocol::acceptor> acceptor;
  active_sessions sessions;
  bool stopping = false;
  int result = 0;
  boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
  auto stop = [&] {
    stopping = true;
    boost::system::error_code ignored;
    if (acceptor) {
      acceptor->close(ignored);
    }
    for (const auto& socket : sessions) {
      socket->close(ignored);
    }
  };
  signals.async_wait([&](const boost::system::error_code& error, int) {
    if (!error) {
      stop();
    }
  });
  boost::asio::co_spawn(io_context, listener(args.at("--uri").asString(), acceptor, sessions, stopping),
                       [&](std::exception_ptr failure) {
    if (failure && !stopping) {
      result = 1;
      std::cerr << "server error: " << rstream::core::throwable::to_string(failure) << std::endl;
    }
    stop();
    boost::system::error_code ignored;
    signals.cancel(ignored);
  });
  io_context.run();
  std::cout << "server stopped" << std::endl;
  return result;
}
