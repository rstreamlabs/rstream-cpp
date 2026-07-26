// See LICENSE file in the project root for license information.

#include "terminal.hpp"

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include "error.hpp"

namespace rstream {
namespace webtty {

#ifdef _WIN32
terminal::terminal(HANDLE handle)
    : m_handle(handle)
#else
terminal::terminal(int fd)
    : m_fd(fd)
#endif
{
#ifdef _WIN32
  std::error_code error_code;
  m_initial_mode = get_console_mode(error_code);
  if (error_code) {
    throw std::system_error(error::code::not_a_tty);
  }
#else
  if (!isatty(m_fd)) {
    throw std::system_error(error::code::not_a_tty);
  }
#endif
#ifdef _WIN32
#else
  m_initial_config = get_termios();
#endif
}

void terminal::set_raw(std::error_code& error_code)
{
#ifdef _WIN32
  auto mode = get_console_mode(error_code);
  if (error_code) {
    return;
  }
  mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  set_console_mode(mode, error_code);
#else
  auto termios = get_termios(error_code);
  if (error_code) {
    return;
  }
  cfmakeraw(&termios);
  set_termios(termios, error_code);
#endif
}

void terminal::set_raw()
{
  std::error_code error_code;
  set_raw(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

void terminal::disable_echo(std::error_code& error_code)
{
#ifdef _WIN32
  auto mode = get_console_mode(error_code);
  if (error_code) {
    return;
  }
  mode &= ~ENABLE_ECHO_INPUT;
  set_console_mode(mode, error_code);
#else
  auto termios = get_termios(error_code);
  if (error_code) {
    return;
  }
  termios.c_lflag &= ~ECHO;
  set_termios(termios, error_code);
#endif
}

void terminal::disable_echo()
{
  std::error_code error_code;
  disable_echo(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

void terminal::reset(std::error_code& error_code)
{
#ifdef _WIN32
  set_console_mode(m_initial_mode, error_code);
#else
  set_termios(m_initial_config, error_code);
#endif
}

void terminal::reset()
{
  std::error_code error_code;
  reset(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

terminal::size terminal::get_size(std::error_code& error_code)
{
  size result = {};
#ifdef _WIN32
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi = {};
    if (!::GetConsoleScreenBufferInfo(m_handle, &csbi)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    else {
      result = size{
          .m_row    = static_cast<unsigned short>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1),
          .m_col    = static_cast<unsigned short>(csbi.srWindow.Right - csbi.srWindow.Left + 1),
          .m_xpixel = 0,
          .m_ypixel = 0,
      };
    }
  }
#else
  {
    struct winsize winsize = {0, 0, 0, 0};
    if (ioctl(m_fd, TIOCGWINSZ, &winsize)) {
      error_code = std::error_code(errno, std::system_category());
    }
    else {
      result = size{
          .m_row    = winsize.ws_row,
          .m_col    = winsize.ws_col,
          .m_xpixel = winsize.ws_xpixel,
          .m_ypixel = winsize.ws_ypixel,
      };
    }
  }
#endif
  return result;
}

terminal::size terminal::get_size()
{
  std::error_code error_code;
  auto result = get_size(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
  return result;
}

void terminal::resize(const size& size, std::error_code& error_code)
{
#ifdef _WIN32
  SMALL_RECT window;
  window.Left   = 0;
  window.Top    = 0;
  window.Right  = static_cast<SHORT>(size.m_col - 1);
  window.Bottom = static_cast<SHORT>(size.m_row - 1);
  if (!::SetConsoleWindowInfo(m_handle, TRUE, &window)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
#else
  struct winsize winsize = {
      .ws_row    = size.m_row,
      .ws_col    = size.m_col,
      .ws_xpixel = size.m_xpixel,
      .ws_ypixel = size.m_ypixel,
  };
  if (ioctl(m_fd, TIOCSWINSZ, &winsize)) {
    error_code = std::error_code(errno, std::system_category());
  }
#endif
}

void terminal::resize(const size& size)
{
  std::error_code error_code;
  resize(size, error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

#ifdef _WIN32

DWORD terminal::get_console_mode(std::error_code& error_code)
{
  DWORD result;
  if (!::GetConsoleMode(m_handle, &result)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
  return result;
}

DWORD terminal::get_console_mode()
{
  std::error_code error_code;
  auto result = get_console_mode(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
  return result;
}

void terminal::set_console_mode(DWORD mode, std::error_code& error_code)
{
  if (!::SetConsoleMode(m_handle, mode)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
}

void terminal::set_console_mode(DWORD mode)
{
  std::error_code error_code;
  set_console_mode(mode, error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

#else

struct termios terminal::get_termios(std::error_code& error_code)
{
  struct termios result;
  if (tcgetattr(m_fd, &result)) {
    error_code = std::error_code(errno, std::system_category());
  }
  return result;
}

struct termios terminal::get_termios()
{
  std::error_code error_code;
  auto result = get_termios(error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
  return result;
}

void terminal::set_termios(const struct termios& termios, std::error_code& error_code)
{
  if (tcsetattr(m_fd, TCSADRAIN, &termios)) {
    error_code = std::error_code(errno, std::system_category());
  }
}

void terminal::set_termios(const struct termios& termios)
{
  std::error_code error_code;
  set_termios(termios, error_code);
  if (error_code) {
    throw std::system_error(error_code);
  }
}

#endif

}  // namespace webtty
}  // namespace rstream
