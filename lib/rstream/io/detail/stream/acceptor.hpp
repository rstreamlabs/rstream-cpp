// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/system/error_code.hpp>

#include <rstream/io/acceptor_base.hpp>

#include "endpoint.hpp"
#include "stream.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class acceptor : public acceptor_base<endpoint, stream_socket> {
 public:
  using acceptor_base<endpoint, stream_socket>::open;
  using acceptor_base<endpoint, stream_socket>::close;
  using acceptor_base<endpoint, stream_socket>::bind;
  using acceptor_base<endpoint, stream_socket>::listen;
  using acceptor_base<endpoint, stream_socket>::local_endpoint;

  acceptor(const io_object::executor_type& executor);

  acceptor(const io_object::executor_type& executor, const endpoint& endpoint);

  virtual ~acceptor() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  void bind(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void listen(int backlog, boost::system::error_code& error_code) override;

  endpoint local_endpoint(boost::system::error_code& error_code) override;

 private:
  class impl;

  acceptor(acceptor_ptr native_handle);

  acceptor_const_ptr native_handle() const;

  acceptor_ptr native_handle();

  void swap(acceptor_ptr native_handle);

  void async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
