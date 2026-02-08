// See LICENSE file in the project root for license information.

#include "io-rstrm.hpp"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/system.hpp>
#include <rstream/core/version.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/io/detail/stream/url.hpp>

#include "error.hpp"

static const rstream::core::logger g_logger({"rstream", "io-rstrm", "common"});

static std::vector<std::string> split(const std::string& str, char delimiter)
{
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream token_stream(str);
  while (std::getline(token_stream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

#define PARSE_PARAMS_VIEW_BOOLEAN(src, dst, prefix, error_code, name)               \
  {                                                                                 \
    if (!error_code) {                                                              \
      auto it = src.find(std::string(prefix) + #name);                              \
      if (it != src.end()) {                                                        \
        bool value;                                                                 \
        rstream::io::detail::stream::parse_url_param_value(value, *it, error_code); \
        dst.m_##name = value;                                                       \
      }                                                                             \
    }                                                                               \
  }

#define PARSE_PARAMS_VIEW_STRING(src, dst, prefix, error_code, name)                       \
  {                                                                                        \
    if (!error_code) {                                                                     \
      auto it = src.find(std::string(prefix) + #name);                                     \
      if (it != src.end()) {                                                               \
        rstream::io::detail::stream::parse_url_param_value(dst.m_##name, *it, error_code); \
      }                                                                                    \
    }                                                                                      \
  }

#define PARSE_PARAMS_VIEW_STRING_VEC(src, dst, prefix, error_code, name, delimiter) \
  {                                                                                 \
    if (!error_code) {                                                              \
      auto it = src.find(std::string(prefix) + #name);                              \
      if (it != src.end()) {                                                        \
        boost::optional<std::string> value;                                         \
        rstream::io::detail::stream::parse_url_param_value(value, *it, error_code); \
        if (!error_code) {                                                          \
          dst.m_##name = split(value.value(), delimiter);                           \
        }                                                                           \
      }                                                                             \
    }                                                                               \
  }

#define PARSE_PARAMS_VIEW_ULONG(src, dst, prefix, error_code, name)                        \
  {                                                                                        \
    if (!error_code) {                                                                     \
      auto it = src.find(std::string(prefix) + #name);                                     \
      if (it != src.end()) {                                                               \
        rstream::io::detail::stream::parse_url_param_value(dst.m_##name, *it, error_code); \
      }                                                                                    \
    }                                                                                      \
  }

#define PARSE_PARAMS_VIEW_STRING_MAP(src, dst, prefix, error_code, name, delimiter)       \
  {                                                                                       \
    if (!error_code) {                                                                    \
      for (auto it = src.begin(); it != src.end(); ++it) {                                \
        if ((*it).key == std::string(prefix) + #name) {                                   \
          boost::optional<std::string> label;                                             \
          rstream::io::detail::stream::parse_url_param_value(label, *it, error_code);     \
          if (!error_code) {                                                              \
            auto pos = label.value().find(delimiter);                                     \
            if (pos != std::string::npos) {                                               \
              dst.m_##name[label.value().substr(0, pos)] = label.value().substr(pos + 1); \
            }                                                                             \
            else {                                                                        \
              g_logger->trace("ignoring invalid label '{}'", label.value());              \
            }                                                                             \
          }                                                                               \
        }                                                                                 \
      }                                                                                   \
    }                                                                                     \
  }

static const bool g_is_debug_build =
#ifdef DEBUG_BUILD
    true;
#else
    false;
#endif

namespace rstream {
namespace io_rstrm {

static void parse_tunnel_properties(const boost::urls::url& url, tunnel_properties& properties, boost::system::error_code& error_code);

static void parse_config(const boost::urls::url& url, config& config, boost::system::error_code& error_code);

static void parse_config_client(const boost::urls::url& url, config_client& config, boost::system::error_code& error_code);

std::ostream& operator<<(std::ostream& os, const status& status)
{
  os << "  version    : " << status.m_version.value_or("-");
  os << std::endl
     << "  update     : " << status.m_update.value_or("-");
  os << std::endl
     << "  plan       : " << status.m_plan.value_or("-");
  os << std::endl
     << "  region     : " << status.m_region.value_or("-");
  return os;
}

nlohmann::json& operator<<(nlohmann::json& json, const status& status)
{
  if (status.m_version) {
    json["version"] = status.m_version.value();
  }
  if (status.m_update) {
    json["update"] = status.m_update.value();
  }
  if (status.m_plan) {
    json["plan"] = status.m_plan.value();
  }
  if (status.m_region) {
    json["region"] = status.m_region.value();
  }
  return json;
}

std::ostream& operator<<(std::ostream& os, const status_extd& status)
{
  os << static_cast<struct status>(status);
  os << std::endl
     << "  status     : " << status.m_status.value_or("-");
  os << std::endl
     << "  tunnel ID  : " << status.m_tunnel_id.value_or("-");
  os << std::endl
     << "  forwarding : " << status.m_forwarding.value_or("-");
  return os;
}

nlohmann::json& operator<<(nlohmann::json& json, const status_extd& status)
{
  json << static_cast<struct status>(status);
  if (status.m_status) {
    json["status"] = status.m_status.value();
  }
  if (status.m_tunnel_id) {
    json["tunnel_id"] = status.m_tunnel_id.value();
  }
  if (status.m_forwarding) {
    json["forwarding"] = status.m_forwarding.value();
  }
  return json;
}

config::config()
    : m_max_buffer_size(UINT16_MAX),
      m_zero_rtt(!g_is_debug_build),
      m_no_token(false)
{
}

config_client::config_client()
    : config(),
      m_connection_timeout_ms(30000),
      m_async_stream_operation(true),
      m_max_ongoing_streams(50),
      m_hearbeat(true),
      m_heartbeat_interval_ms(10000)
{
}

settings_socket::settings_socket()
{
}

settings_acceptor::settings_acceptor()
    : m_auto_reconnect(true),
      m_reconnect_timeout_ms(5000),
      m_auto_recreate_tunnel(true),
      m_recreate_tunnel_timeout_ms(5000)
{
}

void parse_tunnel_properties(const boost::urls::url& url, tunnel_properties& properties, boost::system::error_code& error_code)
{
  // PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, type) // TODO
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, publish)
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, protocol)
  PARSE_PARAMS_VIEW_STRING_MAP(url.params(), properties, "rstrm.", error_code, labels, '=')
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, geoip, ',')
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, trusted_ips, ',')
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, host)
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, tls_min_version)
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, tls_ciphers, ',')
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, mtls)
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, mtls_cacert_pem)
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, http_version)
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, http_use_tls)
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, token_auth)
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, sso)
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, sso_providers, ',')
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, email_whitelist, ',')
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, email_blacklist, ',')
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, challenge)
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, tls_mode)
  PARSE_PARAMS_VIEW_STRING_VEC(url.params(), properties, "rstrm.", error_code, tls_alpns, ',')
}

