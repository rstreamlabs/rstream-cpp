// See LICENSE file in the project root for license information.

#define BOOST_PROCESS_VERSION 1

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#if __has_include(<boost/process/v1/args.hpp>)
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/exe.hpp>
#else
#include <boost/process/args.hpp>
#include <boost/process/exe.hpp>
#endif

#include <rstream/core/system.hpp>
#include <rstream/webtty/detail/process.hpp>
#include <rstream/webtty/error.hpp>
#include <rstream/webtty/stream.hpp>
#include <rstream/webtty/terminal.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace stream = rstream::webtty::stream;

#ifdef _WIN32
static void check_windows_pty_rejects_overlapping_writes()
{
  boost::asio::io_context io_context;
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::tty);
  auto pty        = std::dynamic_pointer_cast<stream::pty_windows>(stream_ptr);
  assert(pty);

  const auto command_shell = rstream::core::get_environment_variable("COMSPEC");
  assert(command_shell.has_value());
  auto child = rstream::webtty::detail::process::make_child(
      stream_ptr,
      boost::process::exe(*command_shell),
      boost::process::args(std::vector<std::string>{"/d", "/s", "/c", "ping -n 30 127.0.0.1 >nul"}));

  std::vector<char> first_payload(16 * 1024 * 1024, 'x');
  const char second_payload[] = "second";
  std::error_code first_error;
  std::error_code second_error;
  std::size_t first_completions  = 0;
  std::size_t second_completions = 0;
  stream::base::async_write_completion_handler first_handler =
      [&](const std::error_code& error_code, std::size_t) {
        first_error = error_code;
        ++first_completions;
      };
  stream::base::async_write_completion_handler second_handler =
      [&](const std::error_code& error_code, std::size_t) {
        second_error = error_code;
        ++second_completions;
      };

  pty->async_write_some(boost::asio::buffer(first_payload), std::move(first_handler));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  pty->async_write_some(boost::asio::buffer(second_payload), std::move(second_handler));
  stream_ptr->close();

  boost::system::error_code ignored;
  child->terminate(ignored);
  child->wait(ignored);
  io_context.run();

  assert(first_completions == 1);
  assert(second_completions == 1);
  assert(first_error == std::error_code(ERROR_OPERATION_ABORTED, std::system_category()));
  assert(second_error == rstream::webtty::error::code::invalid_state);
}

static void check_windows_pty_cancel_resize_and_close_are_serialized()
{
  boost::asio::io_context io_context;
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::tty);
  auto pty        = std::dynamic_pointer_cast<stream::pty_windows>(stream_ptr);
  assert(pty);

  const auto command_shell = rstream::core::get_environment_variable("COMSPEC");
  assert(command_shell.has_value());
  auto child = rstream::webtty::detail::process::make_child(
      stream_ptr,
      boost::process::exe(*command_shell),
      boost::process::args(std::vector<std::string>{"/d", "/s", "/c", "ping -n 30 127.0.0.1 >nul"}));

  std::thread resize_thread([pty] {
    for (std::size_t iteration = 0; iteration < 100; ++iteration) {
      std::error_code error_code;
      pty->set_window_size({.m_row = 25, .m_col = 81, .m_xpixel = 0, .m_ypixel = 0}, error_code);
      if (error_code) {
        assert(error_code == std::error_code(ERROR_INVALID_HANDLE, std::system_category()));
        return;
      }
    }
  });
  std::thread cancel_thread([pty] { pty->cancel(); });
  resize_thread.join();
  cancel_thread.join();
  stream_ptr->close();
  stream_ptr->close();

  boost::system::error_code ignored;
  child->terminate(ignored);
  child->wait(ignored);
}
#else
static void require_posix(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class fd_guard {
 public:
  fd_guard() = default;
  explicit fd_guard(int fd)
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
  int m_fd = -1;
};

static void check_non_tty_file_descriptor_is_rejected()
{
  int pipe_fds[2] = {-1, -1};
  require_posix(pipe(pipe_fds) == 0, "pipe failed");
  fd_guard read_end(pipe_fds[0]);
  fd_guard write_end(pipe_fds[1]);

  bool rejected = false;
  try {
    rstream::webtty::terminal terminal(read_end.get());
    (void)terminal;
  }
  catch (const std::system_error& error) {
    rejected = true;
    assert(error.code() == rstream::webtty::error::code::not_a_tty);
  }
  assert(rejected);
}

static void check_terminal_resize_and_reset_on_pty()
{
  int master_fd = -1;
  int slave_fd  = -1;
  require_posix(openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) == 0, "openpty failed");
  fd_guard master(master_fd);
  fd_guard slave(slave_fd);

  rstream::webtty::terminal terminal(slave.get());
  rstream::webtty::terminal::size size = {
      .m_row    = 31,
      .m_col    = 111,
      .m_xpixel = 0,
      .m_ypixel = 0,
  };
  terminal.resize(size);
  auto actual = terminal.get_size();
  assert(actual.m_row == size.m_row);
  assert(actual.m_col == size.m_col);
  terminal.set_raw();
  terminal.disable_echo();
  terminal.reset();
}

static void check_pty_stream_lifecycle_and_window_size()
{
  boost::asio::io_context io_context;
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::tty);
  assert(stream_ptr);
  assert(stream_ptr->backend() == stream::backend::tty);

  auto pty = std::dynamic_pointer_cast<stream::pty>(stream_ptr);
  assert(pty);

  std::error_code error_code;
  pty->set_window_size({.m_row = 24, .m_col = 80, .m_xpixel = 0, .m_ypixel = 0}, error_code);
  assert(error_code);

  error_code.clear();
  pty->allocate(error_code);
  assert(!error_code);
  pty->allocate(error_code);
  assert(error_code == rstream::webtty::error::code::invalid_state);
  error_code.clear();
  pty->set_window_size({.m_row = 40, .m_col = 120, .m_xpixel = 0, .m_ypixel = 0}, error_code);
  assert(!error_code);

  auto pty_posix = std::dynamic_pointer_cast<stream::pty_posix>(stream_ptr);
  assert(pty_posix);
  pty_posix->on_success(error_code);
  assert(!error_code);
  stream_ptr->close();
  stream_ptr->close();

  pty->allocate(error_code);
  assert(!error_code);
  stream_ptr->close();
}
#endif

static void check_pipe_stream_lifecycle()
{
  boost::asio::io_context io_context;
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::pipe);
  assert(stream_ptr);
  assert(stream_ptr->backend() == stream::backend::pipe);
  assert(std::dynamic_pointer_cast<stream::pipe>(stream_ptr));
  stream_ptr->close();
  stream_ptr->close();
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_pipe_stream_lifecycle();
#ifdef _WIN32
  check_windows_pty_rejects_overlapping_writes();
  check_windows_pty_cancel_resize_and_close_are_serialized();
#else
  check_non_tty_file_descriptor_is_rejected();
  check_terminal_resize_and_reset_on_pty();
  check_pty_stream_lifecycle_and_window_size();
#endif
  return 0;
}
