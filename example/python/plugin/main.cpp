// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/filesystem.hpp>

#include <rstream/core/plugin.hpp>

#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
#include <rstream/python/plugin.hpp>
#endif

#include "interface.hpp"

#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
RSTREAM_PLUGIN_STATIC_DECLARE(python_plugin_wrapper)
RSTREAM_PLUGIN_STATIC_DECLARE(sample_plugin_3)
#endif

int main()
{
  auto config = rstream::core::plugin::factory::default_config();
#ifndef RSTREAM_ENABLE_STATIC_PLUGINS
  config["search_paths"]                 = {PYTHON_WRAPPER_LIBRARY_OUTPUT_DIRECTORY, CMAKE_CURRENT_BINARY_DIR};
  config["python"]["search_paths"]       = {};
  config["python"]["extra_search_paths"] = {CMAKE_CURRENT_SOURCE_DIR};
#endif
  rstream::core::plugin::factory factory(config);
#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
  {
    auto python_modules = {
        boost::filesystem::path(CMAKE_CURRENT_SOURCE_DIR) / "plugin_1.py",
        boost::filesystem::path(CMAKE_CURRENT_SOURCE_DIR) / "plugin_2.py"};
    auto python_plugin = RSTREAM_PLUGIN_PYTHON_STATIC_REGISTER();
    for (const auto& python_module : python_modules) {
      python_plugin->register_module(python_module.string());
    }
    factory.register_plugin(python_plugin);
  }
  factory.register_plugin(RSTREAM_PLUGIN_STATIC_REGISTER(sample_plugin_3));
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
