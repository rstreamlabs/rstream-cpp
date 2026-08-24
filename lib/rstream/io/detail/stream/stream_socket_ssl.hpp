// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/asio/ssl.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/stream_socket_base.hpp>

#include "endpoint.hpp"
#include "ssl.hpp"
#include "stream.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class stream_socket_ssl : public stream_socket_base<endpoint> {
 public:
  enum class type {
    client,
    server,
  };

  stream_socket_ssl(stream_socket_ptr next_layer, const ssl::config& config, type type);

  virtual ~stream_socket_ssl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  endpoint remote_endpoint(boost::system::error_code& error_code) override;

  bool is_secure() const override;

  stream_socket_const_ptr next_layer() const;

  stream_socket_ptr next_layer();

  using async_handshake_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  void async_handshake(async_handshake_completion_handler&& handler);

  using async_shutdown_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;
  void async_shutdown(async_shutdown_completion_handler&& handler);

  static std::shared_ptr<stream_socket_ssl> wrap(stream_socket& peer, const ssl::config& config, type type);

 private:
  class impl;

  void async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler) override;

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) override;

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) override;

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) override;

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) override;

  void async_shutdown_send_internal(async_shutdown_send_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
