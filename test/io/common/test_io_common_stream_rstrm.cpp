// See LICENSE file in the project root for license information.

#include <cassert>
#include <string>

#include <boost/asio/io_context.hpp>

#include <rstream/io/detail/stream/error.hpp>
#include <rstream/io/stream.hpp>

static rstream::io::stream::endpoint resolve_one(boost::asio::io_context& io_context, const std::string& uri)
{
  rstream::io::stream::resolver resolver(io_context.get_executor());
  rstream::io::stream::resolver::results_type results;
  boost::system::error_code error_code;
  bool completed = false;
  resolver.async_resolve(uri, [&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type& resolved) {
    error_code = error;
    results    = resolved;
    completed  = true;
  });
  io_context.run();
  io_context.restart();
  assert(completed);
  assert(!error_code);
  assert(results.size() == 1);
  return results.front().endpoint();
}

static void assert_stream_invalid_argument(const boost::system::error_code& error_code)
{
  assert(error_code.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(error_code.value() == static_cast<int>(rstream::io::detail::stream::error::code::invalid_argument));
}

static void check_rstrm_resolver_uses_generic_stream_plugin()
{
  boost::asio::io_context io_context;
  auto endpoint = resolve_one(io_context, "rstrm://viewer?server=tcp%3A%2F%2Fengine.example%3A443%3Fssl");
  assert(endpoint.protocol() == "rstrm");
  assert(endpoint.to_string().find("viewer") != std::string::npos);
}

static void check_rstrm_socket_rejects_invalid_token_parameter_before_network_io()
{
  boost::asio::io_context io_context;
  auto endpoint = resolve_one(io_context, "rstrm://viewer?server=tcp%3A%2F%2F127.0.0.1%3A9&rstream.token");

  rstream::io::stream::stream_socket socket(io_context.get_executor());
  bool completed = false;
  socket.async_connect(endpoint, [&](const boost::system::error_code& error_code) {
    assert_stream_invalid_argument(error_code);
    completed = true;
  });
  io_context.run();
  io_context.restart();
  assert(completed);
}

static void check_rstrm_acceptor_rejects_invalid_retry_parameter_before_network_io()
{
  boost::asio::io_context io_context;
  auto endpoint = resolve_one(io_context, "rstrm://publisher?server=tcp%3A%2F%2F127.0.0.1%3A9&rstream.retry=maybe");

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert_stream_invalid_argument(error_code);
}

static void check_socket_rejects_endpoint_from_another_plugin()
{
  boost::asio::io_context io_context;
  auto tcp_endpoint   = resolve_one(io_context, "tcp://127.0.0.1:9");
  auto rstrm_endpoint = resolve_one(io_context, "rstrm://viewer?server=tcp%3A%2F%2F127.0.0.1%3A9");

  rstream::io::stream::stream_socket socket(io_context.get_executor());
  boost::system::error_code error_code;
  socket.open(tcp_endpoint, error_code);
  assert(!error_code);

  socket.open(rstrm_endpoint, error_code);
  assert_stream_invalid_argument(error_code);

  bool completed = false;
  socket.async_connect(rstrm_endpoint, [&](const boost::system::error_code& error) {
    assert_stream_invalid_argument(error);
    completed = true;
  });
  assert(!completed);
  io_context.run();
  assert(completed);
}

static void check_acceptor_rejects_endpoint_from_another_plugin()
{
  boost::asio::io_context io_context;
  auto tcp_endpoint   = resolve_one(io_context, "tcp://127.0.0.1:0");
  auto rstrm_endpoint = resolve_one(io_context, "rstrm://publisher?server=tcp%3A%2F%2F127.0.0.1%3A9");

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(tcp_endpoint, error_code);
  assert(!error_code);

  acceptor.open(rstrm_endpoint, error_code);
  assert_stream_invalid_argument(error_code);

  error_code = {};
  acceptor.bind(rstrm_endpoint, error_code);
  assert_stream_invalid_argument(error_code);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_rstrm_resolver_uses_generic_stream_plugin();
  check_rstrm_socket_rejects_invalid_token_parameter_before_network_io();
  check_rstrm_acceptor_rejects_invalid_retry_parameter_before_network_io();
  check_socket_rejects_endpoint_from_another_plugin();
  check_acceptor_rejects_endpoint_from_another_plugin();
  return 0;
}
