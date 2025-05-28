// See LICENSE file in the project root for license information.

#include "plugin.hpp"

#include <rstream/config.hpp>
#include <rstream/core/plugin.hpp>
#include <rstream/io/stream.hpp>

static const auto g_plugin_description = rstream::core::plugin::make_plugin_descriptor({// plugin info
                                                                                        .m_name         = "io-rstrm",
                                                                                        .m_description  = "rstrm transport layer",
                                                                                        .m_version      = RSTREAM_VERSION,
                                                                                        .m_license      = RSTREAM_COPYING,
                                                                                        .m_release_date = boost::gregorian::from_string(RSTREAM_BUILD_DATE)},
                                                                                       {// elements
                                                                                        rstream::io::make_stream<rstream::plugin::io_rstrm::stream>()});

#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_EXPORT(g_plugin_description);
#else
RSTREAM_PLUGIN_STATIC_DEFINE(io_rstrm, g_plugin_description);
#endif

namespace rstream {
namespace plugin {
namespace io_rstrm {

}
}  // namespace plugin
}  // namespace rstream
