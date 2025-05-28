// See LICENSE file in the project root for license information.

#include "stream_socket.hpp"

#include <rstream/io-rstrm/io-rstrm.hpp>

namespace rstream {
namespace plugin {
namespace io_rstrm {

}  // namespace io_rstrm
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_rstrm::stream_socket::open_internal(const rstream::io_rstrm::endpoint& endpoint, boost::system::error_code& error_code)
{
  m_socket.open(endpoint, error_code);
}

template <>
void rstream::plugin::io_rstrm::stream_socket::configure_internal(rstream::io::detail::stream::socket_mode mode, bool connected, const rstream::io_rstrm::endpoint& endpoint, const boost::urls::url& url, boost::system::error_code& error_code)
{
  // no need to configure the socket for server mode as it is already configured in the acceptor
  if (connected || mode == rstream::io::detail::stream::socket_mode::server) {
    return;
  }
  rstream::io_rstrm::settings_socket settings;
  rstream::io_rstrm::parse_settings_socket(url, settings, error_code);
  if (error_code) {
    return;
  }
  m_socket.settings(settings, error_code);
}
