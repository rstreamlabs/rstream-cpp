// See LICENSE file in the project root for license information.

#pragma once

#include <utility>

#include <boost/beast/core/role.hpp>

#include <rstream/core/completion_handler.hpp>

#include "socket.hpp"

namespace rstream {
namespace io_rstrm {

// Beast's timeout path closes the transport through this ADL customization.
inline void beast_close_socket(socket& stream)
{
  boost::system::error_code ignored;
  stream.close(ignored);
}

// Match the generic rstream stream's teardown contract: there is no additional
// WebSocket transport handshake here. The application closes the socket after
// its WebSocket operations finish. Completion is deferred and preserves the
// handler's executor, allocator and cancellation association. It is not a
// barrier proving that the underlying transport has physically closed.
template <typename TeardownHandler>
void async_teardown(boost::beast::role_type role, socket& stream, TeardownHandler&& handler)
{
  (void)role;
  core::invoke_completion_handler(stream.get_executor(), std::forward<TeardownHandler>(handler), boost::system::error_code());
}

}  // namespace io_rstrm
}  // namespace rstream