void parse_config(const boost::urls::url& url, config& config, boost::system::error_code& error_code)
{
  if (!error_code) {
    auto it = url.params().find("rstream.no_token");
    if (it != url.params().end()) {
      rstream::io::detail::stream::parse_url_param_value(config.m_no_token, *it, error_code);
    }
  }
  if (!error_code) {
    auto it = url.params().find("rstream.token");
    if (it != url.params().end()) {
      if (config.m_no_token) {
#ifdef DEBUG_BUILD
        g_logger->warn("cannot set token when 'no_token' option is set");
#endif
        error_code = error::code::invalid_configuration;
      }
      else {
        rstream::io::detail::stream::parse_url_param_value(config.m_token, *it, error_code);
      }
    }
  }
}

void parse_config_client(const boost::urls::url& url, config_client& config, boost::system::error_code& error_code)
{
  if (!error_code) {
    parse_config(url, config, error_code);
  }
}

void parse_settings_socket(const boost::urls::url& url, settings_socket& settings, boost::system::error_code& error_code)
{
  if (!error_code) {
    parse_config(url, settings.m_config, error_code);
  }
}

void parse_settings_acceptor(const boost::urls::url& url, settings_acceptor& settings, boost::system::error_code& error_code)
{
  if (!error_code) {
    parse_config_client(url, settings.m_config, error_code);
  }
  if (!error_code) {
    auto it = url.params().find("rstream.retry");
    if (it != url.params().end()) {
      rstream::io::detail::stream::parse_url_param_value(settings.m_auto_reconnect, *it, error_code);
    }
  }
  if (!error_code) {
    parse_tunnel_properties(url, settings.m_tunnel_properties, error_code);
  }
}

