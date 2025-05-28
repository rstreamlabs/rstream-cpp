// See LICENSE file in the project root for license information.

#pragma once

#include <sstream>

#include <rstream/core/log.hpp>

#include "endpoint_base.hpp"
#include "object_base.hpp"
#include "ssl.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

template <typename native_endpoint_type>
class endpoint_impl : public endpoint_base, public object_base, public std::enable_shared_from_this<endpoint_impl<native_endpoint_type>> {
 public:
  endpoint_impl(const native_endpoint_type& endpoint, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  virtual ~endpoint_impl() = default;

  protocol_type protocol() const override;

  std::string to_string() const override;

  void set_url(const url_type& url) override;

  const url_type& get_url() const override;

  native_endpoint_type& get();

  const native_endpoint_type& get() const;

 protected:
  rstream::core::logger m_logger;

 private:
  native_endpoint_type m_endpoint;

  const endpoint_base::protocol_type::value_type m_protocol;

  const element_const_ptr m_parent_ptr;

  url_type m_url;
};

template <typename native_endpoint_type>
endpoint_impl<native_endpoint_type>::endpoint_impl(const native_endpoint_type& endpoint, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : m_logger({"rstream", "io", "endpoint", fmt::format("#{}", fmt::ptr(this))}),
      m_endpoint(endpoint),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_endpoint_type>
endpoint_base::protocol_type endpoint_impl<native_endpoint_type>::protocol() const
{
  return m_protocol;
}

template <typename native_endpoint_type>
std::string endpoint_impl<native_endpoint_type>::to_string() const
{
  std::stringstream stringstream;
  stringstream << m_endpoint;
  return stringstream.str();
}

template <typename native_endpoint_type>
void endpoint_impl<native_endpoint_type>::set_url(const endpoint_base::url_type& url)
{
  m_url = url;
}

template <typename native_endpoint_type>
const endpoint_base::url_type& endpoint_impl<native_endpoint_type>::get_url() const
{
  return m_url;
}

template <typename native_endpoint_type>
native_endpoint_type& endpoint_impl<native_endpoint_type>::endpoint_impl::get()
{
  return m_endpoint;
}

template <typename native_endpoint_type>
const native_endpoint_type& endpoint_impl<native_endpoint_type>::endpoint_impl::get() const
{
  return m_endpoint;
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
