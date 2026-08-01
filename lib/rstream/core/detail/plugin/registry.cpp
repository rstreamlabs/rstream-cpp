// See LICENSE file in the project root for license information.

#include "registry.hpp"

#include <algorithm>
#include <mutex>

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

namespace {

struct registry {
  std::mutex mutex;
  std::vector<provider> providers;
};

registry& get_registry()
{
  static registry value;
  return value;
}

}  // namespace

bool register_static_plugin(provider provider)
{
  if (provider == nullptr) {
    return false;
  }
  auto& registry = get_registry();
  const std::lock_guard<std::mutex> lock(registry.mutex);
  if (std::find(registry.providers.begin(), registry.providers.end(), provider) != registry.providers.end()) {
    return false;
  }
  registry.providers.push_back(provider);
  return true;
}

std::vector<provider> get_static_plugins()
{
  auto& registry = get_registry();
  const std::lock_guard<std::mutex> lock(registry.mutex);
  return registry.providers;
}

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
