// See LICENSE file in the project root for license information.

#include "stream_socket.hpp"

#include <rstream/io/detail/stream/url.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::tcp::stream_socket::configure_internal(rstream::io::detail::stream::socket_mode mode, bool connected, const boost::asio::ip::tcp::endpoint&, const boost::urls::url& url, boost::system::error_code& error_code)
{
  if (!connected) {
    return;
  }
  (void)mode;
  const auto params = rstream::io::detail::stream::url_params(url);
  bool no_delay     = true;
  if (!error_code) {
    auto it = params.find("tcp.no_delay");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(no_delay, *it, error_code);
    }
  }
  bool keep_alive = true;
  if (!error_code) {
    auto it = params.find("tcp.keep_alive");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(keep_alive, *it, error_code);
    }
  }
  if (!error_code) {
#ifdef DEBUG_BUILD
    m_logger->trace("no-delay socket option set to {}", no_delay);
#endif
    m_socket.set_option(boost::asio::ip::tcp::no_delay(no_delay), error_code);
  }
  if (!error_code) {
#ifdef DEBUG_BUILD
    m_logger->trace("keep-alive socket option set to {}", keep_alive);
#endif
    m_socket.set_option(boost::asio::socket_base::keep_alive(keep_alive), error_code);
  }
}
