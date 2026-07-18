// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/url.hpp>

#include <nlohmann/json.hpp>

#include <rstream/io/address.hpp>

namespace rstream {
namespace io_rstrm {

using labels = std::map<std::string, std::string>;

namespace protocol {
const std::string tls  = "tls";
const std::string tcp  = "tcp";
const std::string http = "http";
}  // namespace protocol

namespace tls_mode {
const std::string terminated  = "terminated";
const std::string passthrough = "passthrough";
}  // namespace tls_mode

struct client_details {
  boost::optional<std::string> m_agent;
  boost::optional<std::string> m_channel;
  boost::optional<std::string> m_os;
  boost::optional<std::string> m_arch;
  boost::optional<std::string> m_version;
  boost::optional<std::string> m_token;
  boost::optional<std::string> m_protocol_version;
};

struct status {
  boost::optional<std::string> m_update;
  boost::optional<std::string> m_plan;
  boost::optional<std::string> m_provider;
  boost::optional<std::string> m_region;
};

std::ostream& operator<<(std::ostream& os, const status& status);

nlohmann::json& operator<<(nlohmann::json& json, const status& status);

struct status_extd : status {
  boost::optional<std::string> m_status;
  boost::optional<std::string> m_tunnel_id;
  boost::optional<std::string> m_forwarding;
};

std::ostream& operator<<(std::ostream& os, const status_extd& status);

nlohmann::json& operator<<(nlohmann::json& json, const status_extd& status);

struct tunnel_properties {
  // tunnel properties
  boost::optional<std::string> m_id;
  boost::optional<std::string> m_name;
  boost::optional<std::chrono::system_clock::time_point> m_creation_date;
  // tunnel options
  boost::optional<std::string> m_type;
  boost::optional<bool> m_publish;
  boost::optional<std::string> m_protocol;
  labels m_labels;
  // security options
  std::vector<std::string> m_geoip;
  std::vector<std::string> m_trusted_ips;
  // publishing options
  boost::optional<std::string> m_host;
  boost::optional<std::string> m_hostname;
  boost::optional<std::uint32_t> m_port;
  // tls options
  boost::optional<std::string> m_tls_mode;
  std::vector<std::string> m_tls_alpns;
  // publishing options (terminated tunnels only)
  boost::optional<std::string> m_tls_min_version;
  std::vector<std::string> m_tls_ciphers;
  boost::optional<bool> m_mtls_auth;
  // http tunnel options (http tunnels only)
  boost::optional<std::string> m_http_version;
  boost::optional<bool> m_http_use_tls;
  boost::optional<bool> m_upstream_tls;
  boost::optional<bool> m_token_auth;
  boost::optional<bool> m_rstream_auth;
  boost::optional<bool> m_challenge_mode;
  boost::optional<bool> m_datagram_guaranteed_delivery;
};

struct config {
  config();
  std::size_t m_max_buffer_size;
  bool m_zero_rtt;
  bool m_no_token;
  boost::optional<std::string> m_token;
  bool m_token_from_uri_param;
  boost::optional<std::string> m_config_path;
};

struct config_client : config {
  config_client();
  unsigned int m_connection_timeout_ms;
  bool m_async_stream_operation;
  std::size_t m_max_ongoing_streams;
  bool m_hearbeat;
  unsigned int m_heartbeat_interval_ms;
};

struct settings_socket {
  settings_socket();
  config m_config;
};

struct settings_acceptor {
  settings_acceptor();
  config_client m_config;
  bool m_auto_reconnect;
  unsigned int m_reconnect_timeout_ms;
  bool m_auto_recreate_tunnel;
  unsigned int m_recreate_tunnel_timeout_ms;
  tunnel_properties m_tunnel_properties;
};

void parse_settings_socket(const boost::urls::url& url, settings_socket& settings, boost::system::error_code& error_code);

void parse_settings_acceptor(const boost::urls::url& url, settings_acceptor& settings, boost::system::error_code& error_code);

boost::system::result<boost::filesystem::path> get_home_path();

boost::system::result<boost::filesystem::path> get_rstream_config_path();

boost::system::result<boost::filesystem::path> get_rstream_config_file_path();

boost::system::result<boost::filesystem::path> get_rstream_config_file_path(const boost::optional<std::string>& config_path);

boost::system::result<nlohmann::json> get_rstream_config_file();

boost::system::result<nlohmann::json> get_rstream_config_file(const boost::optional<std::string>& config_path);

boost::system::result<boost::optional<std::string>> get_rstream_token(const config& config, const io::address& server_address);

boost::system::result<client_details> get_client_details(const boost::optional<std::string> token);

boost::system::result<client_details> get_client_details(const config& config, const io::address& server_address);

boost::system::result<std::string> get_rstream_engine_address();

boost::system::result<std::string> get_rstream_engine_address(const boost::optional<std::string>& config_path);

boost::system::result<std::string> format_forwarding_address(const tunnel_properties& properties);

boost::system::result<std::string> format_forwarded_address(const io::address& forwarded_address, const tunnel_properties& properties);

}  // namespace io_rstrm
}  // namespace rstream
