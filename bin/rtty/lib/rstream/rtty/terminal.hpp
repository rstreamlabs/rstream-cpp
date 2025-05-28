// See LICENSE file in the project root for license information.

#pragma once

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include <system_error>

#include "rtty.hpp"

// clang-format off
// To be included after boost headers
#ifdef _WIN32
#include <windows.h>
#endif
// clang-format on

namespace rstream {
namespace rtty {

class terminal {
 public:
  using size = terminal_size;

#ifdef _WIN32
  terminal(HANDLE handle);
#else
  terminal(int fd);
#endif

  void set_raw(std::error_code& error_code);

  void set_raw();

  void disable_echo(std::error_code& error_code);

  void disable_echo();

  void reset(std::error_code& error_code);

  void reset();

  size get_size(std::error_code& error_code);

  size get_size();

  void resize(const size& size, std::error_code& error_code);

  void resize(const size& size);

 private:
#ifdef _WIN32

  DWORD get_console_mode(std::error_code& error_code);

  DWORD get_console_mode();

  void set_console_mode(DWORD mode, std::error_code& error_code);

  void set_console_mode(DWORD mode);

#else

  struct termios get_termios(std::error_code& error_code);

  struct termios get_termios();

  void set_termios(const struct termios& termios, std::error_code& error_code);

  void set_termios(const struct termios& termios);

#endif

#ifdef _WIN32

  HANDLE m_handle;

  DWORD m_initial_mode;

#else

  int m_fd;

  struct termios m_initial_config;

#endif
};

}  // namespace rtty
}  // namespace rstream
