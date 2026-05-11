// See LICENSE file in the project root for license information.

#include <array>
#include <cassert>
#include <string>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <rstream/io/detail/stream/error.hpp>
#include <rstream/io/stream.hpp>

using tcp = boost::asio::ip::tcp;

static void assert_stream_error(const boost::system::error_code& actual, rstream::io::detail::stream::error::code expected)
{
  assert(actual.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(actual.value() == static_cast<int>(expected));
}

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

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
  assert(!results.empty());
  return results.front().endpoint();
}

static void check_uninitialized_socket_operations_fail()
{
  boost::asio::io_context io_context;
  rstream::io::stream::stream_socket socket(io_context.get_executor());

  boost::system::error_code error_code;
  (void)socket.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
  assert(!socket.is_secure());

  std::array<char, 4> read_buffer{};
  const std::string write_buffer = "ping";
  bool saw_read                  = false;
  bool saw_write                 = false;
  socket.async_read_some(boost::asio::buffer(read_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    assert(size == 0);
    saw_read = true;
  });
  socket.async_write_some(boost::asio::buffer(write_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    assert(size == 0);
    saw_write = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_read);
  assert(saw_write);

  std::array<boost::asio::const_buffer, 0> empty_write{};
  std::array<boost::asio::mutable_buffer, 0> empty_read{};
  bool saw_empty_write = false;
  bool saw_empty_read  = false;
  socket.async_write_some(empty_write, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    saw_empty_write = true;
  });
  socket.async_read_some(empty_read, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    saw_empty_read = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_empty_write);
  assert(saw_empty_read);
}

static void check_uninitialized_acceptor_operations_fail()
{
  boost::asio::io_context io_context;
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.bind(endpoint, error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  error_code = {};
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  error_code = {};
  (void)acceptor.local_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  rstream::io::stream::stream_socket peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  bool saw_accept = false;
  acceptor.async_accept(peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    saw_accept = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_accept);
}

static void check_tcp_accept_connect_and_transfer()
{
  boost::asio::io_context io_context;
  const auto port     = unused_tcp_port();
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port) + "?tcp.no_delay=true&tcp.keep_alive=true");

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);

  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  io_context.run();
  io_context.restart();
  assert(accepted);
  assert(connected);
  assert(!client.is_secure());
  assert(!server_peer.is_secure());

  std::array<char, 4> server_buffer{};
  bool server_read = false;
  bool client_sent = false;
  boost::asio::async_read(server_peer, boost::asio::buffer(server_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == server_buffer.size());
    assert(std::string(server_buffer.data(), server_buffer.size()) == "ping");
    server_read = true;
  });
  boost::asio::async_write(client, boost::asio::buffer(std::string("ping")), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 4);
    client_sent = true;
  });
  io_context.run();
  io_context.restart();
  assert(server_read);
  assert(client_sent);

  std::array<char, 4> client_buffer{};
  bool client_read = false;
  bool server_sent = false;
  boost::asio::async_read(client, boost::asio::buffer(client_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == client_buffer.size());
    assert(std::string(client_buffer.data(), client_buffer.size()) == "pong");
    client_read = true;
  });
  boost::asio::async_write(server_peer, boost::asio::buffer(std::string("pong")), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 4);
    server_sent = true;
  });
  io_context.run();
  io_context.restart();
  assert(client_read);
  assert(server_sent);

  error_code = {};
  client.close(error_code);
  assert(!error_code);
  error_code = {};
  (void)client.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_uninitialized_socket_operations_fail();
  check_uninitialized_acceptor_operations_fail();
  check_tcp_accept_connect_and_transfer();
  return 0;
}
