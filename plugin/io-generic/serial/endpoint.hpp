// See LICENSE file in the project root for license information.

#pragma once

#include <ostream>
#include <string>

#include <boost/asio/serial_port.hpp>

#include <rstream/io/detail/stream/endpoint_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

struct descriptor {
  std::string m_device;
  unsigned int m_baudrate;
};

std::ostream& operator<<(std::ostream& ostream, const descriptor& endpoint);

using endpoint = rstream::io::detail::stream::endpoint_impl<descriptor>;

}  // namespace serial
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
