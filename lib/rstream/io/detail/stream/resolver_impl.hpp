// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/plugin/common.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/resolver_base.hpp>

#include "endpoint.hpp"
#include "endpoint_impl.hpp"
#include "object_base.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

template <typename native_resolver_type, typename native_endpoint_type = typename native_resolver_type::results_type::endpoint_type>
class resolver_impl : public resolver_base<endpoint>, public object_base, public std::enable_shared_from_this<resolver_impl<native_resolver_type, native_endpoint_type>> {
 public:
  resolver_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  resolver_impl(const executor_type& executor, native_resolver_type&& resolver, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  virtual ~resolver_impl() = default;

  void cancel() override;

  native_resolver_type& get();

  const native_resolver_type& get() const;

 protected:
  rstream::core::logger m_logger;

 private:
  void async_resolve_internal(const boost::urls::url& urli, async_resolve_completion_handler&& handler) override;

  void async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const typename native_resolver_type::results_type&)>&& handler);

  native_resolver_type m_resolver;

  const endpoint_base::protocol_type::value_type m_protocol;

  const element_const_ptr m_parent_ptr;
};

template <typename native_endpoint_type>
class basic_resolver_impl : public resolver_base<endpoint> {
 public:
  basic_resolver_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  virtual ~basic_resolver_impl() = default;

  void cancel() override;

 protected:
  rstream::core::logger m_logger;

 private:
  void async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler) override;

  void resolve_internal(const boost::urls::url& url, native_endpoint_type& endpoint, boost::system::error_code& error_code);

  const endpoint_base::protocol_type::value_type m_protocol;

  const element_const_ptr m_parent_ptr;
};

template <typename native_resolver_type, typename native_endpoint_type>
resolver_impl<native_resolver_type, native_endpoint_type>::resolver_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : resolver_base<endpoint>(executor),
      m_logger({"rstream", "io", "resolver", fmt::format("#{}", fmt::ptr(this))}),
      m_resolver(executor),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_resolver_type, typename native_endpoint_type>
resolver_impl<native_resolver_type, native_endpoint_type>::resolver_impl(const executor_type& executor, native_resolver_type&& resolver, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : resolver_base<endpoint>(executor),
      m_logger({"rstream", "io", "resolver", fmt::format("#{}", fmt::ptr(this))}),
      m_resolver(std::move(resolver)),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_resolver_type, typename native_endpoint_type>
void resolver_impl<native_resolver_type, native_endpoint_type>::cancel()
{
  m_resolver.cancel();
}

template <typename native_resolver_type, typename native_endpoint_type>
native_resolver_type& resolver_impl<native_resolver_type, native_endpoint_type>::get()
{
  return m_resolver;
}

template <typename native_resolver_type, typename native_endpoint_type>
const native_resolver_type& resolver_impl<native_resolver_type, native_endpoint_type>::get() const
{
  return m_resolver;
}

template <typename native_resolver_type, typename native_endpoint_type>
void resolver_impl<native_resolver_type, native_endpoint_type>::async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  using completion_handler_type = rstream::core::completion_handler<void(const boost::system::error_code&, const typename native_resolver_type::results_type&)>&&;
  auto executor                 = boost::asio::get_associated_executor(handler, get_executor());
  auto completion_handler       = [url, protocol = m_protocol, ptr = resolver_impl<native_resolver_type, native_endpoint_type>::shared_from_this(), parent_ptr = m_parent_ptr, handler = std::move(handler)](boost::system::error_code error_code, const typename native_resolver_type::results_type& results) mutable {
    results_type endpoints;
    auto cause = error_code;
    if (!cause) {
      if (!results.empty()) {
        for (auto it = results.begin(); it != results.end(); ++it) {
          auto endpoint_ptr_ = std::make_shared<endpoint_impl<native_endpoint_type>>(it->endpoint(), protocol, parent_ptr);
          endpoint_ptr_->set_url(url);
          endpoints.push_back(resolver_entry<endpoint>(endpoint(endpoint_ptr(endpoint_ptr_.get(), core::detail::plugin::object_deleter(endpoint_ptr_, parent_ptr))), url));
        }
      }
    }
    rstream::core::invoke_completion_handler(ptr->get_executor(), std::move(handler), error_code, endpoints);
  };
  async_resolve_internal(url, (completion_handler_type)boost::asio::bind_executor(executor, std::move(completion_handler)));
}

template <typename native_resolver_type, typename native_endpoint_type>
void resolver_impl<native_resolver_type, native_endpoint_type>::async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const typename native_resolver_type::results_type&)>&& handler)
{
  (void)url;
  rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::asio::error::not_found, native_resolver_type::results_type());
}

template <typename native_endpoint_type>
basic_resolver_impl<native_endpoint_type>::basic_resolver_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : resolver_base<endpoint>(executor),
      m_logger({"rstream", "io", "resolver", fmt::format("#{}", fmt::ptr(this))}),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_endpoint_type>
void basic_resolver_impl<native_endpoint_type>::cancel()
{
}

template <typename native_endpoint_type>
void basic_resolver_impl<native_endpoint_type>::async_resolve_internal(const boost::urls::url& url, async_resolve_completion_handler&& handler)
{
  native_endpoint_type result;
  boost::system::error_code error_code;
  resolve_internal(url, result, error_code);
  results_type results;
  if (!error_code) {
    auto endpoint_ptr_ = std::make_shared<endpoint_impl<native_endpoint_type>>(result, m_protocol, m_parent_ptr);
    endpoint_ptr_->set_url(url);
    results.push_back((resolver_entry<endpoint>(endpoint(endpoint_ptr(endpoint_ptr_.get(), core::detail::plugin::object_deleter(endpoint_ptr_, m_parent_ptr))), url)));
  }
  rstream::core::invoke_completion_handler(get_executor(), std::move(handler), error_code, results);
}

template <typename native_endpoint_type>
void basic_resolver_impl<native_endpoint_type>::resolve_internal(const boost::urls::url& url, native_endpoint_type& endpoint, boost::system::error_code& error_code)
{
  error_code = boost::asio::error::not_found;
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
