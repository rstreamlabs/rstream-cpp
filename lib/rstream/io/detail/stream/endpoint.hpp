// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <boost/system/error_code.hpp>

#include "endpoint_base.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class endpoint : public endpoint_base {
 public:
  template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
  friend class acceptor_impl;
  template <typename native_resolver_type, typename native_endpoint_type>
  friend class resolver_impl;
  template <typename native_endpoint_type>
  friend class basic_resolver_impl;
  template <typename native_socket_type, typename native_endpoint_type>
  friend class stream_socket_impl;

  endpoint();

  endpoint(const endpoint&) = default;

  endpoint& operator=(const endpoint&) = default;

  endpoint(endpoint&&) noexcept = default;

  endpoint& operator=(endpoint&&) noexcept = default;

  virtual ~endpoint() = default;

  protocol_type protocol() const override;

  std::string to_string() const override;

  void set_url(const url_type& url) override;

  const url_type& get_url() const override;

 private:
  endpoint(endpoint_ptr native_handle);

  endpoint_const_ptr native_handle() const;

  endpoint_ptr native_handle();

  void swap(endpoint_ptr native_handle);

  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
