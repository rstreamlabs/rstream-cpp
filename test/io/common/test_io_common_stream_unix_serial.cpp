// See LICENSE file in the project root for license information.

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <rstream/io/error.hpp>
#include <rstream/io/stream.hpp>

class fd_guard {
 public:
  explicit fd_guard(int fd = -1)
      : m_fd(fd)
  {
  }

  ~fd_guard()
  {
    reset();
  }

  int get() const
  {
    return m_fd;
  }

  void reset(int fd = -1)
  {
    if (m_fd != -1) {
      close(m_fd);
    }
    m_fd = fd;
  }

 private:
  int m_fd;
};

[[noreturn]] static void fail(const std::string& message)
{
  std::cerr << message << std::endl;
  std::abort();
}

static void check(bool condition, const std::string& message)
{
  if (!condition) {
    fail(message);
  }
}

static bool is_permission_error(const boost::system::error_code& error_code)
{
  return error_code.value() == EACCES || error_code.value() == EPERM;
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

static void check_unix_accept_connect_and_transfer()
{
  boost::asio::io_context io_context;
  auto socket_path = std::filesystem::temp_directory_path() / ("rstream-cpp-unix-" + std::to_string(getpid()) + ".sock");
  std::filesystem::remove(socket_path);

  const auto endpoint = resolve_one(io_context, "unix://" + socket_path.string());
  assert(endpoint.to_string().find("protocol: unix") != std::string::npos);

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

  client.close(error_code);
  server_peer.close(error_code);
  acceptor.close(error_code);
  std::filesystem::remove(socket_path);
}

static void check_serial_resolver_rejects_missing_baudrate()
{
  boost::asio::io_context io_context;
  rstream::io::stream::resolver resolver(io_context.get_executor());
  bool completed = false;
  resolver.async_resolve("serial:///dev/null", [&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type& results) {
    assert(error == rstream::io::error::make_error_code(rstream::io::error::code::invalid_uri));
    assert(results.empty());
    completed = true;
  });
  io_context.run();
  assert(completed);
}

static void check_serial_pty_connect_and_transfer()
{
  fd_guard master(posix_openpt(O_RDWR | O_NOCTTY));
  check(master.get() != -1, "posix_openpt failed");
  check(grantpt(master.get()) == 0, "grantpt failed");
  check(unlockpt(master.get()) == 0, "unlockpt failed");
  const char* slave_name = ptsname(master.get());
  check(slave_name != nullptr, "ptsname failed");

  boost::asio::io_context io_context;
  const auto endpoint = resolve_one(io_context, std::string("serial://") + slave_name + "?serial.baudrate=9600");
  assert(endpoint.to_string().find("protocol: serial") != std::string::npos);

  rstream::io::stream::stream_socket serial(io_context.get_executor());
  bool connected = false;
  boost::system::error_code connect_error;
  serial.async_connect(endpoint, [&](const boost::system::error_code& error) {
    connect_error = error;
    connected     = !error;
  });
  io_context.run();
  io_context.restart();
  if (connect_error && is_permission_error(connect_error)) {
    std::cerr << "serial pty runtime check skipped: " << connect_error.message() << std::endl;
    return;
  }
  check(!connect_error, std::string("serial connect failed: ") + connect_error.message());
  check(connected, "serial connect did not complete");

  bool sent = false;
  boost::asio::async_write(serial, boost::asio::buffer(std::string("ping")), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 4);
    sent = true;
  });
  io_context.run();
  io_context.restart();
  assert(sent);

  std::array<char, 4> master_buffer{};
  auto read_size = read(master.get(), master_buffer.data(), master_buffer.size());
  assert(read_size == static_cast<ssize_t>(master_buffer.size()));
  assert(std::string(master_buffer.data(), master_buffer.size()) == "ping");

  check(write(master.get(), "pong", 4) == 4, "serial master write failed");
  std::array<char, 4> serial_buffer{};
  bool received = false;
  boost::asio::async_read(serial, boost::asio::buffer(serial_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == serial_buffer.size());
    assert(std::string(serial_buffer.data(), serial_buffer.size()) == "pong");
    received = true;
  });
  io_context.run();
  assert(received);

  boost::system::error_code error_code;
  (void)serial.remote_endpoint(error_code);
  assert(error_code == rstream::io::error::make_error_code(rstream::io::error::code::unsupported_operation));
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_unix_accept_connect_and_transfer();
  check_serial_resolver_rejects_missing_baudrate();
  check_serial_pty_connect_and_transfer();
  return 0;
}
