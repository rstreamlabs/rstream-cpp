// See LICENSE file in the project root for license information.

#pragma once

#ifndef _WIN32

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/local/stream_protocol.hpp>

namespace rstream {
namespace core {
namespace posix {

// A socket pair keeps child standard-input writes on Asio's per-operation
// SIGPIPE-safe socket path without changing the process-wide signal policy.
class child_stdin {
 public:
  using stream_type = boost::asio::local::stream_protocol::socket;

  explicit child_stdin(const boost::asio::any_io_executor& executor);

  ~child_stdin();

  child_stdin(const child_stdin&)            = delete;
  child_stdin& operator=(const child_stdin&) = delete;

  child_stdin(child_stdin&&)            = delete;
  child_stdin& operator=(child_stdin&&) = delete;

  stream_type& stream();

  int child_native_handle();

  void close_child_end();

  void close(boost::system::error_code& error_code);

 private:
  stream_type m_parent;

  stream_type m_child;
};

}  // namespace posix
}  // namespace core
}  // namespace rstream

#endif
