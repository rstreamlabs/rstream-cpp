// See LICENSE file in the project root for license information.

#include "io-rstrm.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>
#include <openssl/opensslv.h>
#include <yaml-cpp/yaml.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/system.hpp>
#include <rstream/core/version.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/io/detail/stream/url.hpp>

#include "error.hpp"

static const rstream::core::logger g_logger({"rstream", "io-rstrm", "common"});

static std::string default_tls_groups_query()
{
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
  return "&ssl.groups=SecP256r1MLKEM768%3ASecP384r1MLKEM1024%3AX25519MLKEM768%3AX25519%3Asecp256r1%3Asecp384r1";
#else
  return "&ssl.groups=X25519%3Asecp256r1%3Asecp384r1";
#endif
}

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

static bool has_client_certificate_config(const rstream::io::address& server_address)
{
  for (const auto param : rstream::io::detail::stream::url_params(server_address.m_url)) {
    if (param.key == "ssl.cert" || param.key == "ssl.cert_file") {
      return true;
    }
  }
  return false;
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
  os << "  update     : " << status.m_update.value_or("-");
  os << std::endl
     << "  plan       : " << status.m_plan.value_or("-");
  os << std::endl
     << "  provider   : " << status.m_provider.value_or("-");
  os << std::endl
     << "  region     : " << status.m_region.value_or("-");
  return os;
}

nlohmann::json& operator<<(nlohmann::json& json, const status& status)
{
  if (status.m_update) {
    json["update"] = status.m_update.value();
  }
  if (status.m_plan) {
    json["plan"] = status.m_plan.value();
  }
  if (status.m_provider) {
    json["provider"] = status.m_provider.value();
  }
  if (status.m_region) {
    json["region"] = status.m_region.value();
  }
  return json;
}

std::ostream& operator<<(std::ostream& os, const status_extd& status)
{
  os << "  version    : " << RSTREAM_VERSION;
  os << std::endl
     << "  update     : " << status.m_update.value_or("-");
  os << std::endl
     << "  status     : " << status.m_status.value_or("-");
  os << std::endl
     << "  plan       : " << status.m_plan.value_or("-");
  os << std::endl
     << "  provider   : " << status.m_provider.value_or("-");
  os << std::endl
     << "  region     : " << status.m_region.value_or("-");
  os << std::endl
     << "  tunnel ID  : " << status.m_tunnel_id.value_or("-");
  os << std::endl
     << "  forwarding : " << status.m_forwarding.value_or("-");
  return os;
}

nlohmann::json& operator<<(nlohmann::json& json, const status_extd& status)
{
  json["version"] = RSTREAM_VERSION;
  if (status.m_update) {
    json["update"] = status.m_update.value();
  }
  if (status.m_status) {
    json["status"] = status.m_status.value();
  }
  if (status.m_plan) {
    json["plan"] = status.m_plan.value();
  }
  if (status.m_provider) {
    json["provider"] = status.m_provider.value();
  }
  if (status.m_region) {
    json["region"] = status.m_region.value();
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
      m_no_token(false),
      m_token_from_uri_param(false)
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
  const auto params = rstream::io::detail::stream::url_params(url);
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, type)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, publish)
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, protocol)
  PARSE_PARAMS_VIEW_STRING_MAP(params, properties, "rstrm.", error_code, labels, '=')
  PARSE_PARAMS_VIEW_STRING_VEC(params, properties, "rstrm.", error_code, geoip, ',')
  PARSE_PARAMS_VIEW_STRING_VEC(params, properties, "rstrm.", error_code, trusted_ips, ',')
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, host)
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, hostname)
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, tls_min_version)
  PARSE_PARAMS_VIEW_STRING_VEC(params, properties, "rstrm.", error_code, tls_ciphers, ',')
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, mtls_auth)
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, http_version)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, http_use_tls)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, upstream_tls)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, token_auth)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, rstream_auth)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, challenge_mode)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, datagram_guaranteed_delivery)
  PARSE_PARAMS_VIEW_BOOLEAN(params, properties, "rstrm.", error_code, allow_cross_region_routing)
  PARSE_PARAMS_VIEW_STRING(params, properties, "rstrm.", error_code, tls_mode)
  PARSE_PARAMS_VIEW_STRING_VEC(params, properties, "rstrm.", error_code, tls_alpns, ',')
}

void parse_config(const boost::urls::url& url, config& config, boost::system::error_code& error_code)
{
  const auto params = rstream::io::detail::stream::url_params(url);
  if (!error_code) {
    auto it = params.find("rstream.no_token");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(config.m_no_token, *it, error_code);
    }
  }
  if (!error_code) {
    auto it = params.find("rstream.token");
    if (it != params.end()) {
      if (config.m_no_token) {
#ifdef DEBUG_BUILD
        g_logger->warn("cannot set token when 'no_token' option is set");
#endif
        error_code = error::code::invalid_configuration;
      }
      else {
        rstream::io::detail::stream::parse_url_param_value(config.m_token, *it, error_code);
        config.m_token_from_uri_param = !error_code;
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
    const auto params = rstream::io::detail::stream::url_params(url);
    auto it           = params.find("rstream.retry");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(settings.m_auto_reconnect, *it, error_code);
    }
  }
  if (!error_code) {
    parse_tunnel_properties(url, settings.m_tunnel_properties, error_code);
  }
}

struct config_token {
  config_token()
      : present(false)
  {
  }
  bool present;
  std::string kind;
  std::string value;
};

struct config_mtls_storage {
  config_mtls_storage()
      : present(false),
        max_sessions(0)
  {
  }
  bool present;
  std::string kind;
  std::string provider;
  std::string module;
  std::string openssl_provider;
  std::string token_label;
  std::string token_serial;
  boost::optional<int> slot;
  std::string key_label;
  std::string key_id_hex;
  std::string certificate;
  std::string certificate_file;
  std::string certificate_label;
  std::string certificate_id_hex;
  std::string certificate_sha256;
  std::string pin_env;
  int max_sessions;
};

