// See LICENSE file in the project root for license information.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket.hpp>

#include <rstream/rtty/client.hpp>
#include <rstream/rtty/protobuf/messages.pb.h>
#include <rstream/rtty/rtty.hpp>

namespace http      = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace protobuf  = rstream::rtty::protobuf;
using tcp           = boost::asio::ip::tcp;

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

class stdin_data {
 public:
  explicit stdin_data(const std::string& data)
  {
    int fds[2] = {-1, -1};
    assert(pipe(fds) == 0);
    m_read.reset(fds[0]);
    fd_guard write_fd(fds[1]);
    std::size_t offset = 0;
    while (offset < data.size()) {
      auto n = write(write_fd.get(), data.data() + offset, data.size() - offset);
      assert(n > 0);
      offset += static_cast<std::size_t>(n);
    }
    m_saved.reset(dup(STDIN_FILENO));
    assert(m_saved.get() != -1);
    assert(dup2(m_read.get(), STDIN_FILENO) != -1);
  }

  ~stdin_data()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    assert(dup2(m_saved.get(), STDIN_FILENO) != -1);
    m_saved.reset();
  }

 private:
  fd_guard m_saved;
  fd_guard m_read;
};

class terminal_stdin {
 public:
  terminal_stdin()
      : m_master(posix_openpt(O_RDWR | O_NOCTTY))
  {
    assert(m_master.get() != -1);
    assert(grantpt(m_master.get()) == 0);
    assert(unlockpt(m_master.get()) == 0);
    const char* slave_name = ptsname(m_master.get());
    assert(slave_name != nullptr);
    m_slave.reset(open(slave_name, O_RDWR | O_NOCTTY));
    assert(m_slave.get() != -1);
    winsize size = {
        .ws_row    = 24,
        .ws_col    = 80,
        .ws_xpixel = 640,
        .ws_ypixel = 480,
    };
    assert(ioctl(m_slave.get(), TIOCSWINSZ, &size) == 0);
    m_saved.reset(dup(STDIN_FILENO));
    assert(m_saved.get() != -1);
    assert(dup2(m_slave.get(), STDIN_FILENO) != -1);
  }

  ~terminal_stdin()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    assert(dup2(m_saved.get(), STDIN_FILENO) != -1);
    m_saved.reset();
  }

 private:
  fd_guard m_master;
  fd_guard m_slave;
  fd_guard m_saved;
};

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static std::string serialize(const protobuf::Message& message)
{
  std::string payload;
  payload.resize(message.ByteSizeLong());
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
  return payload;
}

static protobuf::Message read_message(websocket::stream<tcp::socket>& ws)
{
  boost::beast::flat_buffer buffer;
  ws.read(buffer);
  auto payload = boost::beast::buffers_to_string(buffer.data());
  protobuf::Message message;
  assert(message.ParseFromArray(payload.data(), static_cast<int>(payload.size())));
  return message;
}

static void write_message(websocket::stream<tcp::socket>& ws, const protobuf::Message& message)
{
  ws.binary(true);
  auto payload = serialize(message);
  ws.write(boost::asio::buffer(payload));
}

class fake_websocket_server {
 public:
  fake_websocket_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_websocket_server()
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
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        assert(request.target() == "/session");

        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(open.open().config().options().interactive());
        assert(!open.open().config().options().allocate_tty());
        assert(open.open().config().options().send_heartbeat());

        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        protobuf::Message inbound_heartbeat;
        inbound_heartbeat.mutable_heartbeat();
        write_message(ws, inbound_heartbeat);

        bool saw_stdin     = false;
        bool saw_stdin_eos = false;
        bool saw_heartbeat = false;
        while (!saw_stdin || !saw_stdin_eos || !saw_heartbeat) {
          auto message = read_message(ws);
          if (message.payload_case() == protobuf::Message::PayloadCase::kData) {
            assert(message.data().type() == protobuf::Data::TYPE_STDIN);
            if (message.data().has_eos()) {
              saw_stdin_eos = true;
            }
            else if (message.data().data() == "client-input") {
              saw_stdin = true;
            }
          }
          else if (message.payload_case() == protobuf::Message::PayloadCase::kHeartbeat) {
            saw_heartbeat = true;
          }
          else {
            std::cerr << "unexpected websocket client message: " << message.payload_case() << std::endl;
            assert(false);
          }
        }

        protobuf::Message close;
        close.mutable_close()->set_return_code(21);
        write_message(ws, close);
        boost::system::error_code error_code;
        ws.close(websocket::close_code::normal, error_code);
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

class fake_terminal_websocket_server {
 public:
  fake_terminal_websocket_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_terminal_websocket_server()
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
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        assert(request.target() == "/tty");

        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(!open.open().config().options().interactive());
        assert(open.open().config().options().allocate_tty());
        assert(!open.open().config().options().send_heartbeat());

        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        auto parameter = read_message(ws);
        assert(parameter.payload_case() == protobuf::Message::PayloadCase::kParameter);
        assert(parameter.parameter().has_terminal_size());
        assert(parameter.parameter().terminal_size().row() == 24);
        assert(parameter.parameter().terminal_size().col() == 80);

        protobuf::Message close;
        close.mutable_close()->set_return_code(22);
        write_message(ws, close);
        boost::system::error_code error_code;
        ws.close(websocket::close_code::normal, error_code);
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

static void check_websocket_client_sends_open_stdin_eos_and_heartbeat()
{
  fake_websocket_server server;
  server.start();

  stdin_data input("client-input");
  boost::asio::io_context io_context;
  rstream::rtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/session"),
      .m_protocol_config  = {
           .m_protocol_type = rstream::rtty::protocol::type::websocket,
           .m_options       = {
                 .m_interactive    = true,
                 .m_allocate_tty   = false,
                 .m_send_heartbeat = true,
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
              .m_heartbeat = 10,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  bool timed_out  = false;
  {
    rstream::rtty::client client(io_context.get_executor(), config, settings);
    input.restore();
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(std::chrono::seconds(10));
    deadline.async_wait([&](const std::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const std::error_code& error_code, int code) {
      result      = error_code;
      return_code = code;
      done        = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
  }
  server.join();
  assert(!result);
  assert(return_code == 21);
}

static void check_websocket_client_sends_terminal_size_when_tty_allocated()
{
  fake_terminal_websocket_server server;
  server.start();

  terminal_stdin terminal;
  boost::asio::io_context io_context;
  rstream::rtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/tty"),
      .m_protocol_config  = {
           .m_protocol_type = rstream::rtty::protocol::type::websocket,
           .m_options       = {
                 .m_interactive    = false,
                 .m_allocate_tty   = true,
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

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  bool timed_out  = false;
  {
    rstream::rtty::client client(io_context.get_executor(), config, settings);
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(std::chrono::seconds(10));
    deadline.async_wait([&](const std::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const std::error_code& error_code, int code) {
      result      = error_code;
      return_code = code;
      done        = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
  }
  terminal.restore();
  server.join();
  assert(!result);
  assert(return_code == 22);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_websocket_client_sends_open_stdin_eos_and_heartbeat();
  check_websocket_client_sends_terminal_size_when_tty_allocated();
  return 0;
}
