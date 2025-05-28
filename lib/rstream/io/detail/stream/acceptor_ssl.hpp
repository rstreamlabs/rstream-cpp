// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/system/error_code.hpp>

#include <rstream/io/acceptor_base.hpp>

#include "endpoint.hpp"
#include "ssl.hpp"
#include "stream.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class acceptor_ssl : public acceptor_base<endpoint, stream_socket> {
 public:
  acceptor_ssl(acceptor_ptr next_layer, const ssl::config& config);

  virtual ~acceptor_ssl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  void bind(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void listen(int backlog, boost::system::error_code& error_code) override;

  endpoint local_endpoint(boost::system::error_code& error_code) override;

  acceptor_ptr next_layer() const;

  acceptor_ptr next_layer();

 private:
  class impl;

  void async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
