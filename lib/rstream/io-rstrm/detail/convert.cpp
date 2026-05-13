// See LICENSE file in the project root for license information.

#include "convert.hpp"

namespace rstream {
namespace io_rstrm {
namespace detail {

void convert(protobuf::IpAddress& dst, const boost::asio::ip::address& src)
{
  if (src.is_v4()) {
    dst.set_v4(src.to_v4().to_uint());
  }
  else {
    const auto& bytes = src.to_v6().to_bytes();
    dst.set_v6(std::string(std::begin(bytes), std::end(bytes)));
  }
}

void convert(boost::asio::ip::address& dst, const protobuf::IpAddress& src)
{
  if (src.has_v4()) {
    dst = boost::asio::ip::address_v4(src.v4());
  }
  else if (src.has_v6()) {
    const auto& str = src.v6();
    if (str.size() != 16) {
      throw std::invalid_argument("Invalid IPv6 address length");
    }
    boost::asio::ip::address_v6::bytes_type bytes;
    std::copy(str.begin(), str.end(), bytes.begin());
    dst = boost::asio::ip::address_v6(bytes);
  }
  else {
    throw std::invalid_argument("No address field set");
  }
}

void convert(protobuf::ClientDetails& dst, const client_details& src)
{
  if (src.m_agent) {
    google::protobuf::StringValue str;
    str.set_value(src.m_agent.get());
    dst.mutable_agent()->CopyFrom(str);
  }
  if (src.m_channel) {
    google::protobuf::StringValue str;
    str.set_value(src.m_channel.get());
    dst.mutable_channel()->CopyFrom(str);
  }
  if (src.m_os) {
    google::protobuf::StringValue str;
    str.set_value(src.m_os.get());
    dst.mutable_os()->CopyFrom(str);
  }
  if (src.m_arch) {
    google::protobuf::StringValue str;
    str.set_value(src.m_arch.get());
    dst.mutable_arch()->CopyFrom(str);
  }
  if (src.m_version) {
    google::protobuf::StringValue str;
    str.set_value(src.m_version.get());
    dst.mutable_version()->CopyFrom(str);
  }
  if (src.m_token) {
    google::protobuf::StringValue str;
    str.set_value(src.m_token.get());
    dst.mutable_token()->CopyFrom(str);
  }
  if (src.m_protocol_version) {
    google::protobuf::StringValue str;
    str.set_value(src.m_protocol_version.get());
    dst.mutable_protocol_version()->CopyFrom(str);
  }
}

void convert(client_details& dst, const protobuf::ClientDetails& src)
{
  if (src.has_agent()) {
    dst.m_agent = src.agent().value();
  }
  if (src.has_channel()) {
    dst.m_channel = src.channel().value();
  }
  if (src.has_os()) {
    dst.m_os = src.os().value();
  }
  if (src.has_arch()) {
    dst.m_arch = src.arch().value();
  }
  if (src.has_version()) {
    dst.m_version = src.version().value();
  }
  if (src.has_token()) {
    dst.m_token = src.token().value();
  }
  if (src.has_protocol_version()) {
    dst.m_protocol_version = src.protocol_version().value();
  }
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

void convert(protobuf::TunnelProperties& dst, const tunnel_properties& src)
{
  if (src.m_id) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_id);
    dst.mutable_id()->CopyFrom(val);
  }
  if (src.m_creation_date) {
    google::protobuf::Timestamp ts;
    ts.set_seconds(std::chrono::duration_cast<std::chrono::seconds>(src.m_creation_date->time_since_epoch()).count());
    dst.mutable_creation_date()->CopyFrom(ts);
  }
  if (src.m_name) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_name);
    dst.mutable_name()->CopyFrom(val);
  }
  if (src.m_type) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_type);
    dst.mutable_type()->CopyFrom(val);
  }
  if (src.m_publish) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_publish);
    dst.mutable_publish()->CopyFrom(val);
  }
  if (src.m_protocol) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_protocol);
    dst.mutable_protocol()->CopyFrom(val);
  }
  for (const auto& kv : src.m_labels) {
    (*dst.mutable_labels())[kv.first] = kv.second;
  }
  for (const auto& val : src.m_geoip) {
    dst.add_geoip(val);
  }
  for (const auto& val : src.m_trusted_ips) {
    dst.add_trusted_ips(val);
  }
  if (src.m_host) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_host);
    dst.mutable_host()->CopyFrom(val);
  }
  if (src.m_hostname) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_hostname);
    dst.mutable_hostname()->CopyFrom(val);
  }
  if (src.m_port) {
    google::protobuf::UInt32Value val;
    val.set_value(*src.m_port);
    dst.mutable_port()->CopyFrom(val);
  }
  if (src.m_tls_mode) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_tls_mode);
    dst.mutable_tls_mode()->CopyFrom(val);
  }
  for (const auto& val : src.m_tls_alpns) {
    dst.add_tls_alpns(val);
  }
  if (src.m_tls_min_version) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_tls_min_version);
    dst.mutable_tls_min_version()->CopyFrom(val);
  }
  for (const auto& val : src.m_tls_ciphers) {
    dst.add_tls_ciphers(val);
  }
  if (src.m_mtls_auth) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_mtls_auth);
    dst.mutable_mtls_auth()->CopyFrom(val);
  }
  if (src.m_http_version) {
    google::protobuf::StringValue val;
    val.set_value(*src.m_http_version);
    dst.mutable_http_version()->CopyFrom(val);
  }
  if (src.m_http_use_tls) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_http_use_tls);
    dst.mutable_http_use_tls()->CopyFrom(val);
  }
  if (src.m_upstream_tls) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_upstream_tls);
    dst.mutable_upstream_tls()->CopyFrom(val);
  }
  if (src.m_token_auth) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_token_auth);
    dst.mutable_token_auth()->CopyFrom(val);
  }
  if (src.m_rstream_auth) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_rstream_auth);
    dst.mutable_rstream_auth()->CopyFrom(val);
  }
  if (src.m_challenge_mode) {
    google::protobuf::BoolValue val;
    val.set_value(*src.m_challenge_mode);
    dst.mutable_challenge_mode()->CopyFrom(val);
  }
}

