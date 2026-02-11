// See LICENSE file in the project root for license information.

#include "io-rstrm.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>
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
  PARSE_PARAMS_VIEW_STRING(url.params(), properties, "rstrm.", error_code, type)
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
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, rstream_auth)
  PARSE_PARAMS_VIEW_BOOLEAN(url.params(), properties, "rstrm.", error_code, challenge_mode)
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

struct config_token {
  config_token()
      : present(false)
  {
  }
  bool present;
  std::string kind;
  std::string value;
};

struct config_auth {
  config_token token;
};

struct config_environment {
  std::string api_url;
  config_auth auth;
};

struct config_context {
  std::string name;
  std::string api_url;
  std::string engine;
  config_auth auth;
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

static void parse_auth_storage(const YAML::Node& node, config_auth& auth)
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

static YAML::Node get_auth_storage_node(const YAML::Node& node)
{
  if (!node || !node.IsMap()) {
    return YAML::Node();
  }
  YAML::Node auth = node["auth"];
  if (!auth || !auth.IsMap()) {
    return YAML::Node();
  }
  YAML::Node token = auth["token"];
  if (!token || !token.IsMap()) {
    return YAML::Node();
  }
  YAML::Node storage = token["storage"];
  if (!storage || !storage.IsMap()) {
    return YAML::Node();
  }
  return storage;
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
      parse_auth_storage(get_auth_storage_node(env_node), env.auth);
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
      parse_auth_storage(get_auth_storage_node(ctx_node), ctx.auth);
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
      if (env.auth.token.present) {
        env_json["auth"]["token"]["storage"]["kind"]  = env.auth.token.kind;
        env_json["auth"]["token"]["storage"]["value"] = env.auth.token.value;
      }
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
      if (ctx.auth.token.present) {
        ctx_json["auth"]["token"]["storage"]["kind"]  = ctx.auth.token.kind;
        ctx_json["auth"]["token"]["storage"]["value"] = ctx.auth.token.value;
      }
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
  (void)server_address;
  if (config.m_no_token) {
    return boost::none;
  }
  if (config.m_token) {
    return config.m_token;
  }
  std::string env_token = getenv_trim("RSTREAM_AUTHENTICATION_TOKEN");
  if (!env_token.empty()) {
    return env_token;
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

boost::system::result<client_details> get_client_details(const boost::optional<std::string> token)
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
  return (client_details){
      .m_agent            = std::string("rstream-utils"),
      .m_channel          = channel,
      .m_os               = os_value,
      .m_arch             = arch,
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
  return get_rstream_engine_address(boost::none);
}

boost::system::result<std::string> get_rstream_engine_address(const boost::optional<std::string>& config_path)
{
  std::string engine_address = getenv_trim("RSTREAM_ENGINE_ADDRESS");
  if (!engine_address.empty()) {
    return engine_address;
  }
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
  }
  if (engine.empty()) {
    return error::make_error_code(error::code::invalid_configuration);
  }
  if (engine.find("://") != std::string::npos) {
    return engine;
  }
  return std::string("tcp://") + engine + "?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1";
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
