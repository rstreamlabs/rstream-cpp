// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <rstream/core/allocator.hpp>
#include <rstream/io/stream_socket_base.hpp>

#include "endpoint.hpp"

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

class client;

class socket : public io::stream_socket_base<endpoint> {
  friend class client;

 public:
  socket(const executor_type& executor, core::allocator::ptr allocator = nullptr);

  socket(const executor_type& executor, const settings_socket& settings, core::allocator::ptr allocator = nullptr);

  socket(socket&& other) noexcept;

  socket& operator=(socket&& other) noexcept;

  socket(const socket& other) noexcept;

  socket& operator=(const socket& other) noexcept;

  virtual ~socket() = default;

  settings_socket settings(boost::system::error_code& error_code);

  settings_socket settings();

  void settings(const settings_socket& settings, boost::system::error_code& error_code);

  void settings(const settings_socket& settings);

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void open(const endpoint& endpoint);

  void close(boost::system::error_code& error_code) override;

  void close();

  endpoint remote_endpoint(boost::system::error_code& error_code) override;

  endpoint remote_endpoint();

 private:
  enum class type {
    client = 0,
    server = 1,
  };
  class impl;

  void async_connect_internal(type type, const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler) override;

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) override;

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) override;

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) override;

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) override;

  void async_shutdown_send_internal(async_shutdown_send_completion_handler&& handler) override;

  void adopt_impl(socket&& other) noexcept;

  std::shared_ptr<impl> ptr();

  core::allocator::ptr m_allocator;

  std::shared_ptr<impl> m_impl;
};

}  // namespace io_rstrm
}  // namespace rstream
