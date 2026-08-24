// See LICENSE file in the project root for license information.

#include "child_stdin.hpp"

#ifndef _WIN32

#include <cerrno>
#include <utility>

#include <boost/asio/local/connect_pair.hpp>
#include <boost/system/system_error.hpp>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rstream {
namespace core {
namespace posix {

namespace {

void set_close_on_exec(int descriptor)
{
  const auto flags = ::fcntl(descriptor, F_GETFD);
  if (flags == -1 || ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == -1) {
    throw boost::system::system_error(errno, boost::system::system_category(), "fcntl(FD_CLOEXEC)");
  }
}

void disable_sigpipe(int descriptor)
{
#ifdef SO_NOSIGPIPE
  constexpr int enabled = 1;
  if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == -1) {
    throw boost::system::system_error(errno, boost::system::system_category(), "setsockopt(SO_NOSIGPIPE)");
  }
#else
  (void)descriptor;
#endif
}

}  // namespace

child_stdin::child_stdin(const boost::asio::any_io_executor& executor)
    : m_parent(executor),
      m_child(executor)
{
  boost::asio::local::connect_pair(m_parent, m_child);
  if (m_child.native_handle() == STDIN_FILENO) {
    std::swap(m_parent, m_child);
  }
  set_close_on_exec(m_parent.native_handle());
  set_close_on_exec(m_child.native_handle());
  disable_sigpipe(m_parent.native_handle());
}

child_stdin::~child_stdin()
{
  boost::system::error_code ignored;
  close(ignored);
}

child_stdin::stream_type& child_stdin::stream() { return m_parent; }

int child_stdin::child_native_handle() { return m_child.native_handle(); }

void child_stdin::close_child_end()
{
  boost::system::error_code ignored;
  m_child.close(ignored);
}

void child_stdin::close(boost::system::error_code& error_code)
{
  error_code.clear();
  boost::system::error_code parent_error;
  boost::system::error_code child_error;
  m_parent.close(parent_error);
  m_child.close(child_error);
  error_code = parent_error ? parent_error : child_error;
}

}  // namespace posix
}  // namespace core
}  // namespace rstream

#endif
