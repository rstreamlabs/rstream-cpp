// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/dispatch.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core/stream_traits.hpp>

#include <rstream/core/completion_handler.hpp>

#include "stream_socket.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

}
}  // namespace detail
}  // namespace io
}  // namespace rstream

namespace boost {
namespace beast {

namespace websocket {

template <class teardown_handler>
void async_teardown(role_type role, rstream::io::detail::stream::stream_socket& socket, teardown_handler&& handler);

template <class teardown_handler>
void async_teardown(role_type role, rstream::io::detail::stream::stream_socket& socket, teardown_handler&& handler)
{
  (void)role;
  rstream::core::invoke_completion_handler(
      socket.get_executor(), std::forward<teardown_handler>(handler),
      boost::system::error_code());
}

}  // namespace websocket

namespace detail {

template <>
void close_socket_impl::operator()<rstream::io::detail::stream::stream_socket>(rstream::io::detail::stream::stream_socket& socket) const;

}

}  // namespace beast
}  // namespace boost
