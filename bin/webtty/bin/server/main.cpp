// See LICENSE file in the project root for license information.

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>
#include <webtty_cli.hpp>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/webtty/server.hpp>
#include <rstream/webtty/webtty.hpp>

static const char USAGE[] = R"(
rstream-webtty-server - https://rstream.io/ - Web Remote Terminal server using rstream primitives

this program is distributed with the rstream C++ tools. See https://rstream.io/docs/integrations/cpp-sdk and https://github.com/rstreamlabs/rstream-cpp.

usage:
  rstream-webtty-server [options] [--authorized-client-key=ARG]... [--label=ARG]...
  rstream-webtty-server (-h|--help)
  rstream-webtty-server --version

options:
  -h --help                   show this screen
  --version                   show version
  -v --verbose                enable verbose mode
  --uri=ARG                   URI [default: 127.0.0.1:6002]
  --rstream                   serve over an rstream tunnel
  --publish                   publish the rstream tunnel
  --no-publish                keep the rstream tunnel private
  --webtty-config=ARG         WebTTY server runtime config file; may contain serverId or serverEnrollment
  --server-id=ARG             registered WebTTY server ID; implies --rstream
  --server-enrollment=ARG     registered WebTTY server enrollment file; implies --rstream
  --transport=ARG             WebTTY transport to use [default: websocket]
  --execution-mode=ARG        execution mode (spawn, login); defaults to login for registered servers and spawn otherwise [default: spawn]
  --login-user=ARG            default OS user for login execution mode
  --allow-client-user         allow clients to request an OS user in login execution mode
  --auth-token-file=ARG       read local WebTTY bearer token from file
  --allow-unauthenticated     allow unauthenticated local WebTTY access
  --e2e                       require end-to-end encrypted WebTTY terminal content
  --identity=ARG              named local WebTTY server identity
  --identity-file=ARG         local WebTTY server identity file
  --authorized-client-key=ARG authorized WebTTY client signing key (may be specified multiple times)
  --authorized-clients-file=ARG authorized WebTTY client keys file
  --label=ARG                 set WebTTY inventory label (key=value, may be specified multiple times)
  -j --jobs=ARG               number of threads to run simultaneously (0 = auto) [default: 0]

valid transports: plain, websocket
valid execution modes: spawn, login
note: login execution mode does not use passwords; POSIX user switching requires suitable local privileges
)";

const auto version = std::string("rstream-webtty-server ") + RSTREAM_VERSION;

void apply_labels(std::map<std::string, std::string>& labels, const std::vector<std::string>& values)
{
  for (const auto& raw : values) {
    auto value = rstream::webtty::cli::trim_copy(raw);
    auto pos   = value.find('=');
    if (pos == std::string::npos || pos == 0) {
      throw std::runtime_error("--label must be key=value");
    }
    labels[value.substr(0, pos)] = value.substr(pos + 1);
  }
}

std::string workspace_json_string(const nlohmann::json& value, const std::string& key)
{
  auto it = value.find(key);
  if (it == value.end() || !it->is_string()) {
    return "";
  }
  return it->get<std::string>();
}

std::string workspace_canonical_json(const nlohmann::json& value)
{
  if (value.is_null()) {
    return "null";
  }
  if (value.is_string() || value.is_boolean()) {
    return value.dump();
  }
  if (value.is_number_integer() || value.is_number_unsigned()) {
    return value.dump();
  }
  if (value.is_number_float()) {
    throw std::runtime_error("workspace-managed WebTTY credential contains an unsupported JSON number");
  }
  if (value.is_array()) {
    std::string out = "[";
    for (std::size_t i = 0; i < value.size(); ++i) {
      if (i > 0) {
        out += ",";
      }
      out += workspace_canonical_json(value[i]);
    }
    out += "]";
    return out;
  }
  if (value.is_object()) {
    std::string out = "{";
    bool first      = true;
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (it.value().is_null()) {
        continue;
      }
      if (!first) {
        out += ",";
      }
      first = false;
      out += nlohmann::json(it.key()).dump();
      out += ":";
      out += workspace_canonical_json(it.value());
    }
    out += "}";
    return out;
  }
  throw std::runtime_error("workspace-managed WebTTY credential contains unsupported JSON");
}

std::string workspace_sha256_base64url(const nlohmann::json& value)
{
  auto canonical                             = workspace_canonical_json(value);
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(canonical.data()), canonical.size(), digest);
  return rstream::webtty::cli::base64url_encode(rstream::webtty::byte_vector(digest, digest + SHA256_DIGEST_LENGTH));
}

