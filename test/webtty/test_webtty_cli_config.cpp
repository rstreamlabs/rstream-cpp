// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <rstream/webtty/webtty.hpp>

#include "../../bin/webtty/bin/common/webtty_cli.hpp"

namespace cli = rstream::webtty::cli;

static std::filesystem::path temp_dir()
{
  auto now  = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() / ("rstream-cpp-webtty-cli-" + std::to_string(now));
  std::filesystem::create_directories(path);
  return path;
}

static void write_text(const std::filesystem::path& path, const std::string& content)
{
  std::ofstream file(path, std::ios::trunc);
  assert(file.is_open());
  file << content;
}

template <typename Fn>
static bool throws_runtime_error(Fn fn)
{
  try {
    fn();
  }
  catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

template <typename Fn>
static void require_no_runtime_error(const std::string& label, Fn fn)
{
  try {
    fn();
  }
  catch (const std::runtime_error& e) {
    throw std::runtime_error(label + ": " + e.what());
  }
}

static rstream::webtty::e2e_identity test_identity()
{
  rstream::webtty::e2e_identity identity;
  std::error_code error_code;
  rstream::webtty::generate_e2e_identity(identity, error_code);
  assert(!error_code);
  return identity;
}

static rstream::webtty::endpoint_identity test_endpoint_identity()
{
  rstream::webtty::endpoint_identity identity;
  std::error_code error_code;
  rstream::webtty::generate_endpoint_identity(identity, error_code);
  assert(!error_code);
  return identity;
}

static void check_argv_has()
{
  char program[]   = "rstream-webtty-server";
  char server_id[] = "--server-id=prod-shell";
  char label[]     = "--label=env=prod";
  char short_web[] = "-w";
  char* argv[]     = {program, server_id, label, short_web};
  assert(cli::argv_has(4, argv, "--server-id"));
  assert(cli::argv_has(4, argv, "--label"));
  assert(cli::argv_has(4, argv, "--rstream", "-w"));
  assert(!cli::argv_has(4, argv, "--server"));
}

static void check_known_server_key_validation()
{
  auto identity   = test_identity();
  auto public_key = cli::base64url_encode(identity.m_public_key);
  auto key_id     = cli::base64url_encode(identity.m_key_id);
  assert(cli::base64url_decode("AA", 1, "test key") == rstream::webtty::byte_vector{0});
  assert(throws_runtime_error([]() { cli::base64url_decode("AB", 1, "test key"); }));
  assert(throws_runtime_error([]() { cli::base64url_decode("AA==", 1, "test key"); }));
  assert(throws_runtime_error([]() { cli::base64url_decode("AA+", 1, "test key"); }));
  auto bare = cli::parse_known_server_key(public_key);
  assert(bare.m_public_key == identity.m_public_key);
  assert(bare.m_key_id == identity.m_key_id);
  auto explicit_key = cli::parse_known_server_key(key_id + ":" + public_key);
  assert(explicit_key.m_public_key == identity.m_public_key);
  assert(explicit_key.m_key_id == identity.m_key_id);
  auto other    = test_identity();
  auto rejected = false;
  try {
    cli::parse_known_server_key(cli::base64url_encode(other.m_key_id) + ":" + public_key);
  }
  catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

static void check_runtime_config_validation()
{
  auto dir        = temp_dir();
  auto token_path = dir / "token.txt";
  write_text(token_path, "  local-secret\n");
  auto path = dir / "server.yaml";
  write_text(path,
             "version: 1\n"
             "server:\n"
             "  rstream: true\n"
             "  publish: false\n"
             "  serverId: prod-shell\n"
             "  transport: websocket\n"
             "  executionMode: spawn\n"
             "  authTokenFile: "
                 + token_path.string() +
                 "\n"
                 "  allowUnauthenticated: false\n"
                 "  labels:\n"
                 "    env: prod\n"
                 "e2e:\n"
                 "  enabled: true\n"
                 "  identity: prod-shell\n");
  auto config = cli::load_server_runtime_config(path.string());
  assert(config.m_web && *config.m_web);
  assert(config.m_publish && !*config.m_publish);
  assert(config.m_server_id && *config.m_server_id == "prod-shell");
  assert(config.m_transport && *config.m_transport == "websocket");
  assert(config.m_execution_mode && *config.m_execution_mode == "spawn");
  assert(config.m_auth_token_file && *config.m_auth_token_file == token_path.string());
  assert(config.m_allow_unauthenticated && !*config.m_allow_unauthenticated);
  assert(config.m_e2e && *config.m_e2e);
  assert(config.m_identity && *config.m_identity == "prod-shell");
  assert(config.m_labels.at("env") == "prod");
  auto unknown_path = dir / "unknown.yaml";
  write_text(unknown_path,
             "version: 1\n"
             "server:\n"
             "  retry: true\n");
  assert(throws_runtime_error([&unknown_path]() { cli::load_server_runtime_config(unknown_path.string()); }));
  auto login_path = dir / "login.yaml";
  write_text(login_path,
             "version: 1\n"
             "server:\n"
             "  executionMode: login\n"
             "  loginUser: operator\n");
  auto login_config = cli::load_server_runtime_config(login_path.string());
  assert(login_config.m_execution_mode && *login_config.m_execution_mode == "login");
  assert(login_config.m_login_user && *login_config.m_login_user == "operator");
  auto allow_client_user_path = dir / "allow-client-user.yaml";
  write_text(allow_client_user_path,
             "version: 1\n"
             "server:\n"
             "  executionMode: login\n"
             "  allowClientUser: true\n");
  auto allow_client_user_config = cli::load_server_runtime_config(allow_client_user_path.string());
  assert(allow_client_user_config.m_allow_client_user && *allow_client_user_config.m_allow_client_user);
  auto contradictory_path = dir / "contradictory.yaml";
  write_text(contradictory_path,
             "version: 1\n"
             "server:\n"
             "  rstream: false\n"
             "  serverId: prod-shell\n");
  assert(throws_runtime_error([&contradictory_path]() { cli::load_server_runtime_config(contradictory_path.string()); }));
}

static void check_auth_token_resolution()
{
  auto dir  = temp_dir();
  auto path = dir / "token.txt";
  write_text(path, "\n  local-secret  \n");
  auto token = cli::read_auth_token(path.string());
  assert(token && *token == "local-secret");
  auto empty_path = dir / "empty-token.txt";
  write_text(empty_path, "\n");
  assert(throws_runtime_error([&empty_path]() { cli::read_auth_token(empty_path.string()); }));
}

static void check_client_credential_resolution()
{
  auto dir              = temp_dir();
  auto path             = dir / "client-credential.json";
  const auto credential = std::string("{\"type\":\"test.workspace.credential\",\"v\":1}");
  write_text(path, "\n  " + credential + "  \n");
  auto parsed = cli::read_client_credential(path.string());
  assert(std::string(parsed.begin(), parsed.end()) == credential);
  auto empty = cli::read_client_credential("");
  assert(empty.empty());
  auto empty_path = dir / "empty-credential.json";
  write_text(empty_path, "\n");
  assert(throws_runtime_error([&empty_path]() { cli::read_client_credential(empty_path.string()); }));
}

static void check_endpoint_identity_json()
{
  auto identity = test_endpoint_identity();
  auto raw      = cli::identity_to_json(identity).dump();
  auto parsed   = cli::load_identity_json(raw, cli::identity_env);
  assert(parsed.m_encryption.m_key_id == identity.m_encryption.m_key_id);
  assert(parsed.m_encryption.m_public_key == identity.m_encryption.m_public_key);
  assert(parsed.m_encryption.m_private_key == identity.m_encryption.m_private_key);
  assert(parsed.m_signing.m_key_id == identity.m_signing.m_key_id);
  assert(parsed.m_signing.m_public_key == identity.m_signing.m_public_key);
  assert(parsed.m_signing.m_private_key == identity.m_signing.m_private_key);
  assert(throws_runtime_error([]() { cli::load_identity_json("{", cli::identity_env); }));
}

static void check_authorized_client_key_validation()
{
  auto identity   = test_endpoint_identity();
  auto key_id     = cli::base64url_encode(identity.m_signing.m_key_id);
  auto public_key = cli::base64url_encode(identity.m_signing.m_public_key);
  auto parsed     = cli::parse_authorized_client_key(key_id + ":" + public_key);
  assert(parsed.first == identity.m_signing.m_key_id);
  assert(parsed.second == identity.m_signing.m_public_key);
  auto split = cli::split_comma_separated_values(" " + key_id + ":" + public_key + " , , " + public_key + " ");
  assert(split.size() == 2);
  assert(split[0] == key_id + ":" + public_key);
  assert(split[1] == public_key);
  auto other = test_endpoint_identity();
  assert(throws_runtime_error([&]() {
    cli::parse_authorized_client_key(cli::base64url_encode(other.m_signing.m_key_id) + ":" + public_key);
  }));
}

static void write_known_server_entries_file(const std::filesystem::path& path, const nlohmann::json& entries)
{
  nlohmann::json doc = {
      {"version", cli::key_file_version},
      {"crypto_suite", cli::key_file_crypto_suite},
      {"known_servers", entries},
  };
  write_text(path, doc.dump(2) + "\n");
}

static nlohmann::json known_server_entry_json(const std::string& name, const rstream::webtty::endpoint_identity& identity, const std::string& client_identity = "")
{
  nlohmann::json entry = {
      {"name", name},
      {"key_id", cli::base64url_encode(identity.m_encryption.m_key_id)},
      {"public_key", cli::base64url_encode(identity.m_encryption.m_public_key)},
      {"signing_key_id", cli::base64url_encode(identity.m_signing.m_key_id)},
      {"signing_public_key", cli::base64url_encode(identity.m_signing.m_public_key)},
  };
  if (!client_identity.empty()) {
    entry["client_identity"] = client_identity;
  }
  return entry;
}

static void check_known_server_entries_file()
{
  auto dir      = temp_dir();
  auto path     = dir / "known_servers.json";
  auto identity = test_endpoint_identity();
  write_known_server_entries_file(path, nlohmann::json::array({known_server_entry_json("prod-shell", identity, "operator-laptop")}));
  auto entries = cli::load_known_server_entries_file(path.string());
  assert(entries.size() == 1);
  assert(entries[0].m_name == "prod-shell");
  assert(entries[0].m_recipient.m_key_id == identity.m_encryption.m_key_id);
  assert(entries[0].m_recipient.m_public_key == identity.m_encryption.m_public_key);
  assert(entries[0].m_endpoint_identity);
  assert(entries[0].m_endpoint_identity->m_encryption_key_id == identity.m_encryption.m_key_id);
  assert(entries[0].m_endpoint_identity->m_signing_key_id == identity.m_signing.m_key_id);
  assert(entries[0].m_client_identity == "operator-laptop");
  auto recipients = cli::load_known_servers_file(path.string());
  assert(recipients.size() == 1);
  assert(recipients[0].m_key_id == identity.m_encryption.m_key_id);
  auto endpoint_identities = cli::load_known_server_endpoint_identities_file(path.string());
  assert(endpoint_identities.size() == 1);
  assert(endpoint_identities[0].m_signing_key_id == identity.m_signing.m_key_id);
  auto invalid_path = dir / "invalid_known_servers.json";
  write_known_server_entries_file(invalid_path, nlohmann::json::array({known_server_entry_json("prod-shell", identity, "../bad")}));
  assert(throws_runtime_error([&invalid_path]() { cli::load_known_server_entries_file(invalid_path.string()); }));
}

static void check_enrollment_validation()
{
  auto dir                = temp_dir();
  auto identity           = test_endpoint_identity();
  auto public_key         = cli::base64url_encode(identity.m_encryption.m_public_key);
  auto signing_key_id     = cli::base64url_encode(identity.m_signing.m_key_id);
  auto signing_public_key = cli::base64url_encode(identity.m_signing.m_public_key);
  auto fingerprint        = cli::server_public_key_fingerprint(identity.m_encryption.m_public_key);
  auto path               = dir / "prod-shell.yaml";
  write_text(path,
             "version: 1\n"
             "serverId: prod-shell\n"
             "workspaceId: workspace-1\n"
             "projectId: project-1\n"
             "apiUrl: https://app.example.test\n"
             "identityFile: ~/.rstream/webtty/identities/prod-shell.identity.json\n"
             "serverPublicKey: "
                 + public_key +
                 "\n"
                 "serverSigningKeyId: "
                 + signing_key_id +
                 "\n"
                 "serverSigningPublicKey: "
                 + signing_public_key +
                 "\n"
                 "serverFingerprint: "
                 + fingerprint +
                 "\n"
                 "serverKeyAlgorithm: webtty-x25519-hpke-v1\n"
                 "encryptionPolicy: explicit_key\n"
                 "enrollmentStatus: active\n");
  auto enrollment = cli::load_server_enrollment(path.string());
  assert(enrollment.m_server_id == "prod-shell");
  assert(cli::enrollment_requires_e2e(enrollment));
  require_no_runtime_error("explicit enrollment identity validation", [&]() { cli::validate_identity_matches_enrollment(identity, enrollment); });
  auto admission_enrollment                = enrollment;
  admission_enrollment.m_encryption_policy = "disabled";
  rstream::webtty::webtty_uri_options uri_options;
  uri_options.m_managed           = true;
  uri_options.m_publish           = true;
  uri_options.m_server_id         = admission_enrollment.m_server_id;
  uri_options.m_encryption_policy = admission_enrollment.m_encryption_policy;
  uri_options.m_labels            = {{"env", "prod"}};
  auto admission_labels           = rstream::webtty::build_webtty_labels(uri_options);
  auto admission_label            = cli::create_server_admission_label(admission_enrollment, identity, admission_labels);
  auto admission_raw              = cli::base64url_decode(admission_label, 0, "server admission label");
  auto admission_json             = nlohmann::json::parse(std::string(admission_raw.begin(), admission_raw.end()));
  assert(admission_json.at("workspace_id").get<std::string>() == "workspace-1");
  assert(admission_json.at("project_id").get<std::string>() == "project-1");
  assert(admission_json.at("server_id").get<std::string>() == "prod-shell");
  assert(admission_json.at("labels_sha256").get<std::string>() == cli::base64url_encode(cli::webtty_server_admission_labels_hash(admission_labels)));
  uri_options.m_server_admission_label = admission_label;
  assert(rstream::webtty::build_webtty_uri(uri_options).find("rstream.webtty.server_admission") != std::string::npos);
  auto workspace_managed_path = dir / "workspace-managed.yaml";
  write_text(workspace_managed_path,
             "version: 1\n"
             "serverId: prod-shell\n"
             "workspaceId: workspace-1\n"
             "projectId: project-1\n"
             "identityFile: ~/.rstream/webtty/identities/prod-shell.identity.json\n"
             "serverPublicKey: "
                 + public_key +
                 "\n"
                 "serverSigningKeyId: "
                 + signing_key_id +
                 "\n"
                 "serverSigningPublicKey: "
                 + signing_public_key +
                 "\n"
                 "serverFingerprint: "
                 + fingerprint +
                 "\n"
                 "serverKeyAlgorithm: webtty-x25519-hpke-v1\n"
                 "workspaceTrustKeysetId: keyset-1\n"
                 "workspaceTrustKeysetFingerprint: sha256:test-keyset\n"
                 "workspaceTrustPublicSigningKey: "
                 + signing_public_key +
                 "\n"
                 "encryptionPolicy: workspace_managed\n"
                 "enrollmentStatus: active\n");
  auto workspace_managed = cli::load_server_enrollment(workspace_managed_path.string());
  assert(workspace_managed.m_workspace_trust_keyset_id == "keyset-1");
  assert(cli::enrollment_requires_e2e(workspace_managed));
  auto missing_trust_path = dir / "workspace-managed-missing-trust.yaml";
  write_text(missing_trust_path,
             "version: 1\n"
             "serverId: prod-shell\n"
             "workspaceId: workspace-1\n"
             "projectId: project-1\n"
             "identityFile: ~/.rstream/webtty/identities/prod-shell.identity.json\n"
             "serverPublicKey: "
                 + public_key +
                 "\n"
                 "serverSigningKeyId: "
                 + signing_key_id +
                 "\n"
                 "serverSigningPublicKey: "
                 + signing_public_key +
                 "\n"
                 "serverFingerprint: "
                 + fingerprint +
                 "\n"
                 "serverKeyAlgorithm: webtty-x25519-hpke-v1\n"
                 "encryptionPolicy: workspace_managed\n");
  assert(throws_runtime_error([&missing_trust_path]() { cli::load_server_enrollment(missing_trust_path.string()); }));
  auto disabled_path = dir / "disabled.yaml";
  write_text(disabled_path,
             "version: 1\n"
             "serverId: prod-shell\n"
             "projectId: project-1\n"
             "identityFile: ~/.rstream/webtty/identities/prod-shell.identity.json\n"
             "serverPublicKey: "
                 + public_key +
                 "\n"
                 "serverSigningKeyId: "
                 + signing_key_id +
                 "\n"
                 "serverSigningPublicKey: "
                 + signing_public_key +
                 "\n"
                 "serverFingerprint: "
                 + fingerprint +
                 "\n"
                 "serverKeyAlgorithm: webtty-x25519-hpke-v1\n"
                 "encryptionPolicy: disabled\n");
  auto disabled = cli::load_server_enrollment(disabled_path.string());
  assert(!cli::enrollment_requires_e2e(disabled));
  require_no_runtime_error("disabled enrollment identity validation", [&]() { cli::validate_identity_matches_enrollment(identity, disabled); });
  auto missing_identity_path = dir / "missing.identity.json";
  assert(!std::filesystem::exists(missing_identity_path));
  assert(throws_runtime_error([&missing_identity_path]() { cli::load_identity_file(missing_identity_path.string()); }));
  assert(!std::filesystem::exists(missing_identity_path));
  auto tampered_path = dir / "tampered.yaml";
  write_text(tampered_path,
             "version: 1\n"
             "serverId: prod-shell\n"
             "projectId: project-1\n"
             "identityFile: ~/.rstream/webtty/identities/prod-shell.identity.json\n"
             "serverPublicKey: "
                 + public_key +
                 "\n"
                 "serverFingerprint: sha256:tampered\n"
                 "serverKeyAlgorithm: webtty-x25519-hpke-v1\n");
  assert(throws_runtime_error([&tampered_path]() { cli::load_server_enrollment(tampered_path.string()); }));
}

int main()
{
  check_argv_has();
  check_known_server_key_validation();
  check_auth_token_resolution();
  check_client_credential_resolution();
  check_endpoint_identity_json();
  check_authorized_client_key_validation();
  check_known_server_entries_file();
  check_runtime_config_validation();
  check_enrollment_validation();
  return 0;
}
