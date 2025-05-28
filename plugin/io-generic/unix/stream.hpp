// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/core/plugin.hpp>

#include "acceptor.hpp"
#include "endpoint.hpp"
#include "resolver.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

class stream {
 public:
  using acceptor      = unix_::acceptor;
  using endpoint      = unix_::endpoint;
  using resolver      = unix_::resolver;
  using stream_socket = unix_::stream_socket;
  static rstream::core::plugin::element::info get_stream_info();
};

}  // namespace unix_
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