std::string workspace_public_key_fingerprint(const std::string& public_encryption_key, const std::string& public_signing_key)
{
  nlohmann::json payload = {
      {"public_encryption_key", public_encryption_key},
      {"public_signing_key", public_signing_key},
      {"type", "workspace.public_keys"},
      {"v", 1},
  };
  return "sha256:" + workspace_sha256_base64url(payload);
}

void verify_workspace_signature(const std::string& public_signing_key, const nlohmann::json& payload, const std::string& signature, const std::string& label)
{
  auto public_key = rstream::webtty::cli::base64url_decode(public_signing_key, 0, label + " public signing key");
  auto sig        = rstream::webtty::cli::base64url_decode(signature, 0, label + " signature");
  auto canonical  = workspace_canonical_json(payload);
  std::error_code error_code;
  rstream::webtty::verify_p256_sha256_signature(public_key, rstream::webtty::byte_vector(canonical.begin(), canonical.end()), sig, error_code);
  if (error_code) {
    throw std::runtime_error(label + " signature is invalid");
  }
}

bool workspace_trust_payload_matches(const rstream::webtty::cli::server_enrollment& enrollment, const nlohmann::json& credential, const nlohmann::json& trust_payload)
{
  if (workspace_json_string(trust_payload, "workspace_id") != enrollment.m_workspace_id) {
    return false;
  }
  auto device_fingerprint = workspace_json_string(credential, "device_fingerprint");
  auto device_key_id      = workspace_json_string(credential, "device_key_id");
  auto type               = workspace_json_string(trust_payload, "type");
  if (type == "workspace.keyset.setup") {
    return workspace_json_string(trust_payload, "keyset_fingerprint") == enrollment.m_workspace_trust_keyset_fingerprint && workspace_json_string(trust_payload, "keyset_public_signing_key") == enrollment.m_workspace_trust_public_signing_key && workspace_json_string(trust_payload, "device_fingerprint") == device_fingerprint;
  }
  if (type == "workspace.device.approve") {
    return workspace_json_string(trust_payload, "keyset_id") == enrollment.m_workspace_trust_keyset_id && workspace_json_string(trust_payload, "target_device_key_id") == device_key_id && workspace_json_string(trust_payload, "target_fingerprint") == device_fingerprint;
  }
  if (type == "workspace.recovery_kit.use") {
    return workspace_json_string(trust_payload, "keyset_id") == enrollment.m_workspace_trust_keyset_id && workspace_json_string(trust_payload, "device_fingerprint") == device_fingerprint;
  }
  return false;
}

