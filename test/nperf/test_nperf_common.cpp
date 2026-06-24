// See LICENSE file in the project root for license information.

#include <cassert>
#include <stdexcept>
#include <string>

#include <rstream/nperf/detail/convert.hpp>
#include <rstream/nperf/error.hpp>
#include <rstream/nperf/nperf.hpp>
#include <rstream/nperf/protobuf/messages.pb.h>

namespace nperf    = rstream::nperf;
namespace detail   = rstream::nperf::detail;
namespace protobuf = rstream::nperf::protobuf;

static void check_protocol_parsing()
{
  nperf::protocol protocol;
  nperf::parse_protocol(protocol, "plain");
  assert(protocol == nperf::protocol::plain);
  nperf::parse_protocol(protocol, "websocket");
  assert(protocol == nperf::protocol::websocket);
  bool rejected = false;
  try {
    nperf::parse_protocol(protocol, "udp");
  }
  catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

static void check_option_conversion()
{
  unsigned int options = 0;
  detail::convert(options, protobuf::Option::OPTION_PING);
  assert(options == nperf::option::ping);

  options = 0;
  detail::convert(options, protobuf::Option::OPTION_DOWNLOAD);
  assert(options == nperf::option::download);

  options = 0;
  detail::convert(options, protobuf::Option::OPTION_UPLOAD);
  assert(options == nperf::option::upload);

  protobuf::Option option = protobuf::Option::OPTION_UPLOAD;
  detail::convert(option, nperf::option::ping | nperf::option::download);
  assert(option == protobuf::Option::OPTION_PING);
  detail::convert(option, nperf::option::download);
  assert(option == protobuf::Option::OPTION_DOWNLOAD);
  detail::convert(option, nperf::option::upload);
  assert(option == protobuf::Option::OPTION_UPLOAD);
}

static void check_json_serialization()
{
  nperf::sample sample = {
      .m_type     = nperf::sample::type::ping,
      .m_size     = 32,
      .m_min_us   = 1,
      .m_max_us   = 9,
      .m_mean_us  = 4.5,
      .m_stdev_us = 2.0,
  };
  nlohmann::json json;
  json << sample;
  assert(json["type"] == "ping");
  assert(json["size"] == 32);

  nperf::speed speed = {
      .m_measured_bytes  = 4096,
      .m_elapsed_time_ms = 100,
      .m_max_time_ms     = 200,
  };
  json = {};
  json << speed;
  assert(json["measured_bytes"] == 4096);

  nperf::metrics metrics = {
      .m_final     = true,
      .m_options   = nperf::option::download,
      .m_timestamp = nperf::timestamp{},
      .m_data      = speed,
  };
  json = {};
  json << metrics;
  assert(json["final"] == true);
  assert(json["type"] == "download");
  assert(json["speed"]["elapsed_time_ms"] == 100);

  metrics.m_options = 0;
  bool rejected     = false;
  try {
    json << metrics;
  }
  catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);

  auto error     = nperf::error::make_error_code(nperf::error::code::protocol_error);
  metrics.m_data = error;
  json           = {};
  json << metrics;
  assert(json["error"] == "protocol error");
}

static void check_error_category()
{
  assert(nperf::to_string(nperf::error::code::success) == "success");
  assert(nperf::to_string(nperf::error::code::invalid_state) == "invalid state");
  assert(nperf::to_string(nperf::error::code::no_valid_endpoint) == "no valid endpoint");
  assert(nperf::to_string(nperf::error::code::operation_aborted) == "operation aborted");
  assert(nperf::to_string(nperf::error::code::operation_timeout) == "operation timeout");
  assert(nperf::to_string(nperf::error::code::protocol_error) == "protocol error");
  assert(nperf::to_string(nperf::error::code::unexpected_close) == "connection unexpectedly closed");
  assert(nperf::to_string(nperf::error::code::invalid_argument) == "invalid argument");
  assert(nperf::to_string(static_cast<nperf::error::code>(9999)) == "unknown error");

  auto code = nperf::error::make_error_code(static_cast<int>(nperf::error::code::unexpected_close));
  assert(code);
  assert(code.message() == "connection unexpectedly closed");
  assert(std::string(code.category().name()) == "rstream::nperf::error::category");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_protocol_parsing();
  check_option_conversion();
  check_json_serialization();
  check_error_category();
  return 0;
}
