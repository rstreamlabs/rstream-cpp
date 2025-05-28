// See LICENSE file in the project root for license information.

#include "static-plugins.hpp"

RSTREAM_PLUGIN_STATIC_DECLARE(io_generic)
RSTREAM_PLUGIN_STATIC_DECLARE(io_rstrm)

namespace rstream {
namespace static_plugins {

std::list<rstream::core::plugin::plugin::ptr> get_io_plugins()
{
  std::list<rstream::core::plugin::plugin::ptr> plugins;
  plugins.push_back(RSTREAM_PLUGIN_STATIC_REGISTER(io_generic));
  plugins.push_back(RSTREAM_PLUGIN_STATIC_REGISTER(io_rstrm));
  return plugins;
}

}  // namespace static_plugins
}  // namespace rstream
