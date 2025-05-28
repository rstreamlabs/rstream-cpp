// See LICENSE file in the project root for license information.

#include <iostream>

#include <rstream/core/plugin.hpp>

#include "interface.hpp"

#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_STATIC_DECLARE(sample_plugin_1)
RSTREAM_PLUGIN_STATIC_DECLARE(sample_plugin_2)
#endif

int main(int argc, char** argv)
{
  auto config = rstream::core::plugin::factory::default_config();
#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
  config["search_paths"] = {CMAKE_CURRENT_BINARY_DIR};
#endif
  rstream::core::plugin::factory factory(config);
#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
  factory.register_plugin(RSTREAM_PLUGIN_STATIC_REGISTER(sample_plugin_1));
  factory.register_plugin(RSTREAM_PLUGIN_STATIC_REGISTER(sample_plugin_2));
#endif
  std::cout << std::endl;
  auto plugins = factory.get_plugins();
  if (plugins.size() == 0) {
    std::cout << "------------------ no plugin found ------------------" << std::endl
              << std::endl;
  }
  else {
    std::cout << "------------------ listing plugins ------------------" << std::endl
              << std::endl;
    for (const auto& plugin : plugins) {
      std::cout << plugin << std::endl
                << std::endl;
    }
  }
  auto elements = factory.get_elements();
  if (elements.size() == 0) {
    std::cout << "------------------ no element found -----------------" << std::endl
              << std::endl;
  }
  else {
    std::cout << "------------------ listing elements -----------------" << std::endl
              << std::endl;
    for (const auto& element : elements) {
      std::cout << "[element: " << element.m_name << ", result: " << rstream::core::plugin::dynamic_element_cast<interface>(factory.create(element.m_name))->run() << "]" << std::endl;
    }
  }
  std::cout << std::endl;
  return 0;
}
