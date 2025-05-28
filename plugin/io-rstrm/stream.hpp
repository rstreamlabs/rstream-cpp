// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/core/plugin.hpp>

#include "acceptor.hpp"
#include "endpoint.hpp"
#include "resolver.hpp"
#include "stream_socket.hpp"

namespace rstream {
namespace plugin {
namespace io_rstrm {

class stream {
 public:
  using acceptor      = io_rstrm::acceptor;
  using endpoint      = io_rstrm::endpoint;
  using resolver      = io_rstrm::resolver;
  using stream_socket = io_rstrm::stream_socket;
  static rstream::core::plugin::element::info get_stream_info();
};

}  // namespace io_rstrm
}  // namespace plugin
}  // namespace rstream
