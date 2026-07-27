// See LICENSE file in the project root for license information.

#include <cassert>
#include <filesystem>

#include <nlohmann/json.hpp>

#include <rstream/core/plugin.hpp>

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  const std::filesystem::path plugin(RSTREAM_TEST_PLUGIN_WITHOUT_ENTRYPOINT_PATH);
  assert(std::filesystem::is_regular_file(plugin));
  const auto pattern = "^" + plugin.stem().string() + "\\" + plugin.extension().string() + "$";
  rstream::core::plugin::factory factory({
      {"pattern", pattern},
      {"search_paths", {plugin.parent_path().string()}},
  });
  assert(factory.get_plugins().empty());
  return 0;
}
