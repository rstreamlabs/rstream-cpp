// See LICENSE file in the project root for license information.

#include <cassert>
#include <stdexcept>
#include <system_error>

#include <boost/asio/io_context.hpp>

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

#ifndef _WIN32
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
  pty->set_window_size({.m_row = 40, .m_col = 120, .m_xpixel = 0, .m_ypixel = 0}, error_code);
  assert(!error_code);

  stream_ptr->close();
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
#ifndef _WIN32
  check_non_tty_file_descriptor_is_rejected();
  check_terminal_resize_and_reset_on_pty();
  check_pty_stream_lifecycle_and_window_size();
#endif
  return 0;
}
