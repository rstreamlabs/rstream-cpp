// See LICENSE file in the project root for license information.

#include <iostream>

#include <rstream/config.hpp>
#include <rstream/core/plugin.hpp>

#include "interface.hpp"

class element_3;

static const auto g_plugin_description = rstream::core::plugin::make_plugin_descriptor({// plugin info
                                                                                        .m_name         = "sample plugin #3",
                                                                                        .m_description  = "sample plugin #3",
                                                                                        .m_version      = RSTREAM_VERSION,
                                                                                        .m_license      = RSTREAM_COPYING,
                                                                                        .m_release_date = boost::gregorian::from_string(RSTREAM_BUILD_DATE)},
                                                                                       {// elements
                                                                                        rstream::core::plugin::make_element<element_3>()});

#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_EXPORT(g_plugin_description);
#else
RSTREAM_PLUGIN_STATIC_DEFINE(sample_plugin_3, g_plugin_description);
#endif

class RSTREAM_GNUC_INTERNAL element_3 : public interface, public rstream::core::plugin::element {
 public:
  static rstream::core::plugin::element::info get_element_info() { return {.m_name = "sample element #3", .m_description = "sample element #3"}; }
  long run() override { return 3; }
};