struct config_mtls {
  config_mtls()
      : present(false)
  {
  }
  bool present;
  std::string certificate;
  std::string certificate_file;
  std::string key;
  std::string key_file;
  config_mtls_storage storage;
};

struct config_auth {
  config_token token;
  config_mtls mtls;
};

struct config_tls {
  bool present                  = false;
  bool insecure_skip_verify_set = false;
  bool insecure_skip_verify     = false;
  std::string ca_file;
  std::string server_name;
};

struct config_proxy {
  bool present                  = false;
  bool from_environment_present = false;
  bool from_environment         = false;
  std::string http;
  std::string socks5;
  std::string username;
  std::string password;
  std::map<std::string, std::string> headers;
  config_tls tls;
};

struct config_transport {
  bool mode_present = false;
  std::string mode;
  bool use_quic_present = false;
  bool use_quic_valid   = true;
  bool use_quic         = false;
  config_proxy proxy;
  config_tls tls;
};

struct config_environment {
  std::string api_url;
  config_auth auth;
  config_transport transport;
};

struct config_context {
  std::string name;
  std::string api_url;
  std::string engine;
  config_auth auth;
  config_transport transport;
};

struct config_file {
  std::string default_context;
  std::vector<config_environment> environments;
  std::vector<config_context> contexts;
};

static std::string ltrim(const std::string& str)
{
  size_t start = 0;
  while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
    start++;
  }
  return str.substr(start);
}

static std::string rtrim(const std::string& str)
{
  size_t end = str.size();
  while (end > 0 && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
    end--;
  }
  return str.substr(0, end);
}

static std::string trim(const std::string& str)
{
  return rtrim(ltrim(str));
}

static std::string yaml_string(const YAML::Node& node)
{
  if (!node || !node.IsScalar()) {
    return "";
  }
  return trim(node.as<std::string>());
}

static boost::optional<int> yaml_int(const YAML::Node& node)
{
  if (!node || !node.IsScalar()) {
    return boost::none;
  }
  try {
    return node.as<int>();
  }
  catch (const YAML::Exception&) {
    return boost::none;
  }
}

static boost::optional<bool> yaml_bool(const YAML::Node& node)
{
  if (!node || !node.IsScalar()) {
    return boost::none;
  }
  try {
    return node.as<bool>();
  }
  catch (const YAML::Exception&) {
    return boost::none;
  }
}

static void parse_auth_token_storage(const YAML::Node& node, config_auth& auth)
{
  if (!node || !node.IsMap()) {
    return;
  }
  auth.token.present = true;
  std::string kind   = yaml_string(node["kind"]);
  if (!kind.empty()) {
    auth.token.kind = kind;
  }
  std::string value = yaml_string(node["value"]);
  if (!value.empty()) {
    auth.token.value = value;
  }
}

static void parse_auth_mtls_storage(const YAML::Node& node, config_mtls_storage& storage)
{
  if (!node || !node.IsMap()) {
    return;
  }
  storage.present            = true;
  storage.kind               = yaml_string(node["kind"]);
  storage.provider           = yaml_string(node["provider"]);
  storage.module             = yaml_string(node["module"]);
  storage.openssl_provider   = yaml_string(node["opensslProvider"]);
  storage.token_label        = yaml_string(node["tokenLabel"]);
  storage.token_serial       = yaml_string(node["tokenSerial"]);
  storage.slot               = yaml_int(node["slot"]);
  storage.key_label          = yaml_string(node["keyLabel"]);
  storage.key_id_hex         = yaml_string(node["keyIdHex"]);
  storage.certificate        = yaml_string(node["certificate"]);
  storage.certificate_file   = yaml_string(node["certificateFile"]);
  storage.certificate_label  = yaml_string(node["certificateLabel"]);
  storage.certificate_id_hex = yaml_string(node["certificateIdHex"]);
  storage.certificate_sha256 = yaml_string(node["certificateSHA256"]);
  storage.pin_env            = yaml_string(node["pinEnv"]);
  if (auto max_sessions = yaml_int(node["maxSessions"])) {
    storage.max_sessions = max_sessions.get();
  }
}

static void parse_auth_mtls(const YAML::Node& node, config_auth& auth)
{
  if (!node || !node.IsMap()) {
    return;
  }
  std::string certificate      = yaml_string(node["certificate"]);
  std::string certificate_file = yaml_string(node["certificateFile"]);
  std::string key              = yaml_string(node["key"]);
  std::string key_file         = yaml_string(node["keyFile"]);
  config_mtls_storage storage;
  parse_auth_mtls_storage(node["storage"], storage);
  if (certificate.empty() && certificate_file.empty() && key.empty() && key_file.empty() && !storage.present) {
    return;
  }
  auth.mtls.present          = true;
  auth.mtls.certificate      = certificate;
  auth.mtls.certificate_file = certificate_file;
  auth.mtls.key              = key;
  auth.mtls.key_file         = key_file;
  auth.mtls.storage          = storage;
}

static void parse_auth(const YAML::Node& node, config_auth& auth)
{
  if (!node || !node.IsMap()) {
    return;
  }
  YAML::Node auth_node = node["auth"];
  if (!auth_node || !auth_node.IsMap()) {
    return;
  }
  YAML::Node token = auth_node["token"];
  if (token && token.IsMap()) {
    YAML::Node storage = token["storage"];
    if (storage && storage.IsMap()) {
      parse_auth_token_storage(storage, auth);
    }
  }
  parse_auth_mtls(auth_node["mtls"], auth);
}

