// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/core/plugin.hpp>

#include "endpoint.hpp"
#include "resolver.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

class stream {
 public:
  using acceptor      = void;
  using endpoint      = serial::endpoint;
  using resolver      = serial::resolver;
  using stream_socket = serial::stream_socket;
  static rstream::core::plugin::element::info get_stream_info();
};

}  // namespace serial
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
