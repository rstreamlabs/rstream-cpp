// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/io/io_object.hpp>

#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class factory {
 public:
  using ptr = std::shared_ptr<factory>;

  using executor_type = io_object::executor_type;

  factory();

  virtual ~factory() = default;

  resolver_ptr resolver(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);
  stream_socket_ptr socket(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);
  acceptor_ptr acceptor(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

factory::ptr default_factory();

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
