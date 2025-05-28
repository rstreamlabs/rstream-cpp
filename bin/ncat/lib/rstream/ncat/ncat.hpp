// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>

namespace rstream {
namespace ncat {

struct settings {
};

struct settings_client {
  settings m_common;
};

struct settings_server {
  settings m_common;
  std::uint32_t m_read_downstream_buffer_size_bytes;
  std::uint32_t m_read_upstream_buffer_size_bytes;
  struct {
    std::uint32_t m_start;
    std::uint32_t m_open;
  } m_timeouts_ms;
};

}  // namespace ncat
}  // namespace rstream
