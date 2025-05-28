// See LICENSE file in the project root for license information.

#include "nperf.hpp"

#include <iomanip>

#include <rstream/core/log.hpp>

namespace rstream {
namespace nperf {

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
