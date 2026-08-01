// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/io/stream_socket_base.hpp>

#include "endpoint.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class stream_socket_ssl;
class acceptor_ssl;

class stream_socket : public stream_socket_base<endpoint> {
 public:
  using stream_socket_base<endpoint>::open;
  using stream_socket_base<endpoint>::close;
  using stream_socket_base<endpoint>::remote_endpoint;

  template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
  friend class acceptor_impl;
  friend class acceptor_ssl;
  friend class stream_socket_ssl;

  stream_socket(const executor_type& executor);

  stream_socket(const stream_socket&) = delete;

  stream_socket(stream_socket&& other);

  stream_socket& operator=(const stream_socket&) = delete;

  stream_socket& operator=(stream_socket&& other);

  virtual ~stream_socket() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  endpoint remote_endpoint(boost::system::error_code& error_code) override;

  bool is_secure() const override;

 private:
  class impl;

  stream_socket(stream_socket_ptr native_handle);

  stream_socket_const_ptr native_handle() const;

  stream_socket_ptr native_handle();

  void swap(stream_socket_ptr native_handle);

  void async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler) override;

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) override;

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) override;

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) override;

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
