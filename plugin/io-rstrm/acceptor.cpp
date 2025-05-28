// See LICENSE file in the project root for license information.

#include "acceptor.hpp"

#include <rstream/io-rstrm/io-rstrm.hpp>

namespace rstream {
namespace plugin {
namespace io_rstrm {

}
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_rstrm::acceptor::open_internal(const rstream::io_rstrm::endpoint& endpoint, boost::system::error_code& error_code)
{
  m_acceptor.open(endpoint, error_code);
}

template <>
void rstream::plugin::io_rstrm::acceptor::configure_internal(const rstream::io_rstrm::endpoint& endpoint, const boost::urls::url& url, boost::system::error_code& error_code)
{
  rstream::io_rstrm::settings_acceptor settings;
  rstream::io_rstrm::parse_settings_acceptor(url, settings, error_code);
  if (error_code) {
    return;
  }
  m_acceptor.settings(settings, error_code);
}
