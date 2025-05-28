// See LICENSE file in the project root for license information.

#pragma once

#include <list>

#include <rstream/core/plugin.hpp>

namespace rstream {
namespace static_plugins {

std::list<rstream::core::plugin::plugin::ptr> get_io_plugins();

}  // namespace static_plugins
}  // namespace rstream
