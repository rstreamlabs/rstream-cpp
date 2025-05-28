// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/core/plugin.hpp>

#include "detail/stream/acceptor.hpp"
#include "detail/stream/element.hpp"
#include "detail/stream/endpoint.hpp"
#include "detail/stream/resolver.hpp"
#include "detail/stream/stream.hpp"
#include "detail/stream/stream_socket.hpp"

namespace rstream {
namespace io {

class stream {
 public:
  using acceptor      = detail::stream::acceptor;
  using endpoint      = detail::stream::endpoint;
  using resolver      = detail::stream::resolver;
  using stream_socket = detail::stream::stream_socket;
  using socket        = stream_socket;  // for compatibility purposes
};

template <class T, class... Args>
core::plugin::element::handle make_stream(Args&&... args)
{
  auto stream_info = T::get_stream_info();
  auto protocol    = stream_info.m_name;
  stream_info.m_name.insert(0, RSTREAM_STREAM_PREFIX);
  core::plugin::element::handle handle = {
      .m_info        = stream_info,
      .m_create_func = [protocol, args...]() {
        return std::make_shared<detail::stream::element_impl<T>>(protocol, args...);
      }};
  return handle;
}

}  // namespace io
}  // namespace rstream