void convert(tunnel_properties& dst, const protobuf::TunnelProperties& src)
{
  if (src.has_id()) {
    dst.m_id = src.id().value();
  }
  if (src.has_creation_date()) {
    dst.m_creation_date = std::chrono::system_clock::time_point(std::chrono::seconds(src.creation_date().seconds()));
  }
  if (src.has_name()) {
    dst.m_name = src.name().value();
  }
  if (src.has_type()) {
    dst.m_type = src.type().value();
  }
  if (src.has_publish()) {
    dst.m_publish = src.publish().value();
  }
  if (src.has_protocol()) {
    dst.m_protocol = src.protocol().value();
  }
  for (const auto& kv : src.labels()) {
    dst.m_labels[kv.first] = kv.second;
  }
  dst.m_geoip.assign(src.geoip().begin(), src.geoip().end());
  dst.m_trusted_ips.assign(src.trusted_ips().begin(), src.trusted_ips().end());
  if (src.has_host()) {
    dst.m_host = src.host().value();
  }
  if (src.has_hostname()) {
    dst.m_hostname = src.hostname().value();
  }
  if (src.has_port()) {
    dst.m_port = src.port().value();
  }
  if (src.has_tls_mode()) {
    dst.m_tls_mode = src.tls_mode().value();
  }
  dst.m_tls_alpns.assign(src.tls_alpns().begin(), src.tls_alpns().end());
  if (src.has_tls_min_version()) {
    dst.m_tls_min_version = src.tls_min_version().value();
  }
  dst.m_tls_ciphers.assign(src.tls_ciphers().begin(), src.tls_ciphers().end());
  if (src.has_mtls_auth()) {
    dst.m_mtls_auth = src.mtls_auth().value();
  }
  if (src.has_http_version()) {
    dst.m_http_version = src.http_version().value();
  }
  if (src.has_http_use_tls()) {
    dst.m_http_use_tls = src.http_use_tls().value();
  }
  if (src.has_upstream_tls()) {
    dst.m_upstream_tls = src.upstream_tls().value();
  }
  if (src.has_token_auth()) {
    dst.m_token_auth = src.token_auth().value();
  }
  if (src.has_rstream_auth()) {
    dst.m_rstream_auth = src.rstream_auth().value();
  }
  if (src.has_challenge_mode()) {
    dst.m_challenge_mode = src.challenge_mode().value();
  }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

}  // namespace detail
}  // namespace io_rstrm
}  // namespace rstream
