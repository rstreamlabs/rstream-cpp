// See LICENSE file in the project root for license information.

#include "nperf.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>

#include <rstream/core/log.hpp>

#include "detail/statistics.hpp"

namespace rstream {
namespace nperf {

sample detail::compute_sample(const std::vector<std::uint64_t>& values, sample::type type)
{
  sample result = {};
  result.m_type = type;
  result.m_size = values.size();
  if (values.empty()) {
    return result;
  }
  const auto minmax                      = std::minmax_element(values.begin(), values.end());
  result.m_min_us                        = *minmax.first;
  result.m_max_us                        = *minmax.second;
  long double mean                       = 0;
  long double sum_of_squared_differences = 0;
  std::size_t count                      = 0;
  for (const auto value : values) {
    ++count;
    const auto delta = static_cast<long double>(value) - mean;
    mean += delta / static_cast<long double>(count);
    const auto adjusted_delta = static_cast<long double>(value) - mean;
    sum_of_squared_differences += delta * adjusted_delta;
  }
  result.m_mean_us  = static_cast<double>(mean);
  result.m_stdev_us = static_cast<double>(std::sqrt(sum_of_squared_differences / static_cast<long double>(count)));
  return result;
}

void parse_protocol(protocol& dst, const std::string& src)
{
  if (src == "websocket") {
    dst = protocol::websocket;
  }
  else if (src == "plain") {
    dst = protocol::plain;
  }
  else {
    throw std::runtime_error("invalid protocol '" + src + "'");
  }
}

nlohmann::json& operator<<(nlohmann::json& json, const sample& sample)
{
  if (sample.m_type == sample::type::connection) {
    json["type"] = "connection";
  }
  else if (sample.m_type == sample::type::handshake) {
    json["type"] = "handshake";
  }
  else {
    json["type"] = "ping";
  }
  json["size"]     = sample.m_size;
  json["min_us"]   = sample.m_min_us;
  json["max_us"]   = sample.m_max_us;
  json["mean_us"]  = sample.m_mean_us;
  json["stdev_us"] = sample.m_stdev_us;
  return json;
}

nlohmann::json& operator<<(nlohmann::json& json, const speed& speed)
{
  json["measured_bytes"]  = speed.m_measured_bytes;
  json["elapsed_time_ms"] = speed.m_elapsed_time_ms;
  json["max_time_ms"]     = speed.m_max_time_ms;
  return json;
}

nlohmann::json& operator<<(nlohmann::json& json, const metrics& metrics)
{
  json["final"]     = metrics.m_final;
  json["timestamp"] = rstream::core::format_timestamp(metrics.m_timestamp);
  if (metrics.m_data.type() == typeid(sample)) {
    json["sample"] << boost::get<sample>(metrics.m_data);
  }
  else if (metrics.m_data.type() == typeid(speed)) {
    if (metrics.m_options & option::download) {
      json["type"] = "download";
    }
    else if (metrics.m_options & option::upload) {
      json["type"] = "upload";
    }
    else {
      throw std::runtime_error("message has invalid/unknwon type");
    }
    json["speed"] << boost::get<speed>(metrics.m_data);
  }
  else if (metrics.m_data.type() == typeid(boost::system::error_code)) {
    json["error"] = boost::get<boost::system::error_code>(metrics.m_data).message();
  }
  return json;
}

}  // namespace nperf
}  // namespace rstream
