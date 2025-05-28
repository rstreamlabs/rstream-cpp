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
namespace tcp {

class stream {
 public:
  using acceptor      = tcp::acceptor;
  using endpoint      = tcp::endpoint;
  using resolver      = tcp::resolver;
  using stream_socket = tcp::stream_socket;
  static rstream::core::plugin::element::info get_stream_info();
};

}  // namespace tcp
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