boost::optional<rstream::webtty::byte_vector> verify_workspace_client_credential(const rstream::webtty::cli::server_enrollment& enrollment,
                                                                                 const rstream::webtty::byte_vector& client_key_id,
                                                                                 const rstream::webtty::byte_vector& client_public_key,
                                                                                 const rstream::webtty::byte_vector& credential)
{
  if (credential.empty()) {
    return boost::none;
  }
  auto envelope = nlohmann::json::parse(std::string(credential.begin(), credential.end()));
  if (!envelope.is_object() || envelope.value("v", 0) != 1 || !envelope.contains("payload") || !envelope["payload"].is_object()) {
    throw std::runtime_error("workspace-managed WebTTY client credential is invalid");
  }
  const auto& payload = envelope["payload"];
  if (workspace_json_string(payload, "type") != "workspace.webtty.client.credential") {
    throw std::runtime_error("workspace-managed WebTTY client credential has an unsupported type");
  }
  if (workspace_json_string(payload, "workspace_id") != enrollment.m_workspace_id || workspace_json_string(payload, "project_id") != enrollment.m_project_id || workspace_json_string(payload, "server_id") != enrollment.m_server_id) {
    throw std::runtime_error("workspace-managed WebTTY client credential does not match this server");
  }
  if (workspace_json_string(payload, "trust_keyset_id") != enrollment.m_workspace_trust_keyset_id) {
    throw std::runtime_error("workspace-managed WebTTY client credential keyset does not match server enrollment");
  }
  if (workspace_json_string(payload, "client_signing_key_id") != rstream::webtty::cli::base64url_encode(client_key_id)) {
    throw std::runtime_error("workspace-managed WebTTY client credential signing key id does not match proof");
  }
  auto public_signing_key        = workspace_json_string(payload, "client_signing_public_key");
  auto device_public_signing_key = workspace_json_string(payload, "device_public_signing_key");
  if (public_signing_key.empty() || public_signing_key != device_public_signing_key) {
    throw std::runtime_error("workspace-managed WebTTY client credential signing key does not match trusted device");
  }
  auto public_signing_key_bytes = rstream::webtty::cli::base64url_decode(public_signing_key, 0, "workspace-managed WebTTY client signing public key");
  if (public_signing_key_bytes != client_public_key) {
    throw std::runtime_error("workspace-managed WebTTY client credential signing key does not match proof");
  }
  auto fingerprint = workspace_public_key_fingerprint(workspace_json_string(payload, "device_public_encryption_key"), device_public_signing_key);
  if (fingerprint != workspace_json_string(payload, "device_fingerprint")) {
    throw std::runtime_error("workspace-managed WebTTY client credential device fingerprint does not match public keys");
  }
  if (!payload.contains("trust_payload") || !payload["trust_payload"].is_object()) {
    throw std::runtime_error("workspace-managed WebTTY client credential trust payload is invalid");
  }
  const auto& trust_payload = payload["trust_payload"];
  if (workspace_sha256_base64url(trust_payload) != workspace_json_string(payload, "trust_payload_hash")) {
    throw std::runtime_error("workspace-managed WebTTY client credential trust payload hash does not match payload");
  }
  if (!workspace_trust_payload_matches(enrollment, payload, trust_payload)) {
    throw std::runtime_error("workspace-managed WebTTY client credential trust payload does not match device");
  }
  verify_workspace_signature(enrollment.m_workspace_trust_public_signing_key,
                             trust_payload,
                             workspace_json_string(payload, "trust_keyset_signature"),
                             "workspace-managed WebTTY device trust");
  verify_workspace_signature(public_signing_key,
                             payload,
                             workspace_json_string(envelope, "signature"),
                             "workspace-managed WebTTY client credential");
  return client_public_key;
}

