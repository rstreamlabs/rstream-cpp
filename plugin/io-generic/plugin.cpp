// See LICENSE file in the project root for license information.

#include "plugin.hpp"

#include <rstream/config.hpp>
#include <rstream/core/plugin.hpp>
#include <rstream/io/stream.hpp>

static const auto g_plugin_description = rstream::core::plugin::make_plugin_descriptor({// plugin info
                                                                                        .m_name         = "io-generic",
                                                                                        .m_description  = "generic input/output transport layer",
                                                                                        .m_version      = RSTREAM_VERSION,
                                                                                        .m_license      = RSTREAM_COPYING,
                                                                                        .m_release_date = boost::gregorian::from_string(RSTREAM_BUILD_DATE)},
                                                                                       {// elements
                                                                                        rstream::io::make_stream<rstream::plugin::io_generic::serial::stream>(), rstream::io::make_stream<rstream::plugin::io_generic::tcp::stream>(), rstream::io::make_stream<rstream::plugin::io_generic::unix_::stream>()});

#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_EXPORT(g_plugin_description);
#else
RSTREAM_PLUGIN_STATIC_DEFINE(io_generic, g_plugin_description);
#endif

namespace rstream {
namespace plugin {
namespace io_generic {

}
}  // namespace plugin
}  // namespace rstream
