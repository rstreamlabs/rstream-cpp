// See LICENSE file in the project root for license information.

#include <iostream>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/plugin.hpp>
#include <rstream/core/version.hpp>
#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
#include <rstream/static-plugins/static-plugins.hpp>
#endif

static const char USAGE[] = R"(
rstream-inspect - (https://rstream.io/) - inspect rstream plugins and elements

this program is part of rstream-utils (https://rstream.io/download/utils) and was created using rstream C++ SDK (https://rstream.io/sdk)

usage:
  rstream-inspect plugins [-v] [--search-path=ARG...]
  rstream-inspect version
  rstream-inspect version -v [-j]
  rstream-inspect (-h|--help)

options:
  -h --help         show this screen
  -v --verbose      enable verbose mode
  -j --json         print result in JSON format
)";

const auto version = std::string("rstream-inspect ") + RSTREAM_VERSION;

void show_plugins(const std::vector<std::string>& search_paths)
{
  auto config = rstream::core::plugin::factory::default_config();
  if (!search_paths.empty()) {
    config["search_paths"] = std::list<std::string>(search_paths.begin(), search_paths.end());
  }
  rstream::core::plugin::factory factory(config);
#ifdef RSTREAM_ENABLE_STATIC_PLUGINS
  for (const auto& plugin : rstream::static_plugins::get_io_plugins()) {
    factory.register_plugin(plugin);
  }
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
    std::cout << "------------------ no element found -----------------" << std::endl;
  }
  else {
    std::cout << "------------------ listing elements -----------------" << std::endl
              << std::endl;
    for (const auto& element : elements) {
      std::cout << element << std::endl;
    }
  }
  std::cout << std::endl;
}

int main(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  if (args["version"].asBool()) {
    if (args["--verbose"].asBool()) {
      auto version = rstream::core::get_project_info();
      if (args["--json"].asBool()) {
        nlohmann::json json;
        json << version;
        std::cout << json.dump(2) << std::endl;
      }
      else {
        std::cout << version << std::endl;
      }
    }
    else {
      std::cout << RSTREAM_VERSION << std::endl;
    }
  }
  else if (args["plugins"].asBool()) {
    if (args["--verbose"].asBool()) {
      rstream::core::log::enable_ansicolor_stdout_mt();
    }
    show_plugins(args["--search-path"].asStringList());
  }
  else {
    std::cout << RSTREAM_VERSION << std::endl;
  }
  return 0;
}
