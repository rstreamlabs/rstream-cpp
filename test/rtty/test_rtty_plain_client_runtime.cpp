// See LICENSE file in the project root for license information.

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <rstream/rtty/client.hpp>
#include <rstream/rtty/error.hpp>
#include <rstream/rtty/protobuf/messages.pb.h>
#include <rstream/rtty/rtty.hpp>

namespace protobuf = rstream::rtty::protobuf;
using tcp          = boost::asio::ip::tcp;

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

  int release()
  {
    auto fd = m_fd;
    m_fd    = -1;
    return fd;
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

class fd_capture {
 public:
  explicit fd_capture(int target)
      : m_target(target)
  {
    int fds[2] = {-1, -1};
    assert(pipe(fds) == 0);
    m_read.reset(fds[0]);
    fd_guard write(fds[1]);
    m_saved.reset(dup(target));
    assert(m_saved.get() != -1);
    assert(dup2(write.get(), target) != -1);
  }

  ~fd_capture()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    assert(dup2(m_saved.get(), m_target) != -1);
    m_saved.reset();
  }

  std::string read_all()
  {
    restore();
    std::string out;
    std::array<char, 1024> buffer{};
    while (true) {
      auto n = read(m_read.get(), buffer.data(), buffer.size());
      if (n == 0) {
        break;
      }
      assert(n > 0);
      out.append(buffer.data(), static_cast<std::size_t>(n));
    }
    return out;
  }

 private:
  int m_target;
  fd_guard m_saved;
  fd_guard m_read;
};

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static void read_exact(tcp::socket& socket, void* data, std::size_t size)
{
  std::size_t offset = 0;
  while (offset < size) {
    boost::system::error_code error_code;
    auto bytes = socket.read_some(boost::asio::buffer(static_cast<char*>(data) + offset, size - offset), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read");
    }
    offset += bytes;
  }
}

static protobuf::Message read_message(tcp::socket& socket)
{
  std::uint32_t frame_size = 0;
  read_exact(socket, &frame_size, sizeof(frame_size));
  const auto size = ntohl(frame_size);
  assert(size <= 1024 * 1024);
  std::vector<char> payload(size);
  if (!payload.empty()) {
    read_exact(socket, payload.data(), payload.size());
  }
  protobuf::Message message;
  assert(message.ParseFromArray(payload.data(), static_cast<int>(payload.size())));
  return message;
}

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  const auto size          = static_cast<std::uint32_t>(message.ByteSizeLong());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  std::vector<char> payload(size);
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
  if (!payload.empty()) {
    boost::asio::write(socket, boost::asio::buffer(payload));
  }
}

static void write_payload(tcp::socket& socket, const std::string& payload)
{
  auto size                = static_cast<std::uint32_t>(payload.size());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  boost::asio::write(socket, boost::asio::buffer(payload));
}

static protobuf::Message data_message(protobuf::Data::Type type, const std::string& data)
{
  protobuf::Message message;
  message.mutable_data()->set_type(type);
  message.mutable_data()->set_data(data);
  return message;
}

class fake_plain_server {
 public:
  fake_plain_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_plain_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(!open.open().config().options().interactive());
        assert(!open.open().config().options().allocate_tty());
        assert(!open.open().config().options().send_heartbeat());
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        write_message(socket, data_message(protobuf::Data::TYPE_STDOUT, "client-stdout"));
        write_message(socket, data_message(protobuf::Data::TYPE_STDERR, "client-stderr"));
        protobuf::Message close;
        close.mutable_close()->set_return_code(13);
        write_message(socket, close);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_error_server {
 public:
  fake_error_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_error_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message error;
        error.mutable_error()->set_msg("server refused");
        write_message(socket, error);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_invalid_payload_server {
 public:
  fake_invalid_payload_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_invalid_payload_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        write_payload(socket, "not-a-protobuf-message");
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_cancel_server {
 public:
  fake_cancel_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port())),
        m_ack_sent(m_ack_promise.get_future())
  {
  }

  ~fake_cancel_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        m_ack_promise.set_value();
        auto error = read_message(socket);
        assert(error.payload_case() == protobuf::Message::PayloadCase::kError);
        assert(!error.error().msg().empty());
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  bool wait_for_ack()
  {
    return m_ack_sent.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::promise<void> m_ack_promise;
  std::future<void> m_ack_sent;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

static rstream::rtty::client::config plain_client_config(unsigned short port)
{
  return {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
      .m_websocket_target = boost::none,
      .m_protocol_config  = {
           .m_protocol_type = rstream::rtty::protocol::type::plain,
           .m_options       = {
                 .m_interactive    = false,
                 .m_allocate_tty   = false,
                 .m_send_heartbeat = false,
           },
           .m_env_vars = {},
           .m_cmd_args = {"/bin/sh", "-c", "unused"},
           .m_workdir  = {},
           .m_username = {},
      },
  };
}

static rstream::rtty::settings_client plain_client_settings()
{
  return {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };
}

static void check_plain_client_processes_server_messages()
{
  fake_plain_server server;
  server.start();

  fd_capture stdout_capture(STDOUT_FILENO);
  fd_capture stderr_capture(STDERR_FILENO);

  boost::asio::io_context io_context;
  rstream::rtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = boost::none,
      .m_protocol_config  = {
           .m_protocol_type = rstream::rtty::protocol::type::plain,
           .m_options       = {
                 .m_interactive    = false,
                 .m_allocate_tty   = false,
                 .m_send_heartbeat = false,
           },
           .m_env_vars = {},
           .m_cmd_args = {"/bin/sh", "-c", "unused"},
           .m_workdir  = {},
           .m_username = {},
      },
  };
  rstream::rtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };
  rstream::rtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = -1;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(!result);
  assert(return_code == 13);
  assert(stdout_capture.read_all().find("client-stdout") != std::string::npos);
  assert(stderr_capture.read_all().find("client-stderr") != std::string::npos);
}

static void check_plain_client_reports_server_error_during_open()
{
  fake_error_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::rtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(result == rstream::rtty::error::make_error_code(rstream::rtty::error::code::server_error));
  assert(return_code == -1);
}

static void check_plain_client_rejects_invalid_payload_after_open()
{
  fake_invalid_payload_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::rtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(result == rstream::rtty::error::make_error_code(rstream::rtty::error::code::protocol_error));
  assert(return_code == -1);
}

static void check_plain_client_cancel_after_open_sends_error()
{
  fake_cancel_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::rtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  bool done       = false;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
    done        = true;
  });
  std::thread io_thread([&] {
    io_context.run();
  });
  assert(server.wait_for_ack());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.cancel();
  io_thread.join();
  server.join();

  assert(done);
  assert(result == rstream::rtty::error::make_error_code(rstream::rtty::error::code::operation_aborted));
  assert(return_code == -1);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_plain_client_processes_server_messages();
  check_plain_client_reports_server_error_during_open();
  check_plain_client_rejects_invalid_payload_after_open();
  check_plain_client_cancel_after_open_sends_error();
  return 0;
}
