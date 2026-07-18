// See LICENSE file in the project root for license information.

#include <cassert>
#include <sstream>

#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io/address.hpp>

static void check_status_serialization()
{
  rstream::io_rstrm::status status;
  status.m_update   = "available";
  status.m_plan     = "pro";
  status.m_provider = "rstream";
  status.m_region   = "eu";
  std::ostringstream out;
  out << status;
  assert(out.str().find("available") != std::string::npos);

  nlohmann::json json = nlohmann::json::object();
  json << status;
  assert(json["update"] == "available");
  assert(json["plan"] == "pro");

  rstream::io_rstrm::status_extd extd;
  extd.m_status            = "online";
  extd.m_tunnel_id         = "tun_123";
  extd.m_forwarding        = "https://api.example";
  nlohmann::json extd_json = nlohmann::json::object();
  extd_json << extd;
  assert(extd_json["version"].is_string());
  assert(extd_json["status"] == "online");
  assert(extd_json["tunnel_id"] == "tun_123");
}

static void check_format_forwarding_address_for_published_http_tunnel()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_protocol = "http";
  properties.m_hostname = "viewer.example";
  properties.m_port     = 443;
  auto formatted        = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "https://viewer.example");

  properties.m_port = 8443;
  formatted         = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "https://viewer.example:8443");
}

static void check_format_forwarding_address_for_published_webtty_tunnel()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_protocol = "webtty";
  properties.m_hostname = "terminal.example";
  properties.m_port     = 443;
  auto formatted        = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "https://terminal.example (webtty)");

  properties.m_port = 8443;
  formatted         = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "https://terminal.example:8443 (webtty)");
}

static void check_format_forwarding_address_for_published_tcp_tunnel()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_protocol = rstream::io_rstrm::protocol::tcp;
  properties.m_hostname = "tcp.example";
  properties.m_port     = 10042;
  auto formatted        = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "tcp.example:10042 (tcp)");
}

static void check_format_forwarding_address_for_unpublished_tunnel()
{
  rstream::io_rstrm::tunnel_properties properties;
  auto formatted = rstream::io_rstrm::format_forwarding_address(properties);
  assert(!formatted);

  properties.m_name = "camera";
  formatted         = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "rstrm://camera (unpublished)");

  properties.m_name = boost::none;
  properties.m_id   = "tun_123";
  formatted         = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "rstrm://tun_123 (unpublished)");
}

static void check_format_forwarding_address_for_tls_host()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_protocol = "tls";
  properties.m_host     = "edge.example:9443";
  auto formatted        = rstream::io_rstrm::format_forwarding_address(properties);
  assert(formatted);
  assert(formatted.value() == "edge.example:9443 (tls)");
}

static void check_format_forwarded_address_for_http_upstream()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_protocol     = "http";
  properties.m_http_version = "h2";
  auto formatted            = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("127.0.0.1:80"), properties);
  assert(formatted);
  assert(formatted.value() == "http://127.0.0.1 (h2)");

  properties.m_upstream_tls = true;
  formatted                 = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("127.0.0.1:443"), properties);
  assert(formatted);
  assert(formatted.value() == "https://127.0.0.1");
}

static void check_format_forwarded_address_for_transport_modes()
{
  rstream::io_rstrm::tunnel_properties properties;
  auto formatted = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:9443"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:9443 (tcp)");

  properties.m_protocol = "dtls";
  formatted             = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:3478"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:3478 (udp)");

  properties.m_upstream_tls = true;
  formatted                 = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:3479"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:3479 (dtls)");

  properties.m_protocol = "quic";
  formatted             = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:4433"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:4433 (quic)");

  properties.m_upstream_tls = false;
  formatted                 = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:4443"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:4443 (quic)");

  properties.m_protocol = "webtty";
  formatted             = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:7681"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:7681 (webtty)");

  properties.m_protocol     = boost::none;
  properties.m_upstream_tls = true;
  formatted                 = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:443"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:443 (tls)");

  properties.m_protocol     = "tcp";
  properties.m_upstream_tls = false;
  properties.m_tls_mode     = "passthrough";
  formatted                 = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("10.0.0.5:8443"), properties);
  assert(formatted);
  assert(formatted.value() == "10.0.0.5:8443 (tls)");
}

static void check_format_forwarded_address_rejects_unusable_address()
{
  rstream::io_rstrm::tunnel_properties properties;
  auto formatted = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("tcp://"), properties);
  assert(!formatted);

  formatted = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("https://service.example/path"), properties);
  assert(formatted);
  assert(formatted.value() == "https://service.example/path (unparsed)");

  formatted = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address(":8080"), properties);
  assert(!formatted);

  formatted = rstream::io_rstrm::format_forwarded_address(rstream::io::make_address("service.example"), properties);
  assert(!formatted);
}

static void check_error_category_messages()
{
  auto code = rstream::io_rstrm::error::make_error_code(rstream::io_rstrm::error::code::unauthorized);
  assert(code.category() == rstream::io_rstrm::error::rstream_rstream_error_category());
  assert(code.message() == "unauthorized");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::success) == "success");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::invalid_endpoint) == "invalid endpoint");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::no_valid_endpoint) == "no valid endpoint");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::invalid_configuration) == "invalid configuration");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::invalid_state) == "invalid state");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::operation_aborted) == "operation aborted");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::operation_in_progress) == "another operation is in progress");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::operation_timeout) == "operation timeout");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::protocol_error) == "protocol error");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::server_error) == "server error");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::stream_not_found) == "stream not found");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::tunnel_not_found) == "tunnel not found");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::protocol_version_missing) == "protocol version missing");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::protocol_version_invalid) == "protocol version invalid");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::protocol_version_incompatible) == "protocol version incompatible");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::invalid_stream) == "invalid stream");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::feature_not_available) == "feature not available");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::service_unavailable) == "service unavailable");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::capacity_exhausted) == "capacity exhausted");
  assert(rstream::io_rstrm::to_string(rstream::io_rstrm::error::code::internal) == "internal error");
  assert(rstream::io_rstrm::error::make_error_code(2000).message() == "invalid request");
  assert(rstream::io_rstrm::to_string(static_cast<rstream::io_rstrm::error::code>(999)) == "unknown error");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_status_serialization();
  check_format_forwarding_address_for_published_http_tunnel();
  check_format_forwarding_address_for_published_webtty_tunnel();
  check_format_forwarding_address_for_published_tcp_tunnel();
  check_format_forwarding_address_for_unpublished_tunnel();
  check_format_forwarding_address_for_tls_host();
  check_format_forwarded_address_for_http_upstream();
  check_format_forwarded_address_for_transport_modes();
  check_format_forwarded_address_rejects_unusable_address();
  check_error_category_messages();
  return 0;
}
