// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <boost/asio/dispatch.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>

#include "error.hpp"
#include "factory.hpp"
#include "object_base.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class RSTREAM_GNUC_INTERNAL resolver::impl {
 public:
  impl(const executor_type& executor);

  impl(resolver_ptr native_handle);

  virtual ~impl() = default;

  void cancel();

  void async_resolve(const boost::urls::url& url, async_resolve_completion_handler&& handler);

  resolver_const_ptr native_handle() const;

  resolver_ptr native_handle();

  void swap(resolver_ptr native_handle);

 private:
  bool initialized() const;

  void init(const boost::urls::url& url, boost::system::error_code& error_code);

  executor_type m_executor;

  resolver_ptr m_native_handle;
};

resolver::resolver(const executor_type& executor)
    : resolver_base(executor),
      m_impl(std::make_shared<impl>(executor))
{
}

resolver::resolver(resolver_ptr native_handle)
    : resolver_base(native_handle->get_executor()),
      m_impl(std::make_shared<impl>(native_handle))
{
}

void resolver::cancel()
{
  m_impl->cancel();
}

resolver_const_ptr resolver::native_handle() const
{
  return m_impl->native_handle();
}

resolver_ptr resolver::native_handle()
{
  return m_impl->native_handle();
}

void resolver::swap(resolver_ptr native_handle)
{
  m_impl->swap(native_handle);
}

void resolver::async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  m_impl->async_resolve(url, std::move(handler));
}

resolver::impl::impl(const executor_type& executor)
    : m_executor(executor)
{
}

resolver::impl::impl(resolver_ptr native_handle)
    : m_executor(native_handle->get_executor()),
      m_native_handle(native_handle)
{
}

void resolver::impl::cancel()
{
  if (m_native_handle) {
    m_native_handle->cancel();
  }
}

void resolver::impl::async_resolve(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    init(url, error_code);
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, results_type());
  }
  else {
    m_native_handle->async_resolve(url, std::move(handler));
  }
}

resolver_const_ptr resolver::impl::native_handle() const
{
  return m_native_handle;
}

resolver_ptr resolver::impl::native_handle()
{
  return m_native_handle;
}

void resolver::impl::swap(resolver_ptr native_handle)
{
  m_native_handle = native_handle;
}

bool resolver::impl::initialized() const
{
  return m_native_handle != nullptr;
}

void resolver::impl::init(const boost::urls::url& url, boost::system::error_code& error_code)
{
  m_native_handle = default_factory()->resolver(m_executor, url.scheme(), error_code);
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
