// See LICENSE file in the project root for license information.

#include "endpoint.hpp"

#include <rstream/config.hpp>

#include "error.hpp"
#include "object_base.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class RSTREAM_GNUC_INTERNAL endpoint::impl {
 public:
  impl();

  impl(endpoint_ptr native_handle);

  virtual ~impl() = default;

  protocol_type protocol() const;

  std::string to_string() const;

  void set_url(const url_type& url);

  const url_type& get_url() const;

  endpoint_const_ptr native_handle() const;

  endpoint_ptr native_handle();

  void swap(endpoint_ptr native_handle);

 private:
  bool initialized() const;

  endpoint_ptr m_native_handle;

  url_type m_url;
};

endpoint::endpoint()
    : m_impl(std::make_shared<impl>())
{
}

endpoint::endpoint(endpoint_ptr native_handle)
    : m_impl(std::make_shared<impl>(native_handle))
{
}

endpoint_base::protocol_type endpoint::protocol() const
{
  return m_impl->protocol();
}

std::string endpoint::to_string() const
{
  return m_impl->to_string();
}

void endpoint::set_url(const endpoint_base::url_type& url)
{
  m_impl->set_url(url);
}

const endpoint_base::url_type& endpoint::get_url() const
{
  return m_impl->get_url();
}

endpoint_const_ptr endpoint::native_handle() const
{
  return m_impl->native_handle();
}

endpoint_ptr endpoint::native_handle()
{
  return m_impl->native_handle();
}

void endpoint::swap(endpoint_ptr native_handle)
{
  m_impl->swap(native_handle);
}

endpoint::impl::impl() {}

endpoint::impl::impl(endpoint_ptr native_handle)
    : m_native_handle(native_handle)
{
}

endpoint_base::protocol_type endpoint::impl::protocol() const
{
  return initialized() ? m_native_handle->protocol() : error::code::uninitialized_object;
}

std::string endpoint::impl::to_string() const
{
  return initialized() ? m_native_handle->to_string() : "[uninitialized endpoint]";
}

void endpoint::impl::set_url(const endpoint_base::url_type& url)
{
  if (initialized()) {
    m_native_handle->set_url(url);
  }
}

const endpoint_base::url_type& endpoint::impl::get_url() const
{
  return initialized() ? m_native_handle->get_url() : m_url;
}

endpoint_const_ptr endpoint::impl::native_handle() const
{
  return m_native_handle;
}

endpoint_ptr endpoint::impl::native_handle()
{
  return m_native_handle;
}

void endpoint::impl::swap(endpoint_ptr native_handle)
{
  m_native_handle = native_handle;
}

bool endpoint::impl::initialized() const
{
  return m_native_handle != nullptr;
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
