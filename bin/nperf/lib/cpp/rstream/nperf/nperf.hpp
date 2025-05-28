// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

#include <boost/system/system_error.hpp>
#include <boost/variant.hpp>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define INIT_NPERF_OPTION(value) (1U << value)
#else
#define INIT_NPERF_OPTION(value) \
  (unsigned int) { 1U << value }
#endif

namespace rstream {
namespace nperf {

enum protocol {
  websocket = 0,
  plain     = 1
};

enum option {
  ping     = INIT_NPERF_OPTION(1),
  download = INIT_NPERF_OPTION(2),
  upload   = INIT_NPERF_OPTION(3)
};

using options = unsigned int;

using timestamp = std::chrono::time_point<std::chrono::system_clock>;

struct sample {
  enum class type {
    connection = 0,
    handshake  = 1,
    ping       = 2,
  };
  type m_type;
  std::size_t m_size;
  std::uint64_t m_min_us;
  std::uint64_t m_max_us;
  double m_mean_us;
  double m_stdev_us;
};

struct speed {
  std::uint64_t m_measured_bytes;
  std::uint32_t m_elapsed_time_ms;
  std::uint32_t m_max_time_ms;
};

struct metrics {
  using data = boost::variant<sample, speed, boost::system::error_code>;
  bool m_final;
  options m_options;
  timestamp m_timestamp;
  data m_data;
};

using on_metrics_cb = std::function<void(const metrics&)>;

struct settings {
  std::uint32_t m_buffer_size;
  std::uint32_t m_timeouts_max_time_ms;
  std::uint32_t m_timeouts_open_close_ms;
  protocol m_protocol;
};

struct settings_client {
  settings m_common;
  std::uint32_t m_execution_count;
  std::uint32_t m_max_ping;
  std::uint32_t m_period_metrics_ms;
  std::uint32_t m_period_ms;
  std::uint32_t m_ping_buffer_size;
  std::uint32_t m_sessions;
  std::uint64_t m_max_data_bytes;
  bool m_retry;
};

struct settings_server {
  settings m_common;
  std::uint32_t m_timeouts_start_ms;
};

void parse_protocol(protocol& dst, const std::string& src);

nlohmann::json& operator<<(nlohmann::json& json, const sample& sample);

nlohmann::json& operator<<(nlohmann::json& json, const speed& speed);

nlohmann::json& operator<<(nlohmann::json& json, const metrics& metrics);

}  // namespace nperf
}  // namespace rstream
