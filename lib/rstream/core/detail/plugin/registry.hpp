// See LICENSE file in the project root for license information.

#pragma once

#include <vector>

#include "plugin.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

using provider = plugin::ptr (*)();

bool register_static_plugin(provider provider);

std::vector<provider> get_static_plugins();

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
