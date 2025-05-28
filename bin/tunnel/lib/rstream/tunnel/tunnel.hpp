// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <functional>
#include <ostream>

#include <boost/optional.hpp>

#include <nlohmann/json.hpp>

#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>

namespace rstream {
namespace tunnel {

struct settings_proxy {
  std::uint32_t m_read_downstream_buffer_size_bytes;
  std::uint32_t m_read_upstream_buffer_size_bytes;
  struct {
    std::uint32_t m_open;
  } m_timeouts_ms;
};

struct status_proxy : io_rstrm::status_extd {
  boost::optional<std::string> m_forwarded;
};

std::ostream& operator<<(std::ostream& os, const status_proxy& status);

nlohmann::json& operator<<(nlohmann::json& json, const status_proxy& status);

using on_status_cb = std::function<void(const status_proxy&)>;

using on_new_connection_cb = std::function<void(const io_rstrm::endpoint&)>;

}  // namespace tunnel
}  // namespace rstream