static void parse_transport_tls(const YAML::Node& node, config_tls& tls_config)
{
  if (!node || !node.IsMap()) {
    return;
  }
  tls_config.present     = true;
  tls_config.ca_file     = yaml_string(node["caFile"]);
  tls_config.server_name = yaml_string(node["serverName"]);
  if (auto insecure_skip_verify = yaml_bool(node["insecureSkipVerify"])) {
    tls_config.insecure_skip_verify_set = true;
    tls_config.insecure_skip_verify     = insecure_skip_verify.get();
  }
}

static void parse_transport_proxy(const YAML::Node& node, config_transport& transport)
{
  if (!node || !node.IsMap()) {
    return;
  }
  transport.proxy.present  = true;
  transport.proxy.http     = yaml_string(node["http"]);
  transport.proxy.socks5   = yaml_string(node["socks5"]);
  transport.proxy.username = yaml_string(node["username"]);
  transport.proxy.password = yaml_string(node["password"]);
  if (auto from_environment = yaml_bool(node["fromEnvironment"])) {
    transport.proxy.from_environment_present = true;
    transport.proxy.from_environment         = from_environment.get();
  }
  parse_transport_tls(node["tls"], transport.proxy.tls);
  YAML::Node headers = node["headers"];
  if (headers && headers.IsMap()) {
    for (const auto& entry : headers) {
      std::string key   = yaml_string(entry.first);
      std::string value = yaml_string(entry.second);
      if (!key.empty() && !value.empty()) {
        transport.proxy.headers[key] = value;
      }
    }
  }
}

static void parse_transport(const YAML::Node& node, config_transport& transport)
{
  if (!node || !node.IsMap()) {
    return;
  }
  if (node["mode"]) {
    transport.mode_present = true;
    transport.mode         = yaml_string(node["mode"]);
  }
  if (node["useQuic"]) {
    transport.use_quic_present = true;
    if (auto use_quic = yaml_bool(node["useQuic"])) {
      transport.use_quic = use_quic.get();
    }
    else {
      transport.use_quic_valid = false;
    }
  }
  parse_transport_tls(node["tls"], transport.tls);
  parse_transport_proxy(node["proxy"], transport);
}

static bool parse_config_yaml(const std::string& content, config_file& cfg)
{
  if (trim(content).empty()) {
    return true;
  }
  YAML::Node root;
  try {
    root = YAML::Load(content);
  }
  catch (const YAML::Exception&) {
    return false;
  }
  if (!root || !root.IsMap()) {
    return true;
  }
  YAML::Node defaults = root["defaults"];
  if (defaults && defaults.IsMap()) {
    YAML::Node ctx = defaults["context"];
    if (ctx && ctx.IsMap()) {
      cfg.default_context = yaml_string(ctx["name"]);
    }
  }
  YAML::Node environments = root["environments"];
  if (environments && environments.IsSequence()) {
    for (const auto& env_node : environments) {
      if (!env_node.IsMap()) {
        continue;
      }
      config_environment env;
      env.api_url = yaml_string(env_node["apiUrl"]);
      parse_auth(env_node, env.auth);
      parse_transport(env_node["transport"], env.transport);
      cfg.environments.push_back(env);
    }
  }
  YAML::Node contexts = root["contexts"];
  if (contexts && contexts.IsSequence()) {
    for (const auto& ctx_node : contexts) {
      if (!ctx_node.IsMap()) {
        continue;
      }
      config_context ctx;
      ctx.name    = yaml_string(ctx_node["name"]);
      ctx.api_url = yaml_string(ctx_node["apiUrl"]);
      ctx.engine  = yaml_string(ctx_node["engine"]);
      parse_auth(ctx_node, ctx.auth);
      parse_transport(ctx_node["transport"], ctx.transport);
      cfg.contexts.push_back(ctx);
    }
  }
  return true;
}

static std::string getenv_trim(const char* key)
{
  const char* value = std::getenv(key);
  if (!value) {
    return "";
  }
  return trim(value);
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
  const char* env_path = std::getenv("RSTREAM_CONFIG");
  if (env_path && env_path[0] != '\0') {
    return boost::filesystem::path(env_path).parent_path();
  }
  auto home = get_home_path();
  if (home) {
    return home.value() / ".rstream";
  }
  return home.error();
}

boost::system::result<boost::filesystem::path> get_rstream_config_file_path()
{
  return get_rstream_config_file_path(boost::none);
}

boost::system::result<boost::filesystem::path> get_rstream_config_file_path(const boost::optional<std::string>& config_path)
{
  if (config_path && !config_path.value().empty()) {
    return boost::filesystem::path(config_path.value());
  }
  const char* env_path = std::getenv("RSTREAM_CONFIG");
  if (env_path && env_path[0] != '\0') {
    return boost::filesystem::path(env_path);
  }
  auto config_path_default = get_rstream_config_path();
  if (config_path_default) {
    return config_path_default.value() / "config.yaml";
  }
  return config_path_default.error();
}