int run(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      rstream::core::log::enable_ansicolor_stdout_mt();
    }
  }
  rstream::core::default_logger()->info(version);
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  std::string uri = args.at("--uri").asString();
  bool use_web    = args.at("--rstream").asBool();
  bool publish    = true;
  if (args.at("--publish").asBool() && args.at("--no-publish").asBool()) {
    throw std::runtime_error("--publish and --no-publish cannot be combined");
  }
  if (args.at("--no-publish").asBool()) {
    publish = false;
  }
  auto config_path_value             = args.at("--webtty-config") ? args.at("--webtty-config").asString() : "";
  auto config_path                   = rstream::webtty::cli::config_path_from_arg_or_env(config_path_value);
  std::string server_id              = args.at("--server-id") ? args.at("--server-id").asString() : "";
  std::string server_enrollment_path = args.at("--server-enrollment") ? args.at("--server-enrollment").asString() : "";
  std::string identity_name          = args.at("--identity") ? args.at("--identity").asString() : "";
  std::string identity_file          = args.at("--identity-file") ? args.at("--identity-file").asString() : "";
  bool identity_file_configured      = !identity_file.empty();
  boost::optional<rstream::webtty::endpoint_identity> inline_identity;
  bool e2e_requested                  = args.at("--e2e").asBool();
  std::string login_user              = args.at("--login-user") ? args.at("--login-user").asString() : "";
  bool allow_client_user              = args.at("--allow-client-user").asBool();
  std::string auth_token_file         = args.at("--auth-token-file") ? args.at("--auth-token-file").asString() : "";
  bool allow_unauthenticated          = args.at("--allow-unauthenticated").asBool();
  std::string authorized_clients_file = args.at("--authorized-clients-file") ? args.at("--authorized-clients-file").asString() : "";
  std::string host_key_id;
  std::map<std::string, std::string> labels;
  std::vector<std::string> authorized_client_keys;
  std::string transport_value      = args.at("--transport").asString();
  std::string execution_mode_value = args.at("--execution-mode").asString();
  bool execution_mode_explicit     = rstream::webtty::cli::argv_has(argc, argv, "--execution-mode");
  if (!config_path.empty()) {
    auto config = rstream::webtty::cli::load_server_runtime_config(config_path);
    if (!rstream::webtty::cli::argv_has(argc, argv, "--uri") && config.m_uri) {
      uri = *config.m_uri;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--rstream") && config.m_web) {
      use_web = *config.m_web;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--publish") && !rstream::webtty::cli::argv_has(argc, argv, "--no-publish") && config.m_publish) {
      publish = *config.m_publish;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--server-id") && config.m_server_id) {
      server_id = *config.m_server_id;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--server-enrollment") && config.m_server_enrollment) {
      server_enrollment_path = *config.m_server_enrollment;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--transport") && config.m_transport) {
      transport_value = *config.m_transport;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--execution-mode") && config.m_execution_mode) {
      execution_mode_value    = *config.m_execution_mode;
      execution_mode_explicit = true;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--login-user") && config.m_login_user) {
      login_user = *config.m_login_user;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--allow-client-user") && config.m_allow_client_user) {
      allow_client_user = *config.m_allow_client_user;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--auth-token-file") && config.m_auth_token_file) {
      auth_token_file = *config.m_auth_token_file;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--allow-unauthenticated") && config.m_allow_unauthenticated) {
      allow_unauthenticated = *config.m_allow_unauthenticated;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--identity-file") && !rstream::webtty::cli::argv_has(argc, argv, "--identity") && config.m_identity_file) {
      identity_file            = *config.m_identity_file;
      identity_file_configured = true;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--identity") && config.m_identity) {
      identity_name = *config.m_identity;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--e2e") && config.m_e2e) {
      e2e_requested = *config.m_e2e;
    }
    if (!rstream::webtty::cli::argv_has(argc, argv, "--authorized-clients-file") && config.m_authorized_clients_file) {
      authorized_clients_file = *config.m_authorized_clients_file;
    }
    labels = config.m_labels;
  }
  std::vector<std::string> label_args;
  auto label_value = args.at("--label");
  if (label_value && label_value.isStringList()) {
    label_args = label_value.asStringList();
  }
  else if (label_value && label_value.isString()) {
    label_args.push_back(label_value.asString());
  }
  apply_labels(labels, label_args);
  auto authorized_client_key_value = args.at("--authorized-client-key");
  if (authorized_client_key_value && authorized_client_key_value.isStringList()) {
    authorized_client_keys = authorized_client_key_value.asStringList();
  }
  else if (authorized_client_key_value && authorized_client_key_value.isString()) {
    authorized_client_keys.push_back(authorized_client_key_value.asString());
  }
  for (const auto& key : rstream::webtty::cli::getenv_comma_separated_values(rstream::webtty::cli::authorized_client_keys_env)) {
    authorized_client_keys.push_back(key);
  }
  if (authorized_clients_file.empty()) {
    authorized_clients_file = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::authorized_clients_file_env);
  }
  if (!server_id.empty() || !server_enrollment_path.empty()) {
    use_web = true;
  }
  if (!use_web && (rstream::webtty::cli::argv_has(argc, argv, "--publish") || rstream::webtty::cli::argv_has(argc, argv, "--no-publish"))) {
    throw std::runtime_error("--publish and --no-publish require --rstream, --server-id, or --server-enrollment");
  }
  rstream::webtty::protocol::type protocol_type;
  rstream::webtty::protocol::parse_type(protocol_type, transport_value);
  auto auth_token = rstream::webtty::cli::read_auth_token(auth_token_file);
  boost::optional<std::string> auth_token_option;
  if (auth_token) {
    auth_token_option = *auth_token;
  }
  if (use_web && auth_token) {
    throw std::runtime_error("--auth-token-file and RSTREAM_WEBTTY_AUTH_TOKEN are only used by local WebTTY WebSocket servers");
  }
  if (protocol_type == rstream::webtty::protocol::type::plain && auth_token) {
    throw std::runtime_error("plain WebTTY transport does not support HTTP bearer tokens");
  }
  if (!use_web && protocol_type == rstream::webtty::protocol::type::plain && !allow_unauthenticated) {
    throw std::runtime_error("local plain WebTTY transport has no HTTP auth layer; use --allow-unauthenticated only for isolated development");
  }
  if (!use_web && protocol_type == rstream::webtty::protocol::type::websocket && !auth_token && !allow_unauthenticated) {
    throw std::runtime_error("local WebTTY server requires --auth-token-file or RSTREAM_WEBTTY_AUTH_TOKEN; use --allow-unauthenticated only for isolated development");
  }
  identity_name = rstream::webtty::cli::trim_copy(identity_name);
  if (!identity_name.empty() && identity_file_configured) {
    throw std::runtime_error("--identity cannot be combined with --identity-file");
  }
  auto env_identity = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::identity_env);
  if (!env_identity.empty()) {
    if (!identity_name.empty() || identity_file_configured) {
      throw std::runtime_error(std::string(rstream::webtty::cli::identity_env) + " cannot be combined with --identity or --identity-file");
    }
    inline_identity = rstream::webtty::cli::load_identity_json(env_identity, rstream::webtty::cli::identity_env);
  }
  if (!identity_name.empty()) {
    identity_file            = rstream::webtty::cli::default_identity_path(identity_name);
    identity_file_configured = true;
  }
  if (identity_file.empty()) {
    auto env_identity_file = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::identity_file_env);
    if (!env_identity_file.empty()) {
      identity_file            = env_identity_file;
      identity_file_configured = true;
    }
  }
  std::optional<rstream::webtty::cli::server_enrollment> enrollment;
  if (!server_id.empty() || !server_enrollment_path.empty()) {
    use_web = true;
    if (!server_id.empty()) {
      rstream::webtty::cli::validate_local_name(server_id, "--server-id");
    }
    if (server_enrollment_path.empty()) {
      server_enrollment_path = rstream::webtty::cli::default_server_enrollment_path(server_id);
    }
    enrollment = rstream::webtty::cli::load_server_enrollment(server_enrollment_path);
    if (!server_id.empty() && enrollment->m_server_id != server_id) {
      throw std::runtime_error("WebTTY server enrollment belongs to another server ID");
    }
    server_id = enrollment->m_server_id;
    if (identity_file.empty()) {
      identity_file = enrollment->m_identity_file;
    }
  }
  if (enrollment && !execution_mode_explicit) {
    execution_mode_value = "login";
  }
  rstream::webtty::execution_mode execution_mode;
  rstream::webtty::parse_execution_mode(execution_mode, execution_mode_value);
  if (execution_mode != rstream::webtty::execution_mode::login && (!rstream::webtty::cli::trim_copy(login_user).empty() || allow_client_user)) {
    throw std::runtime_error("--login-user and --allow-client-user require --execution-mode=login");
  }
  if (enrollment && execution_mode == rstream::webtty::execution_mode::login && rstream::webtty::cli::trim_copy(login_user).empty() && !allow_client_user) {
    throw std::runtime_error("registered WebTTY servers default to login execution mode; set --login-user, --allow-client-user, or --execution-mode=spawn");
  }
  rstream::webtty::protocol::username default_username;
  rstream::webtty::protocol::parse_username(default_username, rstream::webtty::cli::trim_copy(login_user));
  std::optional<rstream::webtty::endpoint_identity> registered_identity;
  if (enrollment) {
    registered_identity = inline_identity ? *inline_identity : rstream::webtty::cli::load_identity_file(identity_file);
    rstream::webtty::cli::validate_identity_matches_enrollment(*registered_identity, *enrollment);
  }
  const bool e2e_active                  = e2e_requested || inline_identity || identity_file_configured || !authorized_client_keys.empty() || (enrollment && rstream::webtty::cli::enrollment_requires_e2e(*enrollment));
  rstream::webtty::server::config config = {
      .m_address       = rstream::io::address(uri),
      .m_protocol_type = protocol_type,
  };
  rstream::webtty::settings_server settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 5000,
          },
      },
      .m_timeouts_start_ms   = 5000,
      .m_std_out_buffer_size = 800 * 1024,
      .m_std_err_buffer_size = 800 * 1024,
      .m_execution_mode      = execution_mode,
      .m_default_username    = default_username,
      .m_allow_client_user   = allow_client_user,
      .m_auth_token          = auth_token_option,
  };
  if (e2e_active) {
    if (identity_file.empty() && !inline_identity) {
      identity_file = rstream::webtty::cli::default_identity_path("default");
    }
    auto identity                      = registered_identity ? *registered_identity : (inline_identity ? *inline_identity : rstream::webtty::cli::load_identity_file_or_create(identity_file));
    host_key_id                        = rstream::webtty::cli::base64url_encode(identity.m_encryption.m_key_id);
    settings.m_payload_crypto_resolver = rstream::webtty::make_e2e_server_payload_crypto_resolver(identity.m_encryption);
    settings.m_endpoint_identity       = identity;
    settings.m_require_client_proof    = true;
    settings.m_workspace_id            = enrollment ? enrollment->m_workspace_id : "";
    settings.m_project_id              = enrollment ? enrollment->m_project_id : "";
    settings.m_server_id               = !server_id.empty() ? server_id : (enrollment ? enrollment->m_server_id : "");
    if (enrollment && enrollment->m_encryption_policy == "workspace_managed") {
      if (!authorized_client_keys.empty() || !authorized_clients_file.empty()) {
        throw std::runtime_error("workspace-managed WebTTY servers use trusted workspace devices for client authorization; remove --authorized-client-key, --authorized-clients-file, and RSTREAM_WEBTTY_AUTHORIZED_CLIENT_KEYS");
      }
      settings.m_client_proof_credential_verifier = [enrollment_value = *enrollment](const rstream::webtty::byte_vector& client_key_id,
                                                                                     const rstream::webtty::byte_vector& client_public_key,
                                                                                     const rstream::webtty::byte_vector& credential) -> boost::optional<rstream::webtty::byte_vector> {
        return verify_workspace_client_credential(enrollment_value, client_key_id, client_public_key, credential);
      };
    }
    if (!(enrollment && enrollment->m_encryption_policy == "workspace_managed")) {
      for (const auto& value : authorized_client_keys) {
        auto parsed = rstream::webtty::cli::parse_authorized_client_key(value);
        auto key    = std::string(parsed.first.begin(), parsed.first.end());
        auto it     = settings.m_authorized_client_signing_keys.find(key);
        if (it != settings.m_authorized_client_signing_keys.end() && it->second != parsed.second) {
          throw std::runtime_error("conflicting authorized WebTTY client signing key");
        }
        settings.m_authorized_client_signing_keys[key] = parsed.second;
      }
      if (authorized_clients_file.empty()) {
        std::string store_name = "default";
        if (!identity_name.empty()) {
          store_name = identity_name;
        }
        else if (!server_id.empty()) {
          store_name = server_id;
        }
        else if (!identity_file.empty()) {
          auto base   = std::filesystem::path(identity_file).filename().string();
          auto suffix = std::string(".identity.json");
          if (base.size() > suffix.size() && base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
            base.erase(base.size() - suffix.size());
          }
          else {
            base = std::filesystem::path(base).stem().string();
          }
          if (!base.empty()) {
            store_name = base;
          }
        }
        authorized_clients_file = rstream::webtty::cli::default_authorized_clients_path(store_name);
      }
      settings.m_authorized_client_signing_key_resolver = [authorized_clients_file](const rstream::webtty::byte_vector& client_key_id) -> boost::optional<rstream::webtty::byte_vector> {
        auto keys = rstream::webtty::cli::load_authorized_clients_file(authorized_clients_file);
        auto it   = keys.find(std::string(client_key_id.begin(), client_key_id.end()));
        if (it == keys.end()) {
          return boost::none;
        }
        return it->second;
      };
    }
  }
  if (use_web) {
    if (protocol_type != rstream::webtty::protocol::type::websocket) {
      throw std::runtime_error("transport must be set to websocket when using rstream tunnels");
    }
    if (rstream::webtty::cli::argv_has(argc, argv, "--uri")) {
      throw std::runtime_error("--uri cannot be combined with --rstream, --server-id, or --server-enrollment");
    }
    rstream::webtty::webtty_uri_options uri_options;
    uri_options.m_managed           = enrollment.has_value();
    uri_options.m_publish           = publish;
    uri_options.m_execution_mode    = execution_mode;
    uri_options.m_server_id         = server_id;
    uri_options.m_host_key_id       = host_key_id;
    uri_options.m_encryption_policy = enrollment ? enrollment->m_encryption_policy : "";
    uri_options.m_labels            = labels;
    if (enrollment) {
      uri_options.m_server_admission_label = rstream::webtty::cli::create_server_admission_label(*enrollment, *registered_identity, rstream::webtty::build_webtty_labels(uri_options));
    }
    uri              = rstream::webtty::build_webtty_uri(uri_options);
    config.m_address = rstream::io::address(uri);
  }
  rstream::webtty::server server(io_context.get_executor(), config, settings);
  signal_set.async_wait([&server](const std::error_code&, int) { server.cancel(); });
  std::error_code result;
  server.async_run([&signal_set, &result](const std::error_code& error_code) {
    result = error_code;
    signal_set.cancel();
  });
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (decltype(n) i = 0; i < n; ++i) {
      threads.emplace_back([&io_context]() { io_context.run(); });
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  if (result) {
    std::cerr << result.message() << std::endl;
  }
  return result ? -1 : 0;
}

int main(int argc, char** argv)
{
  std::exception_ptr error;
  try {
    return run(argc, argv);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::cerr << "a fatal error occurred: " << rstream::core::throwable::message(error) << std::endl;
  }
  return error ? -1 : 0;
}
