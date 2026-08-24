// See LICENSE file in the project root for license information.

#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rstream/ncat/client.hpp>
#include <rstream/ncat/error.hpp>
#include <rstream/ncat/server.hpp>
#include <rstream/test/time.hpp>

using tcp = boost::asio::ip::tcp;

static void require_posix(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static tcp::socket connect_with_retry(boost::asio::io_context& io_context, unsigned short port)
{
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + rstream::test::timeout(std::chrono::seconds(5));
  boost::system::error_code error_code;
  do {
    tcp::socket socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      return socket;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect to ncat server: " << error_code.message() << std::endl;
  assert(false);
  return tcp::socket(io_context);
}

static std::string read_until_eof(tcp::socket& socket)
{
  std::string out;
  std::array<char, 1024> buffer{};
  while (true) {
    boost::system::error_code error_code;
    auto bytes = socket.read_some(boost::asio::buffer(buffer), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code == boost::asio::error::eof) {
      break;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read");
    }
    out.append(buffer.data(), bytes);
  }
  return out;
}

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

class fd_capture {
 public:
  explicit fd_capture(int target)
      : m_target(target)
  {
    int fds[2] = {-1, -1};
    require_posix(pipe(fds) == 0, "pipe failed");
    m_read.reset(fds[0]);
    fd_guard write(fds[1]);
    m_saved.reset(dup(target));
    require_posix(m_saved.get() != -1, "dup failed");
    require_posix(dup2(write.get(), target) != -1, "dup2 failed");
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
    if (dup2(m_saved.get(), m_target) == -1) {
      std::abort();
    }
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

class stdin_data {
 public:
  explicit stdin_data(const std::string& data)
  {
    int fds[2] = {-1, -1};
    require_posix(pipe(fds) == 0, "pipe failed");
    m_read.reset(fds[0]);
    fd_guard write(fds[1]);
    std::size_t offset = 0;
    while (offset < data.size()) {
      auto n = ::write(write.get(), data.data() + offset, data.size() - offset);
      require_posix(n > 0, "write failed");
      offset += static_cast<std::size_t>(n);
    }
    m_saved.reset(dup(STDIN_FILENO));
    require_posix(m_saved.get() != -1, "dup failed");
    require_posix(dup2(m_read.get(), STDIN_FILENO) != -1, "dup2 failed");
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
    if (dup2(m_saved.get(), STDIN_FILENO) == -1) {
      std::abort();
    }
    m_saved.reset();
  }

 private:
  fd_guard m_saved;
  fd_guard m_read;
};

class ncat_server_fixture {
 public:
  explicit ncat_server_fixture(rstream::ncat::server::config config)
      : m_port(static_cast<unsigned short>(std::stoul(config.m_local.port()))),
        m_settings({
            .m_common                            = {},
            .m_read_downstream_buffer_size_bytes = 64 * 1024,
            .m_read_upstream_buffer_size_bytes   = 64 * 1024,
            .m_timeouts_ms                       = {
                .m_start = rstream::test::timeout_ms(5000),
                .m_open  = rstream::test::timeout_ms(5000),
            },
        }),
        m_server(std::make_shared<rstream::ncat::server>(m_io_context.get_executor(), config, m_settings))
  {
  }

  ~ncat_server_fixture()
  {
    stop();
  }

  unsigned short port() const
  {
    return m_port;
  }

  void start()
  {
    m_server->async_run([this](const boost::system::error_code& error_code) {
      m_result = error_code;
      m_done   = true;
    });
    m_thread = std::thread([this] {
      try {
        m_io_context.run();
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void stop()
  {
    if (!m_thread.joinable()) {
      return;
    }
    m_server->cancel();
    m_thread.join();
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
    assert(m_done);
    assert(!m_result);
  }

 private:
  unsigned short m_port;
  rstream::ncat::settings_server m_settings;
  boost::asio::io_context m_io_context;
  std::shared_ptr<rstream::ncat::server> m_server;
  std::thread m_thread;
  std::exception_ptr m_exception;
  bool m_done = false;
  boost::system::error_code m_result;
};

class echo_upstream {
 public:
  echo_upstream()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
  {
  }

  ~echo_upstream()
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
        std::array<char, 4> buffer{};
        boost::asio::read(socket, boost::asio::buffer(buffer));
        assert(std::string(buffer.data(), buffer.size()) == "ping");
        boost::asio::write(socket, boost::asio::buffer(std::string("pong")));
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

class interactive_echo_server {
 public:
  interactive_echo_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~interactive_echo_server()
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
        std::array<char, 12> input{};
        boost::asio::read(socket, boost::asio::buffer(input));
        assert(std::string(input.data(), input.size()) == "client-input");
        boost::asio::write(socket, boost::asio::buffer(std::string("server-output")));
        boost::system::error_code error_code;
        socket.shutdown(tcp::socket::shutdown_send, error_code);
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

class output_server {
 public:
  output_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~output_server()
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
        boost::asio::write(socket, boost::asio::buffer(std::string("server-output")));
        boost::system::error_code error_code;
        socket.shutdown(tcp::socket::shutdown_send, error_code);
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

class holding_server {
 public:
  holding_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~holding_server()
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
        std::array<char, 1> buffer{};
        boost::system::error_code error_code;
        while (socket.read_some(boost::asio::buffer(buffer), error_code) > 0) {
        }
        assert(error_code == boost::asio::error::eof || error_code == boost::asio::error::operation_aborted || error_code == boost::asio::error::connection_reset);
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

static void check_error_category()
{
  assert(rstream::ncat::to_string(rstream::ncat::error::code::success) == "success");
  assert(rstream::ncat::to_string(rstream::ncat::error::code::invalid_state) == "invalid state");
  assert(rstream::ncat::to_string(rstream::ncat::error::code::no_valid_endpoint) == "no valid endpoint");
  assert(rstream::ncat::to_string(rstream::ncat::error::code::operation_aborted) == "operation aborted");
  assert(rstream::ncat::to_string(rstream::ncat::error::code::operation_timeout) == "operation timeout");
  assert(rstream::ncat::to_string(static_cast<rstream::ncat::error::code>(9999)) == "unknown error");

  auto code = rstream::ncat::error::make_error_code(static_cast<int>(rstream::ncat::error::code::operation_timeout));
  assert(code);
  assert(code.message() == "operation timeout");
  assert(std::string(code.category().name()) == "rstream::ncat::error::category");
}

static void check_exec_server_pipes_downstream_to_child()
{
  const auto port                      = unused_tcp_port();
  rstream::ncat::server::config config = {
      .m_local  = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
      .m_remote = rstream::ncat::server::exec{.m_shell = false, .m_cmd = "/bin/cat"},
  };
  ncat_server_fixture server(config);
  server.start();

  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  boost::asio::write(socket, boost::asio::buffer(std::string("exec")));
  boost::system::error_code error_code;
  socket.shutdown(tcp::socket::shutdown_send, error_code);
  auto response = read_until_eof(socket);
  assert(response.find("exec") != std::string::npos);
}

static int run_exec_server_write_after_child_closed_stdin()
{
  std::signal(SIGPIPE, SIG_DFL);
  const auto port                      = unused_tcp_port();
  rstream::ncat::server::config config = {
      .m_local  = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
      .m_remote = rstream::ncat::server::exec{.m_shell = true, .m_cmd = "exec 0<&-; printf ready; sleep 30"},
  };
  ncat_server_fixture server(config);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  std::array<char, 5> ready{};
  boost::asio::read(socket, boost::asio::buffer(ready));
  assert(std::string(ready.data(), ready.size()) == "ready");
  boost::system::error_code error_code;
  boost::asio::write(socket, boost::asio::buffer(std::string("payload")), error_code);
  std::array<char, 1> response{};
  socket.read_some(boost::asio::buffer(response), error_code);
  assert(error_code);
  server.stop();
  return 0;
}

static void check_exec_server_write_after_child_closed_stdin_does_not_raise_sigpipe(const char* executable)
{
  const auto pid = ::fork();
  require_posix(pid != -1, "fork failed");
  if (pid == 0) {
    ::execl(executable, executable, "--exec-write-after-child-closed-stdin", static_cast<char*>(nullptr));
    ::_exit(127);
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) == -1) {
    require_posix(errno == EINTR, "waitpid failed");
  }
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) == 0);
}

static void check_proxy_server_pipes_downstream_to_upstream()
{
  echo_upstream upstream;
  upstream.start();

  const auto port                      = unused_tcp_port();
  rstream::ncat::server::config config = {
      .m_local  = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
      .m_remote = rstream::io::address(std::string("127.0.0.1:") + std::to_string(upstream.port())),
  };
  ncat_server_fixture server(config);
  server.start();

  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  boost::asio::write(socket, boost::asio::buffer(std::string("ping")));
  std::array<char, 4> buffer{};
  boost::asio::read(socket, boost::asio::buffer(buffer));
  assert(std::string(buffer.data(), buffer.size()) == "pong");
  boost::system::error_code error_code;
  socket.close(error_code);
  upstream.join();
}

static void check_client_pipes_stdin_to_socket_and_socket_to_stdout()
{
  interactive_echo_server server;
  server.start();

  fd_capture stdout_capture(STDOUT_FILENO);
  stdin_data input("client-input");

  boost::asio::io_context io_context;
  rstream::ncat::client::config config = {
      .m_address         = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_interactive     = true,
      .m_non_interactive = false,
  };
  rstream::ncat::settings_client settings = {
      .m_common                        = {},
      .m_read_socket_buffer_size_bytes = 64 * 1024,
      .m_read_std_in_buffer_size_bytes = 64 * 1024,
  };
  boost::system::error_code result;
  bool done      = false;
  bool timed_out = false;
  {
    rstream::ncat::client client(io_context.get_executor(), config, settings);
    input.restore();
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(rstream::test::timeout(std::chrono::seconds(10)));
    deadline.async_wait([&](const boost::system::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const boost::system::error_code& error_code) {
      result = error_code;
      done   = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
    assert(!result);
  }
  server.join();
  assert(stdout_capture.read_all() == "server-output");
}

static void check_non_interactive_client_accepts_graceful_peer_close()
{
  output_server server;
  server.start();

  fd_capture stdout_capture(STDOUT_FILENO);
  boost::asio::io_context io_context;
  rstream::ncat::client::config config = {
      .m_address         = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_interactive     = false,
      .m_non_interactive = true,
  };
  rstream::ncat::settings_client settings = {
      .m_common                        = {},
      .m_read_socket_buffer_size_bytes = 64 * 1024,
      .m_read_std_in_buffer_size_bytes = 64 * 1024,
  };
  boost::system::error_code result;
  bool done = false;
  rstream::ncat::client client(io_context.get_executor(), config, settings);
  client.async_run([&](const boost::system::error_code& error_code) {
    result = error_code;
    done   = true;
  });
  io_context.run();
  server.join();
  assert(done);
  assert(!result);
  assert(stdout_capture.read_all() == "server-output");
}

static void check_client_cancel_closes_pending_socket_read()
{
  holding_server server;
  server.start();

  boost::asio::io_context io_context;
  rstream::ncat::client::config config = {
      .m_address         = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_interactive     = false,
      .m_non_interactive = true,
  };
  rstream::ncat::settings_client settings = {
      .m_common                        = {},
      .m_read_socket_buffer_size_bytes = 64 * 1024,
      .m_read_std_in_buffer_size_bytes = 64 * 1024,
  };
  rstream::ncat::client client(io_context.get_executor(), config, settings);
  boost::system::error_code result;
  bool done      = false;
  bool timed_out = false;

  boost::asio::steady_timer cancel_timer(io_context);
  cancel_timer.expires_after(std::chrono::milliseconds(100));
  cancel_timer.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      client.cancel();
    }
  });

  boost::asio::steady_timer deadline(io_context);
  deadline.expires_after(rstream::test::timeout(std::chrono::seconds(5)));
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code && !done) {
      timed_out = true;
      client.cancel();
    }
  });

  client.async_run([&](const boost::system::error_code& error_code) {
    result = error_code;
    done   = true;
    deadline.cancel();
  });
  io_context.run();
  server.join();
  assert(done);
  assert(!timed_out);
  assert(result == rstream::ncat::error::make_error_code(rstream::ncat::error::code::operation_aborted));
}

static void check_exec_server_cancel_keeps_active_child_alive_until_completion()
{
  for (std::size_t iteration = 0; iteration < 16; ++iteration) {
    const auto port                      = unused_tcp_port();
    rstream::ncat::server::config config = {
        .m_local  = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
        .m_remote = rstream::ncat::server::exec{.m_shell = true, .m_cmd = "printf active; sleep 30"},
    };
    ncat_server_fixture server(config);
    server.start();
    boost::asio::io_context io_context;
    auto socket = connect_with_retry(io_context, server.port());
    std::array<char, 6> output{};
    boost::asio::read(socket, boost::asio::buffer(output));
    assert(std::string(output.data(), output.size()) == "active");
    server.stop();
    boost::system::error_code ignored;
    socket.close(ignored);
  }
}

int main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--exec-write-after-child-closed-stdin") {
    return run_exec_server_write_after_child_closed_stdin();
  }
  check_exec_server_write_after_child_closed_stdin_does_not_raise_sigpipe(argv[0]);
  check_error_category();
  check_exec_server_pipes_downstream_to_child();
  check_proxy_server_pipes_downstream_to_upstream();
  check_client_pipes_stdin_to_socket_and_socket_to_stdout();
  check_non_interactive_client_accepts_graceful_peer_close();
  check_client_cancel_closes_pending_socket_read();
  check_exec_server_cancel_keeps_active_child_alive_until_completion();
  return 0;
}