boost::system::result<boost::filesystem::path> get_home_path()
{
  char const* home = std::getenv("HOME");
  if (home == nullptr) {
    home = std::getenv("USERPROFILE");
  }
  if (home) {
    return boost::filesystem::path(home);
  }
  else {
#ifdef DEBUG_BUILD
    g_logger->warn("could not find home directory");
#endif
    return boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory);
  }
}

boost::system::result<boost::filesystem::path> get_rstream_config_path()
{
  const char* env_path = std::getenv("RSTREAM_DEFAULT_CONFIG_PATH");
  if (env_path) {
    return env_path;
  }
  else {
    auto home = get_home_path();
    if (home) {
      return home.value() / ".rstream";
    }
    else {
      return home.error();
    }
  }
}

boost::system::result<boost::filesystem::path> get_rstream_config_file_path()
{
  auto config_path = get_rstream_config_path();
  if (config_path) {
    return config_path.value() / "config.json";
  }
  else {
    return config_path.error();
  }
}

boost::system::result<nlohmann::json> get_rstream_config_file()
{
  auto config_file_path = get_rstream_config_file_path();
  if (config_file_path) {
    std::ifstream file(config_file_path.value().string());
    if (file.is_open()) {
      nlohmann::json json;
      try {
        file >> json;
      }
      catch (...) {
#ifdef DEBUG_BUILD
        g_logger->warn("could not parse rstream config file '{}': {}", config_file_path.value().string(), rstream::core::throwable::message(std::current_exception()));
#endif
        return boost::system::errc::make_error_code(boost::system::errc::io_error);
      }
      return json;
    }
    else {
#ifdef DEBUG_BUILD
      g_logger->trace("could not find rstream config file '{}'", config_file_path.value().string());
#endif
      return boost::system::error_code();
    }
  }
  else {
    return config_file_path.error();
  }
}

boost::system::result<boost::optional<std::string>> get_rstream_token(const config& config, const io::address& server_address)
{
  auto host = server_address.m_url.host();
  if (config.m_no_token) {
    return boost::none;
  }
  else if (config.m_token) {
#ifdef DEBUG_BUILD
    g_logger->trace("using rstream token from url parameter for host '{}'", host);
#endif
    return config.m_token;
  }
  {
    char const* token = std::getenv("RSTREAM_DEFAULT_AUTHENTICATION_TOKEN");
    if (token) {
#ifdef DEBUG_BUILD
      g_logger->trace("using rstream token from environment variable for host '{}'", host);
#endif
      return std::string(token);
    }
  }
  auto config_file = get_rstream_config_file();
  if (config_file) {
    try {
      const auto& tokens = config_file.value().at("tokens");
      if (tokens.contains(host)) {
        auto token = tokens[host].get<std::string>();
#ifdef DEBUG_BUILD
        g_logger->trace("using rstream token from config file for host '{}'", host);
#endif
        return token;
      }
      // attempt to match subdomains by trimming from the leftmost subdomain iteratively
      std::string domain = host;
      size_t pos         = domain.find('.');
      while (pos != std::string::npos && pos + 1 < domain.size()) {
        domain = domain.substr(pos + 1);
        if (tokens.contains(domain)) {
          auto token = tokens[domain].get<std::string>();
#ifdef DEBUG_BUILD
          g_logger->trace("using rstream token from config file for host '{}'", host);
#endif
          return token;
        }
        pos = domain.find('.');
      }
#ifdef DEBUG_BUILD
      g_logger->trace("could not find rstream token for host '{}'", host);
#endif
      return boost::optional<std::string>();
    }
    catch (...) {
#ifdef DEBUG_BUILD
      g_logger->warn("could not find rstream token for host '{}': {}", host, rstream::core::throwable::message(std::current_exception()));
#endif
      return boost::system::errc::make_error_code(boost::system::errc::io_error);
    }
  }
  else if (config_file.error()) {
    return config_file.error();
  }
  else {
    return boost::optional<std::string>();
  }
}

