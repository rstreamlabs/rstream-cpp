// See LICENSE file in the project root for license information.

#include "websocket.hpp"

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

namespace detail {

template <>
void close_socket_impl::operator()<rstream::io::detail::stream::stream_socket>(rstream::io::detail::stream::stream_socket& socket) const
{
  boost::system::error_code tmp;
  socket.close(tmp);
}

}  // namespace detail

}  // namespace beast
}  // namespace boost
