// See LICENSE file in the project root for license information.

#include "factory.hpp"

#include <rstream/config.hpp>
#include <rstream/core/plugin.hpp>

#include "element.hpp"
#include "error.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class RSTREAM_GNUC_INTERNAL factory::impl {
 public:
  impl(const core::plugin::config& config);

  resolver_ptr resolver(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);
  stream_socket_ptr socket(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);
  acceptor_ptr acceptor(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);

 private:
  element_const_ptr get(const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code);

  rstream::core::plugin::factory m_factory;
};

factory::factory()
{
  auto config = rstream::core::plugin::factory::default_config();
  m_impl      = std::make_shared<impl>(config);
}

resolver_ptr factory::resolver(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  return m_impl->resolver(executor, protocol, error_code);
}

stream_socket_ptr factory::socket(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  return m_impl->socket(executor, protocol, error_code);
}

acceptor_ptr factory::acceptor(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  return m_impl->acceptor(executor, protocol, error_code);
}

factory::ptr default_factory()
{
  static factory::ptr default_factory = std::make_shared<factory>();
  return default_factory;
}

factory::impl::impl(const core::plugin::config& config)
    : m_factory(config)
{
}

resolver_ptr factory::impl::resolver(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  auto ptr = get(protocol, error_code);
  return error_code ? nullptr : ptr->resolver(executor, protocol.value());
}

stream_socket_ptr factory::impl::socket(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  auto ptr = get(protocol, error_code);
  return error_code ? nullptr : ptr->socket(executor, protocol.value());
}

acceptor_ptr factory::impl::acceptor(const executor_type& executor, const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  auto ptr = get(protocol, error_code);
  return error_code ? nullptr : ptr->acceptor(executor, protocol.value());
}

element_const_ptr factory::impl::get(const endpoint_base::protocol_type& protocol, boost::system::error_code& error_code)
{
  element_const_ptr ptr = nullptr;
  if (protocol.has_error()) {
    error_code = error::code::uninitialized_object;
  }
  else {
    ptr = std::dynamic_pointer_cast<class element>(m_factory.create(std::string(RSTREAM_STREAM_PREFIX) + protocol.value(), error_code));
  }
  return ptr;
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