boost::system::result<client_details> get_client_details(const boost::optional<std::string> token)
{
  auto system_info              = core::get_system_info();
  auto protobuf_file_descriptor = protobuf::ClientDetails::descriptor()->file();
  boost::optional<std::string> protocol_version;
  if (protobuf_file_descriptor->options().HasExtension(protobuf::protocol_version)) {
    protocol_version = protobuf_file_descriptor->options().GetExtension(protobuf::protocol_version);
  }
  return (client_details){
      .m_agent            = std::string("stream-cpp-sdk"),
      .m_os               = system_info.m_sysname + " (" + system_info.m_release + ")",
      .m_arch             = system_info.m_machine,
      .m_version          = std::string(RSTREAM_VERSION),
      .m_token            = token ? boost::optional<std::string>(token.value()) : boost::none,
      .m_protocol_version = protocol_version,
  };
}

boost::system::result<client_details> get_client_details(const config& config, const io::address& server_address)
{
  auto token = get_rstream_token(config, server_address);
  if (token) {
    return get_client_details(token.value());
  }
  else {
    return token.error();
  }
}

boost::system::result<std::string> get_rstream_engine_address()
{
  std::string engine_address;
  const char* default_engine_address = std::getenv("RSTREAM_DEFAULT_ENGINE_ADDRESS");
  if (default_engine_address) {
    engine_address = default_engine_address;
  }
  else {
    engine_address = "tcp://engine.rstream.io:443";
  }
  const char* default_engine_params = std::getenv("RSTREAM_DEFAULT_ENGINE_PARAMS");
  if (default_engine_address) {
    if (default_engine_params) {
#ifdef DEBUG_BUILD
      g_logger->warn("ignoring default engine parameters when default engine address is set");
#endif
    }
  }
  else {
    std::string engine_params;
    if (default_engine_params) {
      engine_params = default_engine_params;
    }
    else {
      engine_params = "ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1";
    }
    engine_address += "?" + engine_params;
  }
  return engine_address;
}

boost::system::result<std::string> format_forwarding_address(const tunnel_properties& properties)
{
  if (properties.m_host) {
    std::string res;
    std::string protocol;
    if (properties.m_protocol && properties.m_protocol.value() == "http") {
      protocol = "https";
      res      = protocol + "://";
    }
    else {
      protocol = "tls";
    }
    res += properties.m_host.value();
    if (protocol != "https") {
      res += " (" + protocol + ")";
    }
    return res;
  }
  else {
    if (properties.m_name) {
      return "rstrm://" + properties.m_name.value() + " (unpublished)";
    }
    else if (properties.m_id) {
      return "rstrm://" + properties.m_id.value() + " (unpublished)";
    }
    else {
      return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
    }
  }
}

boost::system::result<std::string> format_forwarded_address(const io::address& forwarded_address, const tunnel_properties& properties)
{
  if (forwarded_address.m_str) {
    if (forwarded_address.m_str->find("://") != std::string::npos) {
      return forwarded_address.m_str.value() + " (unparsed)";
    }
  }
  if (forwarded_address.host().empty()) {
    return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
  }
  if (forwarded_address.port().empty()) {
    return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
  }
  std::string res;
  std::string protocol;
  if (properties.m_protocol && properties.m_protocol.value() == "http") {
    if (properties.m_http_use_tls && properties.m_http_use_tls.value()) {
      protocol = "https";
    }
    else {
      protocol = "http";
    }
    res = protocol + "://";
  }
  res += forwarded_address.host();
  if (!(protocol == "http" && forwarded_address.port() == "80")
      && !(protocol == "https" && forwarded_address.port() == "443")) {
    res += ":" + forwarded_address.port();
  }
  if (protocol == "http") {
    if (properties.m_http_version) {
      res += " (" + properties.m_http_version.value() + ")";
    }
  }
  else if (protocol != "https") {
    if (properties.m_tls_mode && properties.m_tls_mode.value() == "passthrough") {
      res += " (tls)";
    }
    else {
      res += " (tcp)";
    }
  }
  return res;
}

}  // namespace io_rstrm
}  // namespace rstream