static boost::system::result<config_file> load_rstream_config(const boost::optional<std::string>& config_path)
{
  auto config_file_path = get_rstream_config_file_path(config_path);
  if (!config_file_path) {
    return config_file_path.error();
  }
  std::ifstream file(config_file_path.value().string());
  if (!file.is_open()) {
#ifdef DEBUG_BUILD
    g_logger->trace("could not find rstream config file '{}'", config_file_path.value().string());
#endif
    return config_file();
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  config_file cfg;
  if (!parse_config_yaml(buffer.str(), cfg)) {
#ifdef DEBUG_BUILD
    g_logger->warn("could not parse rstream config file '{}'", config_file_path.value().string());
#endif
    return boost::system::errc::make_error_code(boost::system::errc::io_error);
  }
  return cfg;
}

static void append_auth_json(nlohmann::json& json, const config_auth& auth)
{
  if (auth.token.present) {
    json["auth"]["token"]["storage"]["kind"]  = auth.token.kind;
    json["auth"]["token"]["storage"]["value"] = auth.token.value;
  }
  if (auth.mtls.present) {
    if (!auth.mtls.certificate.empty()) {
      json["auth"]["mtls"]["certificate"] = auth.mtls.certificate;
    }
    if (!auth.mtls.certificate_file.empty()) {
      json["auth"]["mtls"]["certificateFile"] = auth.mtls.certificate_file;
    }
    if (!auth.mtls.key.empty()) {
      json["auth"]["mtls"]["key"] = auth.mtls.key;
    }
    if (!auth.mtls.key_file.empty()) {
      json["auth"]["mtls"]["keyFile"] = auth.mtls.key_file;
    }
    if (auth.mtls.storage.present) {
      json["auth"]["mtls"]["storage"]["kind"] = auth.mtls.storage.kind;
      if (!auth.mtls.storage.provider.empty()) {
        json["auth"]["mtls"]["storage"]["provider"] = auth.mtls.storage.provider;
      }
      if (!auth.mtls.storage.module.empty()) {
        json["auth"]["mtls"]["storage"]["module"] = auth.mtls.storage.module;
      }
      if (!auth.mtls.storage.openssl_provider.empty()) {
        json["auth"]["mtls"]["storage"]["opensslProvider"] = auth.mtls.storage.openssl_provider;
      }
      if (!auth.mtls.storage.token_label.empty()) {
        json["auth"]["mtls"]["storage"]["tokenLabel"] = auth.mtls.storage.token_label;
      }
      if (!auth.mtls.storage.token_serial.empty()) {
        json["auth"]["mtls"]["storage"]["tokenSerial"] = auth.mtls.storage.token_serial;
      }
      if (auth.mtls.storage.slot) {
        json["auth"]["mtls"]["storage"]["slot"] = auth.mtls.storage.slot.get();
      }
      if (!auth.mtls.storage.key_label.empty()) {
        json["auth"]["mtls"]["storage"]["keyLabel"] = auth.mtls.storage.key_label;
      }
      if (!auth.mtls.storage.key_id_hex.empty()) {
        json["auth"]["mtls"]["storage"]["keyIdHex"] = auth.mtls.storage.key_id_hex;
      }
      if (!auth.mtls.storage.certificate.empty()) {
        json["auth"]["mtls"]["storage"]["certificate"] = auth.mtls.storage.certificate;
      }
      if (!auth.mtls.storage.certificate_file.empty()) {
        json["auth"]["mtls"]["storage"]["certificateFile"] = auth.mtls.storage.certificate_file;
      }
      if (!auth.mtls.storage.certificate_label.empty()) {
        json["auth"]["mtls"]["storage"]["certificateLabel"] = auth.mtls.storage.certificate_label;
      }
      if (!auth.mtls.storage.certificate_id_hex.empty()) {
        json["auth"]["mtls"]["storage"]["certificateIdHex"] = auth.mtls.storage.certificate_id_hex;
      }
      if (!auth.mtls.storage.certificate_sha256.empty()) {
        json["auth"]["mtls"]["storage"]["certificateSHA256"] = auth.mtls.storage.certificate_sha256;
      }
      if (!auth.mtls.storage.pin_env.empty()) {
        json["auth"]["mtls"]["storage"]["pinEnv"] = auth.mtls.storage.pin_env;
      }
      if (auth.mtls.storage.max_sessions != 0) {
        json["auth"]["mtls"]["storage"]["maxSessions"] = auth.mtls.storage.max_sessions;
      }
    }
  }
}

static bool transport_proxy_requested(const config_proxy& proxy)
{
  return !proxy.http.empty() || !proxy.socks5.empty() || !proxy.username.empty() || !proxy.password.empty() || proxy.from_environment || !proxy.headers.empty() || proxy.tls.present;
}

static void append_transport_tls_json(nlohmann::json& json, const config_tls& tls_config)
{
  if (!tls_config.present) {
    return;
  }
  if (!tls_config.ca_file.empty()) {
    json["caFile"] = tls_config.ca_file;
  }
  if (!tls_config.server_name.empty()) {
    json["serverName"] = tls_config.server_name;
  }
  if (tls_config.insecure_skip_verify_set) {
    json["insecureSkipVerify"] = tls_config.insecure_skip_verify;
  }
}

static void append_transport_json(nlohmann::json& json, const config_transport& transport)
{
  if (transport.mode_present) {
    json["transport"]["mode"] = transport.mode;
  }
  if (transport.use_quic_present && transport.use_quic_valid) {
    json["transport"]["useQuic"] = transport.use_quic;
  }
  if (transport.tls.present) {
    append_transport_tls_json(json["transport"]["tls"], transport.tls);
  }
  if (!transport.proxy.present) {
    return;
  }
  if (!transport.proxy.http.empty()) {
    json["transport"]["proxy"]["http"] = transport.proxy.http;
  }
  if (!transport.proxy.socks5.empty()) {
    json["transport"]["proxy"]["socks5"] = transport.proxy.socks5;
  }
  if (!transport.proxy.username.empty()) {
    json["transport"]["proxy"]["username"] = transport.proxy.username;
  }
  if (!transport.proxy.password.empty()) {
    json["transport"]["proxy"]["password"] = transport.proxy.password;
  }
  if (transport.proxy.from_environment_present) {
    json["transport"]["proxy"]["fromEnvironment"] = transport.proxy.from_environment;
  }
  for (const auto& header : transport.proxy.headers) {
    json["transport"]["proxy"]["headers"][header.first] = header.second;
  }
  if (transport.proxy.tls.present) {
    append_transport_tls_json(json["transport"]["proxy"]["tls"], transport.proxy.tls);
  }
}

static nlohmann::json config_file_to_json(const config_file& cfg)
{
  nlohmann::json json = nlohmann::json::object();
  if (!cfg.default_context.empty()) {
    json["defaults"]["context"]["name"] = cfg.default_context;
  }
  if (!cfg.environments.empty()) {
    json["environments"] = nlohmann::json::array();
    for (const auto& env : cfg.environments) {
      nlohmann::json env_json = nlohmann::json::object();
      if (!env.api_url.empty()) {
        env_json["apiUrl"] = env.api_url;
      }
      append_auth_json(env_json, env.auth);
      append_transport_json(env_json, env.transport);
      json["environments"].push_back(env_json);
    }
  }
  if (!cfg.contexts.empty()) {
    json["contexts"] = nlohmann::json::array();
    for (const auto& ctx : cfg.contexts) {
      nlohmann::json ctx_json = nlohmann::json::object();
      if (!ctx.name.empty()) {
        ctx_json["name"] = ctx.name;
      }
      if (!ctx.api_url.empty()) {
        ctx_json["apiUrl"] = ctx.api_url;
      }
      if (!ctx.engine.empty()) {
        ctx_json["engine"] = ctx.engine;
      }
      append_auth_json(ctx_json, ctx.auth);
      append_transport_json(ctx_json, ctx.transport);
      json["contexts"].push_back(ctx_json);
    }
  }
  return json;
}

struct resolved_config {
  std::string api_url;
  const config_environment* environment;
  const config_context* context;
};

static boost::system::result<std::string> transport_mode_from_config(const config_transport& transport)
{
  if (transport.mode_present) {
    if (transport.mode.empty()) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    return transport.mode;
  }
  if (transport.use_quic_present) {
    if (!transport.use_quic_valid) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    return transport.use_quic ? "quic" : "tls";
  }
  return std::string();
}

static boost::system::result<std::string> resolve_tunnel_transport_mode(const config_environment* env, const config_context* ctx)
{
  std::string mode = getenv_trim("RSTREAM_TUNNEL_TRANSPORT");
  if (mode.empty()) {
    std::string legacy = getenv_trim("RSTREAM_QUIC_TRANSPORT");
    if (!legacy.empty()) {
      mode = legacy == "1" ? "quic" : "tls";
    }
  }
  if (mode.empty() && ctx) {
    auto context_mode = transport_mode_from_config(ctx->transport);
    if (!context_mode) {
      return context_mode.error();
    }
    mode = context_mode.value();
  }
  if (mode.empty() && env) {
    auto environment_mode = transport_mode_from_config(env->transport);
    if (!environment_mode) {
      return environment_mode.error();
    }
    mode = environment_mode.value();
  }
  if (mode.empty()) {
    mode = "auto";
  }
  for (auto& character : mode) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (mode != "auto" && mode != "tls" && mode != "quic") {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (mode == "quic") {
    return error::make_error_code(error::code::invalid_configuration);
  }
  return std::string("tls");
}

static boost::system::result<resolved_config> resolve_config_selection(const config_file& cfg, const std::string& api_url_explicit, const std::string& context_name)
{
  std::string api_url       = api_url_explicit;
  const config_context* ctx = nullptr;
  if (!context_name.empty()) {
    std::vector<const config_context*> matches;
    for (const auto& entry : cfg.contexts) {
      if (entry.name == context_name) {
        matches.push_back(&entry);
      }
    }
    if (matches.empty()) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (!api_url_explicit.empty()) {
      std::vector<const config_context*> exact;
      for (const auto* entry : matches) {
        if (entry->api_url == api_url_explicit) {
          exact.push_back(entry);
        }
      }
      if (exact.size() == 1) {
        ctx = exact[0];
      }
      else if (exact.empty()) {
        std::vector<const config_context*> unlinked;
        for (const auto* entry : matches) {
          if (entry->api_url.empty()) {
            unlinked.push_back(entry);
          }
        }
        if (unlinked.size() == 1) {
          ctx = unlinked[0];
        }
        else {
          return error::make_error_code(error::code::invalid_configuration);
        }
      }
      else {
        return error::make_error_code(error::code::invalid_configuration);
      }
    }
    else {
      if (matches.size() == 1) {
        ctx = matches[0];
      }
      else {
        return error::make_error_code(error::code::invalid_configuration);
      }
    }
    if (!api_url_explicit.empty() && ctx && !ctx->api_url.empty() && ctx->api_url != api_url_explicit) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (api_url.empty() && ctx && !ctx->api_url.empty()) {
      api_url = ctx->api_url;
    }
  }
  if (api_url.empty()) {
    api_url = "https://rstream.io";
  }
  const config_environment* env = nullptr;
  for (const auto& entry : cfg.environments) {
    if (entry.api_url == api_url) {
      env = &entry;
      break;
    }
  }
  if (ctx && ctx->api_url.empty()) {
    env = nullptr;
  }
  auto tunnel_transport_mode = resolve_tunnel_transport_mode(env, ctx);
  if (!tunnel_transport_mode) {
    return tunnel_transport_mode.error();
  }
  if ((ctx && transport_proxy_requested(ctx->transport.proxy)) || (env && transport_proxy_requested(env->transport.proxy))) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  return resolved_config{api_url, env, ctx};
}

static boost::system::result<boost::optional<std::string>> resolve_token_from_auth(const config_auth& auth)
{
  if (!auth.token.present) {
    return boost::optional<std::string>();
  }
  if (!auth.token.kind.empty() && auth.token.kind != "inline") {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (auth.token.value.empty()) {
    return boost::optional<std::string>();
  }
  return auth.token.value;
}

static boost::system::result<boost::optional<std::string>> resolve_token_from_config(const config_file& cfg, const std::string& api_url_explicit, const std::string& context_name)
{
  auto resolved = resolve_config_selection(cfg, api_url_explicit, context_name);
  if (!resolved) {
    return resolved.error();
  }
  if (resolved.value().context) {
    auto token = resolve_token_from_auth(resolved.value().context->auth);
    if (!token) {
      return token.error();
    }
    if (token.value()) {
      return token.value();
    }
  }
  if (resolved.value().environment) {
    auto token = resolve_token_from_auth(resolved.value().environment->auth);
    if (!token) {
      return token.error();
    }
    return token.value();
  }
  return boost::optional<std::string>();
}

static boost::system::result<boost::optional<std::string>> resolve_engine_from_config(const config_file& cfg, const std::string& api_url_explicit, const std::string& context_name)
{
  auto resolved = resolve_config_selection(cfg, api_url_explicit, context_name);
  if (!resolved) {
    return resolved.error();
  }
  if (resolved.value().context && !resolved.value().context->engine.empty()) {
    return resolved.value().context->engine;
  }
  return boost::optional<std::string>();
}

static boost::system::result<boost::optional<config_mtls>> resolve_mtls_from_config(const config_file& cfg, const std::string& api_url_explicit, const std::string& context_name)
{
  auto resolved = resolve_config_selection(cfg, api_url_explicit, context_name);
  if (!resolved) {
    return resolved.error();
  }
  if (resolved.value().context && resolved.value().context->auth.mtls.present) {
    return resolved.value().context->auth.mtls;
  }
  if (resolved.value().environment && resolved.value().environment->auth.mtls.present) {
    return resolved.value().environment->auth.mtls;
  }
  return boost::optional<config_mtls>();
}

static boost::system::result<boost::optional<config_tls>> resolve_tls_from_config(const config_file& cfg, const std::string& api_url_explicit, const std::string& context_name)
{
  auto resolved = resolve_config_selection(cfg, api_url_explicit, context_name);
  if (!resolved) {
    return resolved.error();
  }
  if (resolved.value().context && resolved.value().context->transport.tls.present) {
    return resolved.value().context->transport.tls;
  }
  if (resolved.value().environment && resolved.value().environment->transport.tls.present) {
    return resolved.value().environment->transport.tls;
  }
  return boost::optional<config_tls>();
}

static boost::optional<config_mtls> resolve_mtls_from_environment()
{
  config_mtls mtls;
  mtls.certificate_file = getenv_trim("RSTREAM_MTLS_CERT_FILE");
  mtls.key_file         = getenv_trim("RSTREAM_MTLS_KEY_FILE");
  mtls.present          = !mtls.certificate_file.empty() || !mtls.key_file.empty();
  if (!mtls.present) {
    return boost::none;
  }
  return mtls;
}

static std::string query_escape(const std::string& value)
{
  std::ostringstream escaped;
  escaped << std::uppercase << std::hex;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << static_cast<char>(c);
    }
    else {
      escaped << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return escaped.str();
}

static std::string append_query_param(std::string address, const std::string& key, const std::string& value)
{
  address += address.find('?') == std::string::npos ? '?' : '&';
  address += key;
  address += '=';
  address += query_escape(value);
  return address;
}

static int count_non_empty(const std::string& first, const std::string& second)
{
  int count = 0;
  if (!first.empty()) {
    count++;
  }
  if (!second.empty()) {
    count++;
  }
  return count;
}

static std::string pkcs11_uri_escape(const std::string& value)
{
  std::ostringstream escaped;
  escaped << std::uppercase << std::hex;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << static_cast<char>(c);
    }
    else {
      escaped << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return escaped.str();
}

static bool is_hex_string(const std::string& value)
{
  if (value.empty() || value.size() % 2 != 0) {
    return false;
  }
  for (unsigned char c : value) {
    if (!std::isxdigit(c)) {
      return false;
    }
  }
  return true;
}

static boost::system::result<std::string> pkcs11_uri_id_from_hex(const std::string& value)
{
  if (!is_hex_string(value)) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  std::ostringstream escaped;
  escaped << std::uppercase;
  for (size_t i = 0; i < value.size(); i += 2) {
    escaped << '%' << value[i] << value[i + 1];
  }
  return escaped.str();
}

static boost::system::result<std::string> pkcs11_uri_for_object(const config_mtls_storage& storage, const std::string& label, const std::string& id_hex, const std::string& type)
{
  std::string uri = "pkcs11:";
  if (!storage.token_label.empty()) {
    uri += "token=" + pkcs11_uri_escape(storage.token_label) + ";";
  }
  if (!storage.token_serial.empty()) {
    uri += "serial=" + pkcs11_uri_escape(storage.token_serial) + ";";
  }
  if (!label.empty()) {
    uri += "object=" + pkcs11_uri_escape(label) + ";";
  }
  else {
    auto id = pkcs11_uri_id_from_hex(id_hex);
    if (!id) {
      return id.error();
    }
    uri += "id=" + id.value() + ";";
  }
  uri += "type=" + type;
  return uri;
}

static boost::system::result<std::string> append_mtls_auth_params(std::string address, const boost::optional<config_mtls>& mtls)
{
  if (!mtls) {
    return address;
  }
  const bool has_inline_certificate = !mtls.value().certificate.empty();
  const bool has_file_certificate   = !mtls.value().certificate_file.empty();
  const bool has_inline_key         = !mtls.value().key.empty();
  const bool has_file_key           = !mtls.value().key_file.empty();
  if (mtls.value().storage.present) {
    if (has_inline_certificate || has_file_certificate || has_inline_key || has_file_key) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    const auto& storage = mtls.value().storage;
    if (storage.kind == "keychain") {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (storage.kind != "pkcs11") {
      return error::make_error_code(error::code::invalid_configuration);
    }
#ifndef RSTREAM_WITH_PKCS11
    return error::make_error_code(error::code::invalid_configuration);
#else
    if (!storage.provider.empty() || storage.module.empty() || storage.pin_env.empty()) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (storage.slot || storage.max_sessions != 0 || !storage.certificate_sha256.empty()) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (count_non_empty(storage.token_label, storage.token_serial) != 1) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (count_non_empty(storage.key_label, storage.key_id_hex) != 1) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    const bool has_pem_certificate   = !storage.certificate.empty() || !storage.certificate_file.empty();
    const bool has_token_certificate = !storage.certificate_label.empty() || !storage.certificate_id_hex.empty();
    if (count_non_empty(storage.certificate, storage.certificate_file) > 1 || count_non_empty(storage.certificate_label, storage.certificate_id_hex) > 1) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    if (has_pem_certificate == has_token_certificate) {
      return error::make_error_code(error::code::invalid_configuration);
    }
    auto key_uri = pkcs11_uri_for_object(storage, storage.key_label, storage.key_id_hex, "private");
    if (!key_uri) {
      return key_uri.error();
    }
    address = append_query_param(address, "ssl.pkcs11_module", storage.module);
    if (!storage.openssl_provider.empty()) {
      address = append_query_param(address, "ssl.pkcs11_provider", storage.openssl_provider);
    }
    address = append_query_param(address, "ssl.pkcs11_pin_env", storage.pin_env);
    address = append_query_param(address, "ssl.key", key_uri.value());
    address = append_query_param(address, "ssl.key_type", "pkcs11");
    if (!storage.certificate.empty()) {
      address = append_query_param(address, "ssl.cert", storage.certificate);
    }
    else if (!storage.certificate_file.empty()) {
      address = append_query_param(address, "ssl.cert_file", storage.certificate_file);
    }
    else {
      auto certificate_uri = pkcs11_uri_for_object(storage, storage.certificate_label, storage.certificate_id_hex, "cert");
      if (!certificate_uri) {
        return certificate_uri.error();
      }
      address = append_query_param(address, "ssl.cert", certificate_uri.value());
      address = append_query_param(address, "ssl.cert_type", "pkcs11");
    }
    return address;
#endif
  }
  if (has_inline_certificate == has_file_certificate || has_inline_key == has_file_key) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (has_inline_certificate != has_inline_key || has_file_certificate != has_file_key) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (has_inline_certificate) {
    address = append_query_param(address, "ssl.cert", mtls.value().certificate);
    address = append_query_param(address, "ssl.key", mtls.value().key);
  }
  else {
    address = append_query_param(address, "ssl.cert_file", mtls.value().certificate_file);
    address = append_query_param(address, "ssl.key_file", mtls.value().key_file);
  }
  return address;
}

static std::string append_engine_tls_params(std::string address, const boost::optional<config_tls>& tls_config)
{
  if (!tls_config) {
    return address;
  }
  if (!tls_config.value().ca_file.empty()) {
    address = append_query_param(address, "ssl.cacert_file", tls_config.value().ca_file);
  }
  if (!tls_config.value().server_name.empty()) {
    address = append_query_param(address, "ssl.sni", tls_config.value().server_name);
  }
  if (tls_config.value().insecure_skip_verify_set && tls_config.value().insecure_skip_verify) {
    address = append_query_param(address, "ssl.peer_verification", "false");
  }
  return address;
}

boost::system::result<nlohmann::json> get_rstream_config_file()
{
  auto cfg = load_rstream_config(boost::none);
  if (!cfg) {
    return cfg.error();
  }
  return config_file_to_json(cfg.value());
}

boost::system::result<nlohmann::json> get_rstream_config_file(const boost::optional<std::string>& config_path)
{
  auto cfg = load_rstream_config(config_path);
  if (!cfg) {
    return cfg.error();
  }
  return config_file_to_json(cfg.value());
}

boost::system::result<boost::optional<std::string>> get_rstream_token(const config& config, const io::address& server_address)
{
  const bool uses_mtls_auth = has_client_certificate_config(server_address);
  if (config.m_no_token) {
    return boost::none;
  }
  if (config.m_token) {
    if (uses_mtls_auth) {
      return error::make_error_code(error::code::authentication_conflict);
    }
    return config.m_token;
  }
  std::string env_token = getenv_trim("RSTREAM_AUTHENTICATION_TOKEN");
  if (!env_token.empty()) {
    if (uses_mtls_auth) {
      return error::make_error_code(error::code::authentication_conflict);
    }
    return env_token;
  }
  if (uses_mtls_auth) {
    return boost::none;
  }
  auto cfg = load_rstream_config(config.m_config_path);
  if (!cfg) {
    return cfg.error();
  }
  std::string api_url      = getenv_trim("RSTREAM_API_URL");
  std::string context_name = getenv_trim("RSTREAM_CONTEXT");
  if (context_name.empty() && !cfg.value().default_context.empty()) {
    context_name = cfg.value().default_context;
  }
  auto token = resolve_token_from_config(cfg.value(), api_url, context_name);
  if (!token) {
    return token.error();
  }
  return token.value();
}

boost::system::result<client_details> get_client_details(boost::optional<std::string> token)
{
  auto compiletime_identity     = core::get_compiletime_identity();
  auto protobuf_file_descriptor = protobuf::ClientDetails::descriptor()->file();
  boost::optional<std::string> protocol_version;
  if (protobuf_file_descriptor->options().HasExtension(protobuf::protocol_version)) {
    protocol_version = protobuf_file_descriptor->options().GetExtension(protobuf::protocol_version);
  }
  boost::optional<std::string> channel;
#ifdef RSTREAM_BUILD_CHANNEL
  {
    std::string value = std::string(RSTREAM_BUILD_CHANNEL);
    if (!value.empty()) {
      channel = value;
    }
  }
#endif
  boost::optional<std::string> os_value;
  if (!compiletime_identity.m_os.empty()) {
    os_value = compiletime_identity.m_os;
  }
  boost::optional<std::string> arch;
  if (!compiletime_identity.m_arch.empty()) {
    arch = compiletime_identity.m_arch;
  }
  return client_details{
      .m_agent            = std::string("rstream-utils"),
      .m_channel          = channel,
      .m_os               = os_value,
      .m_arch             = arch,
      .m_version          = std::string(RSTREAM_VERSION),
      .m_token            = std::move(token),
      .m_protocol_version = protocol_version,
  };
}

boost::system::result<client_details> get_client_details(const config& config, const io::address& server_address)
{
  auto token = get_rstream_token(config, server_address);
  if (token) {
    if (token.value() && has_client_certificate_config(server_address)) {
      return error::make_error_code(error::code::authentication_conflict);
    }
    return get_client_details(token.value());
  }
  else {
    return token.error();
  }
}

boost::system::result<std::string> get_rstream_engine_address()
{
  return get_rstream_engine_address(boost::none);
}

boost::system::result<std::string> get_rstream_engine_address(const boost::optional<std::string>& config_path)
{
  auto environment_transport_mode = resolve_tunnel_transport_mode(nullptr, nullptr);
  if (!environment_transport_mode) {
    return environment_transport_mode.error();
  }
  std::string engine_address = getenv_trim("RSTREAM_ENGINE_ADDRESS");
  if (!engine_address.empty()) {
    return append_mtls_auth_params(engine_address, resolve_mtls_from_environment());
  }
  boost::optional<config_mtls> mtls = resolve_mtls_from_environment();
  boost::optional<config_tls> tls_config;
  std::string engine = getenv_trim("RSTREAM_ENGINE");
  if (engine.empty()) {
    auto cfg = load_rstream_config(config_path);
    if (!cfg) {
      return cfg.error();
    }
    std::string api_url      = getenv_trim("RSTREAM_API_URL");
    std::string context_name = getenv_trim("RSTREAM_CONTEXT");
    if (context_name.empty() && !cfg.value().default_context.empty()) {
      context_name = cfg.value().default_context;
    }
    auto resolved_engine = resolve_engine_from_config(cfg.value(), api_url, context_name);
    if (!resolved_engine) {
      return resolved_engine.error();
    }
    if (resolved_engine.value()) {
      engine = resolved_engine.value().get();
    }
    if (!mtls) {
      auto resolved_mtls = resolve_mtls_from_config(cfg.value(), api_url, context_name);
      if (!resolved_mtls) {
        return resolved_mtls.error();
      }
      mtls = resolved_mtls.value();
    }
    auto resolved_tls = resolve_tls_from_config(cfg.value(), api_url, context_name);
    if (!resolved_tls) {
      return resolved_tls.error();
    }
    tls_config = resolved_tls.value();
  }
  if (engine.empty()) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (engine.find("://") != std::string::npos) {
    return append_mtls_auth_params(append_engine_tls_params(engine, tls_config), mtls);
  }
  return append_mtls_auth_params(append_engine_tls_params(std::string("tcp://") + engine + "?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1" + default_tls_groups_query(), tls_config), mtls);
}

boost::system::result<std::string> format_forwarding_address(const tunnel_properties& properties)
{
  if (properties.m_hostname || properties.m_host) {
    std::string res;
    std::string edge_protocol;
    std::string edge_protocol_suffix;
    if (properties.m_protocol && properties.m_protocol.value() == "http") {
      edge_protocol = "https";
      res           = edge_protocol + "://";
    }
    else if (properties.m_protocol && properties.m_protocol.value() == "webtty") {
      edge_protocol        = "https";
      edge_protocol_suffix = "webtty";
      res                  = edge_protocol + "://";
    }
    else {
      edge_protocol = properties.m_protocol.value_or("tls");
    }
    if (properties.m_hostname) {
      res += properties.m_hostname.value();
      if (properties.m_port && (edge_protocol != "https" || properties.m_port.value() != 443)) {
        res += ":" + std::to_string(properties.m_port.value());
      }
    }
    else {
      res += properties.m_host.value();
    }
    if (!edge_protocol_suffix.empty()) {
      res += " (" + edge_protocol_suffix + ")";
    }
    else if (edge_protocol != "https") {
      res += " (" + edge_protocol + ")";
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
      if (forwarded_address.host().empty()) {
        return boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
      }
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
    auto upstream_tls = properties.m_upstream_tls ? properties.m_upstream_tls : properties.m_http_use_tls;
    if (upstream_tls && upstream_tls.value()) {
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
    else if (properties.m_upstream_tls && properties.m_upstream_tls.value()) {
      if (properties.m_protocol && properties.m_protocol.value() == "dtls") {
        res += " (dtls)";
      }
      else if (properties.m_protocol && properties.m_protocol.value() == "quic") {
        res += " (quic)";
      }
      else {
        res += " (tls)";
      }
    }
    else if (properties.m_protocol && properties.m_protocol.value() == "dtls") {
      res += " (udp)";
    }
    else if (properties.m_protocol && properties.m_protocol.value() == "quic") {
      res += " (quic)";
    }
    else if (properties.m_protocol && properties.m_protocol.value() == "webtty") {
      res += " (webtty)";
    }
    else {
      res += " (tcp)";
    }
  }
  return res;
}

}  // namespace io_rstrm
}  // namespace rstream
