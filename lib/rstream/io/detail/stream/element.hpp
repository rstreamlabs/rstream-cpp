// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/plugin.hpp>
#include <rstream/io/io_object.hpp>

#include "endpoint_base.hpp"
#include "stream.hpp"

#define RSTREAM_STREAM_PREFIX "io.stream."

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class element : public rstream::core::plugin::element {
 public:
  using executor_type = io_object::executor_type;

  virtual ~element() = default;

  virtual endpoint_base::protocol_type::value_type protocol() const = 0;

  virtual resolver_ptr resolver(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const    = 0;
  virtual stream_socket_ptr socket(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const = 0;
  virtual acceptor_ptr acceptor(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const    = 0;
};

template <typename Fp, typename T>
struct make_object_ptr;

template <typename Rp, typename... Args, typename T>
struct make_object_ptr<Rp(Args...), T> {
 public:
  Rp operator()(Args... args)
  {
    return std::make_shared<T>(std::forward<Args>(args)...);
  }
};

template <typename Rp, typename... Args>
struct make_object_ptr<Rp(Args...), void> {
 public:
  Rp operator()(Args... args)
  {
    return nullptr;
  }
};

template <typename T>
class element_impl : public element, public std::enable_shared_from_this<element_impl<T>> {
 public:
  element_impl(const endpoint_base::protocol_type::value_type& protocol);

  endpoint_base::protocol_type::value_type protocol() const override;

  resolver_ptr resolver(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const override;
  stream_socket_ptr socket(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const override;
  acceptor_ptr acceptor(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const override;

 private:
  const endpoint_base::protocol_type::value_type m_protocol;
};

template <typename T>
element_impl<T>::element_impl(const endpoint_base::protocol_type::value_type& protocol)
    : m_protocol(protocol)
{
}

template <typename T>
endpoint_base::protocol_type::value_type element_impl<T>::protocol() const
{
  return m_protocol;
}

template <typename T>
resolver_ptr element_impl<T>::resolver(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const
{
  auto parent_ptr = std::enable_shared_from_this<element_impl>::shared_from_this();
  auto ptr        = make_object_ptr<resolver_ptr(const executor_type&, const endpoint_base::protocol_type::value_type&, element_const_ptr), typename T::resolver>()(executor, element_impl<T>::protocol(), parent_ptr);
  return resolver_ptr(ptr.get(), core::detail::plugin::object_deleter(ptr, parent_ptr));
}

template <typename T>
stream_socket_ptr element_impl<T>::socket(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const
{
  auto parent_ptr = std::enable_shared_from_this<element_impl>::shared_from_this();
  auto ptr        = make_object_ptr<stream_socket_ptr(const executor_type&, const endpoint_base::protocol_type::value_type&, element_const_ptr), typename T::stream_socket>()(executor, element_impl<T>::protocol(), parent_ptr);
  return stream_socket_ptr(ptr.get(), core::detail::plugin::object_deleter(ptr, parent_ptr));
}

template <typename T>
acceptor_ptr element_impl<T>::acceptor(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol) const
{
  auto parent_ptr = std::enable_shared_from_this<element_impl>::shared_from_this();
  auto ptr        = make_object_ptr<acceptor_ptr(const executor_type&, const endpoint_base::protocol_type::value_type&, element_const_ptr), typename T::acceptor>()(executor, element_impl<T>::protocol(), parent_ptr);
  return acceptor_ptr(ptr.get(), core::detail::plugin::object_deleter(ptr, parent_ptr));
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
