// See LICENSE file in the project root for license information.

#pragma once

#ifdef _WIN32

#include <cstddef>
#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>

#include <rstream/core/completion_handler.hpp>

namespace rstream {
namespace core {
namespace windows {

class blocking_handle {
 public:
  using executor_type      = boost::asio::any_io_executor;
  using native_handle_type = void*;

  enum class access {
    read,
    write
  };

  using completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, std::size_t)>;

  explicit blocking_handle(const executor_type& executor);

  ~blocking_handle();

  blocking_handle(const blocking_handle&)            = delete;
  blocking_handle& operator=(const blocking_handle&) = delete;

  blocking_handle(blocking_handle&&)            = delete;
  blocking_handle& operator=(blocking_handle&&) = delete;

  void open(native_handle_type handle, access access, boost::system::error_code& error_code);

  bool is_open() const;

  void async_read_some(const boost::asio::mutable_buffer& buffer, completion_handler&& handler);

  void async_write(const boost::asio::const_buffer& buffer, completion_handler&& handler);

  void cancel();

  void close();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace windows
}  // namespace core
}  // namespace rstream

#endif
