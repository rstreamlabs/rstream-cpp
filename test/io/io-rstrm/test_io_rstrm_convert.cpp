// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <stdexcept>

#include <boost/asio/ip/address.hpp>

#include <rstream/io-rstrm/detail/convert.hpp>

namespace protobuf = rstream::io_rstrm::protobuf;

static void check_ip_address_roundtrip()
{
  protobuf::IpAddress v4_proto;
  auto v4 = boost::asio::ip::make_address("192.0.2.17");
  rstream::io_rstrm::detail::convert(v4_proto, v4);
  assert(v4_proto.has_v4());
  boost::asio::ip::address v4_decoded;
  rstream::io_rstrm::detail::convert(v4_decoded, v4_proto);
  assert(v4_decoded == v4);
  protobuf::IpAddress v6_proto;
  auto v6 = boost::asio::ip::make_address("2001:db8::1");
  rstream::io_rstrm::detail::convert(v6_proto, v6);
  assert(v6_proto.has_v6());
  assert(v6_proto.v6().size() == 16);
  boost::asio::ip::address v6_decoded;
  rstream::io_rstrm::detail::convert(v6_decoded, v6_proto);
  assert(v6_decoded == v6);
}

static void check_ip_address_rejects_invalid_proto()
{
  protobuf::IpAddress missing;
  boost::asio::ip::address decoded;
  bool missing_rejected = false;
  try {
    rstream::io_rstrm::detail::convert(decoded, missing);
  }
  catch (const std::invalid_argument&) {
    missing_rejected = true;
  }
  assert(missing_rejected);
  protobuf::IpAddress bad_v6;
  bad_v6.set_v6("too-short");
  bool short_v6_rejected = false;
  try {
    rstream::io_rstrm::detail::convert(decoded, bad_v6);
  }
  catch (const std::invalid_argument&) {
    short_v6_rejected = true;
  }
  assert(short_v6_rejected);
}

static void check_client_details_roundtrip()
{
  rstream::io_rstrm::client_details details;
  details.m_agent            = "agent";
  details.m_channel          = "stable";
  details.m_os               = "darwin";
  details.m_arch             = "arm64";
  details.m_version          = "1.2.3";
  details.m_token            = "secret-token";
  details.m_protocol_version = "1.4.1";
  protobuf::ClientDetails proto;
  rstream::io_rstrm::detail::convert(proto, details);
  assert(proto.has_token());
  assert(proto.token().value() == "secret-token");
  rstream::io_rstrm::client_details decoded;
  rstream::io_rstrm::detail::convert(decoded, proto);
  assert(decoded.m_agent == details.m_agent);
  assert(decoded.m_channel == details.m_channel);
  assert(decoded.m_os == details.m_os);
  assert(decoded.m_arch == details.m_arch);
  assert(decoded.m_version == details.m_version);
  assert(decoded.m_token == details.m_token);
  assert(decoded.m_protocol_version == details.m_protocol_version);
}

static void check_tunnel_properties_roundtrip()
{
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_id               = "tun_123";
  properties.m_name             = "api";
  properties.m_creation_date    = std::chrono::system_clock::time_point(std::chrono::seconds(12345));
  properties.m_type             = "bytestream";
  properties.m_publish          = false;
  properties.m_protocol         = "http";
  properties.m_labels["env"]    = "prod";
  properties.m_labels["owner"]  = "platform";
  properties.m_geoip            = {"FR", "US"};
  properties.m_trusted_ips      = {"203.0.113.0/24", "198.51.100.12/32"};
  properties.m_host             = "legacy.example";
  properties.m_hostname         = "api.example";
  properties.m_port             = 8443;
  properties.m_tls_mode         = "terminated";
  properties.m_tls_alpns        = {"h2", "http/1.1"};
  properties.m_tls_min_version  = "1.3";
  properties.m_tls_ciphers      = {"TLS_AES_128_GCM_SHA256"};
  properties.m_mtls             = true;
  properties.m_mtls_cacert_pem  = "pem";
  properties.m_http_version     = "2";
  properties.m_http_use_tls     = false;
  properties.m_upstream_tls     = true;
  properties.m_token_auth       = true;
  properties.m_rstream_auth     = true;
  properties.m_challenge_mode   = true;
  protobuf::TunnelProperties proto;
  rstream::io_rstrm::detail::convert(proto, properties);
  assert(proto.has_publish());
  assert(!proto.publish().value());
  assert(proto.labels().at("env") == "prod");
  assert(proto.trusted_ips().size() == 2);
  assert(proto.has_http_use_tls());
  assert(!proto.http_use_tls().value());
  rstream::io_rstrm::tunnel_properties decoded;
  rstream::io_rstrm::detail::convert(decoded, proto);
  assert(decoded.m_id == properties.m_id);
  assert(decoded.m_name == properties.m_name);
  assert(decoded.m_creation_date == properties.m_creation_date);
  assert(decoded.m_publish == properties.m_publish);
  assert(decoded.m_labels == properties.m_labels);
  assert(decoded.m_geoip == properties.m_geoip);
  assert(decoded.m_trusted_ips == properties.m_trusted_ips);
  assert(decoded.m_hostname == properties.m_hostname);
  assert(decoded.m_port == properties.m_port);
  assert(decoded.m_http_use_tls == properties.m_http_use_tls);
  assert(decoded.m_upstream_tls == properties.m_upstream_tls);
  assert(decoded.m_token_auth == properties.m_token_auth);
  assert(decoded.m_rstream_auth == properties.m_rstream_auth);
  assert(decoded.m_challenge_mode == properties.m_challenge_mode);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_ip_address_roundtrip();
  check_ip_address_rejects_invalid_proto();
  check_client_details_roundtrip();
  check_tunnel_properties_roundtrip();
  return 0;
}
