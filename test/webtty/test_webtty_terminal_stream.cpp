// See LICENSE file in the project root for license information.

#define BOOST_PROCESS_VERSION 1

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
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
static int windows_pty_console_child()
{
  for (const auto channel : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    DWORD mode = 0;
    if (!::GetConsoleMode(::GetStdHandle(channel), &mode)) {
      return 10;
    }
  }
  DWORD count        = 0;
  const char ready[] = "CONPTY_READY\n";
  if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &count, nullptr)) {
    return 11;
  }
  char input[128] = {};
  if (!::ReadFile(::GetStdHandle(STD_INPUT_HANDLE), input, sizeof(input), &count, nullptr)
      || std::string(input, count) != "conpty-input\r\n") {
    return 12;
  }
  const char output[] = "CONPTY_STDIN_OK\n";
  const char error[]  = "CONPTY_STDERR_OK\n";
  if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), output, sizeof(output) - 1, &count, nullptr)
      || !::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), error, sizeof(error) - 1, &count, nullptr)) {
    return 13;
  }
  return 0;
}

static void check_windows_pty_console_io(const char* executable)
{
  boost::asio::io_context io_context;
  auto work       = boost::asio::make_work_guard(io_context);
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::tty);
  auto child      = rstream::webtty::detail::process::make_child(
      stream_ptr,
      boost::process::exe(executable),
      boost::process::args(std::vector<std::string>{"--conpty-console-child"}));
  char buffer[4096]  = {};
  const char input[] = "conpty-input\r";
  std::string output;
  std::promise<void> output_complete;
  auto output_ready    = output_complete.get_future();
  bool output_reported = false;
  bool input_sent      = false;
  bool input_written   = false;
  std::function<void()> read;
  read = [&] {
    stream::base::async_read_some_completion_handler handler =
        [&](const std::error_code& error_code, std::size_t count) {
          if (error_code || output.size() + count > 16384) {
            return;
          }
          output.append(buffer, count);
          if (!input_sent && output.find("CONPTY_READY") != std::string::npos) {
            input_sent = true;
            stream::base::async_write_completion_handler write_handler =
                [&](const std::error_code& write_error, std::size_t written) {
                  input_written = !write_error && written == sizeof(input) - 1;
                };
            stream_ptr->async_write(boost::asio::buffer(input, sizeof(input) - 1), stream::type::std_in, std::move(write_handler));
          }
          if (!output_reported && output.find("CONPTY_STDIN_OK") != std::string::npos
              && output.find("CONPTY_STDERR_OK") != std::string::npos) {
            output_reported = true;
            output_complete.set_value();
          }
          read();
        };
    stream_ptr->async_read_some(boost::asio::buffer(buffer), stream::type::std_out, std::move(handler));
  };
  read();
  std::thread runner([&] { io_context.run(); });
  const auto exited = ::WaitForSingleObject(child->native_handle(), 10000) == WAIT_OBJECT_0;
  boost::system::error_code ignored;
  if (!exited) {
    child->terminate(ignored);
  }
  child->wait(ignored);
  const auto drained = exited && child->exit_code() == 0
                       && output_ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  stream_ptr->close();
  work.reset();
  runner.join();
  assert(exited);
  assert(child->exit_code() == 0);
  assert(drained);
  assert(input_written);
  assert(output.find("CONPTY_STDIN_OK") != std::string::npos);
  assert(output.find("CONPTY_STDERR_OK") != std::string::npos);
}

static void check_windows_pty_with_redirected_parent(const char* executable)
{
  boost::process::child parent(
      boost::process::exe(executable),
      boost::process::args(std::vector<std::string>{"--conpty-redirected-parent"}),
      boost::process::std_in<boost::process::null,
                             boost::process::std_out>
          boost::process::null);
  const auto exited = ::WaitForSingleObject(parent.native_handle(), 20000) == WAIT_OBJECT_0;
  boost::system::error_code ignored;
  if (!exited) {
    parent.terminate(ignored);
  }
  parent.wait(ignored);
  assert(exited);
  assert(parent.exit_code() == 0);
}

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

  stream_ptr->close();
  auto child = rstream::webtty::detail::process::make_child(
      stream_ptr,
      boost::process::exe("/bin/sleep"),
      boost::process::args(std::vector<std::string>{"30"}));
  pty->set_window_size({.m_row = 50, .m_col = 150, .m_xpixel = 0, .m_ypixel = 0}, error_code);
  assert(!error_code);
  child->terminate();
  child->wait();
  stream_ptr->close();
  stream_ptr->close();
  pty->set_window_size({.m_row = 24, .m_col = 80, .m_xpixel = 0, .m_ypixel = 0}, error_code);
  assert(error_code);

  error_code.clear();
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
#ifdef _WIN32
  if (argc == 2 && std::strcmp(argv[1], "--conpty-console-child") == 0) {
    return windows_pty_console_child();
  }
  if (argc == 2 && std::strcmp(argv[1], "--conpty-redirected-parent") == 0) {
    DWORD mode = 0;
    assert(!::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode));
    assert(!::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode));
    check_windows_pty_console_io(argv[0]);
    return 0;
  }
  check_windows_pty_with_redirected_parent(argv[0]);
#else
  (void)argc;
  (void)argv;
#endif
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
