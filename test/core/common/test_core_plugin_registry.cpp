// See LICENSE file in the project root for license information.

#include <cassert>
#include <thread>
#include <vector>

#include <rstream/core/detail/plugin/registry.hpp>

namespace {

rstream::core::detail::plugin::plugin::ptr make_plugin()
{
  return nullptr;
}

}  // namespace

int main()
{
  constexpr std::size_t thread_count = 16;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    threads.emplace_back([]() {
      rstream::core::detail::plugin::register_static_plugin(&make_plugin);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  const auto providers = rstream::core::detail::plugin::get_static_plugins();
  std::size_t matches  = 0;
  for (const auto provider : providers) {
    if (provider == &make_plugin) {
      ++matches;
    }
  }
  assert(matches == 1);
  assert(!rstream::core::detail::plugin::register_static_plugin(nullptr));
  assert(!rstream::core::detail::plugin::register_static_plugin(&make_plugin));
  return 0;
}
