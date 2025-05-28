// See LICENSE file in the project root for license information.

#include "plugin.hpp"

#include <rstream/config.hpp>
#include <rstream/python/plugin.hpp>

static const rstream::core::plugin::plugin::info g_plugin_info = {
    // plugin info
    .m_name         = "python-wrapper",
    .m_description  = "python plugin wrapper",
    .m_version      = RSTREAM_VERSION,
    .m_license      = RSTREAM_COPYING,
    .m_release_date = boost::gregorian::from_string(RSTREAM_BUILD_DATE),
};

#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_EXPORT_FULL(rstream::python::plugin::plugin, g_plugin_info);
#else
RSTREAM_PLUGIN_STATIC_DEFINE_FULL(rstream::python::plugin::plugin, python_plugin_wrapper, g_plugin_info);
#endif

namespace rstream {
namespace binding {
namespace python {

}
}  // namespace binding
}  // namespace rstream
