// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <string>

#include <boost/filesystem.hpp>

namespace rstream {
namespace file_server {

struct context {
  boost::filesystem::path m_workdir;
};

struct settings_server {
  std::uint32_t m_timeouts_start_ms;
};

}  // namespace file_server
}  // namespace rstream
