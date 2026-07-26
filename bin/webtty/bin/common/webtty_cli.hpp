// See LICENSE file in the project root for license information.

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <yaml-cpp/yaml.h>

#include <rstream/webtty/webtty.hpp>

namespace docopt {
struct value;
}

namespace rstream {
namespace webtty {
namespace cli {

constexpr const char* identity_env                   = "RSTREAM_WEBTTY_IDENTITY";
constexpr const char* identity_file_env              = "RSTREAM_WEBTTY_IDENTITY_FILE";
constexpr const char* authorized_client_keys_env     = "RSTREAM_WEBTTY_AUTHORIZED_CLIENT_KEYS";
constexpr const char* authorized_clients_file_env    = "RSTREAM_WEBTTY_AUTHORIZED_CLIENTS_FILE";
constexpr const char* known_server_key_env           = "RSTREAM_WEBTTY_KNOWN_SERVER_KEY";
constexpr const char* known_servers_file_env         = "RSTREAM_WEBTTY_KNOWN_SERVERS_FILE";
constexpr const char* client_credential_file_env     = "RSTREAM_WEBTTY_CLIENT_CREDENTIAL_FILE";
constexpr const char* webtty_config_env              = "RSTREAM_WEBTTY_CONFIG";
constexpr const char* auth_token_env                 = "RSTREAM_WEBTTY_AUTH_TOKEN";
constexpr const char* key_file_crypto_suite          = "webtty-e2e-x25519-hpke-aes-256-gcm-v1";
constexpr const char* endpoint_identity_crypto_suite = "webtty-endpoint-x25519-ecdsa-p256-v1";
constexpr const int key_file_version                 = 1;

struct server_enrollment {
  int m_version = 0;
  std::string m_server_id;
  std::string m_workspace_id;
  std::string m_project_id;
  std::string m_api_url;
  std::string m_identity_file;
  std::string m_server_public_key;
  std::string m_server_signing_key_id;
  std::string m_server_signing_public_key;
  std::string m_server_fingerprint;
  std::string m_server_key_algorithm;
  std::string m_workspace_trust_keyset_id;
  std::string m_workspace_trust_keyset_fingerprint;
  std::string m_workspace_trust_public_signing_key;
  std::string m_encryption_policy;
  std::string m_enrollment_status;
};

struct server_runtime_config {
  std::optional<std::string> m_uri;
  std::optional<bool> m_web;
  std::optional<bool> m_publish;
  std::optional<std::string> m_server_id;
  std::optional<std::string> m_server_enrollment;
  std::optional<std::string> m_transport;
  std::optional<std::string> m_execution_mode;
  std::optional<std::string> m_login_user;
  std::optional<bool> m_allow_client_user;
  std::optional<std::string> m_auth_token_file;
  std::optional<bool> m_allow_unauthenticated;
  std::optional<bool> m_e2e;
  std::optional<std::string> m_identity;
  std::optional<std::string> m_identity_file;
  std::optional<std::string> m_authorized_clients_file;
  std::map<std::string, std::string> m_labels;
};

struct known_server_entry {
  std::string m_name;
  e2e_recipient m_recipient;
  std::optional<endpoint_identity_public> m_endpoint_identity;
  std::string m_client_identity;
};

inline std::string trim_copy(const std::string& value)
{
  auto begin = value.begin();
  auto end   = value.end();
  while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  while (begin != end) {
    auto last = end;
    --last;
    if (!std::isspace(static_cast<unsigned char>(*last))) {
      break;
    }
    end = last;
  }
  return std::string(begin, end);
}

inline std::string getenv_trimmed(const char* name)
{
  auto value = std::getenv(name);
  return value == nullptr ? "" : trim_copy(value);
}

inline std::vector<std::string> split_comma_separated_values(const std::string& raw)
{
  std::vector<std::string> values;
  std::size_t start = 0;
  while (start < raw.size()) {
    auto pos   = raw.find(',', start);
    auto value = trim_copy(raw.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
    if (!value.empty()) {
      values.push_back(value);
    }
    if (pos == std::string::npos) {
      break;
    }
    start = pos + 1;
  }
  return values;
}

inline std::vector<std::string> getenv_comma_separated_values(const char* name)
{
  return split_comma_separated_values(getenv_trimmed(name));
}

inline std::string home_dir()
{
#ifdef _WIN32
  auto value = getenv_trimmed("USERPROFILE");
#else
  auto value = getenv_trimmed("HOME");
#endif
  if (value.empty()) {
    throw std::runtime_error("failed to resolve user home directory");
  }
  return value;
}

inline std::string expand_path(const std::string& raw)
{
  auto value = trim_copy(raw);
  if (value == "~") {
    return home_dir();
  }
  if (value.rfind("~/", 0) == 0 || value.rfind("~\\", 0) == 0) {
    return (std::filesystem::path(home_dir()) / value.substr(2)).string();
  }
  return value;
}

inline std::string read_trimmed_file(const std::string& raw_path, const std::string& description)
{
  auto path = expand_path(raw_path);
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to read " + description + ": " + path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  auto value = trim_copy(buffer.str());
  if (value.empty()) {
    throw std::runtime_error(description + " is empty");
  }
  return value;
}

inline std::optional<std::string> read_auth_token(const std::string& raw_file)
{
  auto path = trim_copy(raw_file);
  if (!path.empty()) {
    return read_trimmed_file(path, "WebTTY auth token file");
  }
  auto value = getenv_trimmed(auth_token_env);
  if (!value.empty()) {
    return value;
  }
  return std::nullopt;
}

inline byte_vector read_client_credential(const std::string& raw_file)
{
  auto path = trim_copy(raw_file);
  if (path.empty()) {
    path = getenv_trimmed(client_credential_file_env);
  }
  if (path.empty()) {
    return {};
  }
  auto value = read_trimmed_file(path, "WebTTY client credential file");
  return byte_vector(value.begin(), value.end());
}

inline std::filesystem::path rstream_home()
{
  return std::filesystem::path(home_dir()) / ".rstream";
}

inline void validate_local_name(const std::string& name, const std::string& flag)
{
  auto value = trim_copy(name);
  if (value.empty()) {
    throw std::runtime_error(flag + " is required");
  }
  if (value == "." || value == ".." || value.find('/') != std::string::npos || value.find('\\') != std::string::npos) {
    throw std::runtime_error(flag + " contains unsupported characters");
  }
  if (!std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-';
      })) {
    throw std::runtime_error(flag + " contains unsupported characters");
  }
}

inline std::string default_identity_path(const std::string& name)
{
  validate_local_name(name, "identity name");
  return (rstream_home() / "webtty" / "identities" / (name + ".identity.json")).string();
}

inline std::string default_server_enrollment_path(const std::string& server_id)
{
  validate_local_name(server_id, "--server-id");
  return (rstream_home() / "webtty" / "enrollments" / (server_id + ".yaml")).string();
}

inline std::string default_known_servers_path()
{
  return (rstream_home() / "webtty" / "known_servers.json").string();
}

inline std::string default_authorized_clients_path(const std::string& name)
{
  auto value = trim_copy(name);
  if (value.empty()) {
    value = "default";
  }
  validate_local_name(value, "authorized clients store name");
  return (rstream_home() / "webtty" / "authorized_clients" / (value + ".json")).string();
}

inline void protect_file(const std::filesystem::path& path)
{
#ifndef _WIN32
  ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#else
  (void)path;
#endif
}

inline void protect_dir(const std::filesystem::path& path)
{
#ifndef _WIN32
  ::chmod(path.c_str(), S_IRWXU);
#else
  (void)path;
#endif
}

inline std::string utc_rfc3339(std::chrono::system_clock::time_point value)
{
  auto value_seconds = std::chrono::time_point_cast<std::chrono::seconds>(value);
  auto time          = std::chrono::system_clock::to_time_t(value_seconds);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

inline std::string utc_now_rfc3339()
{
  return utc_rfc3339(std::chrono::system_clock::now());
}

inline std::string base64url_encode(const byte_vector& bytes)
{
  static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  std::size_t i = 0;
  while (i + 3 <= bytes.size()) {
    auto v = (static_cast<unsigned int>(bytes[i]) << 16) | (static_cast<unsigned int>(bytes[i + 1]) << 8) | static_cast<unsigned int>(bytes[i + 2]);
    out.push_back(alphabet[(v >> 18) & 0x3f]);
    out.push_back(alphabet[(v >> 12) & 0x3f]);
    out.push_back(alphabet[(v >> 6) & 0x3f]);
    out.push_back(alphabet[v & 0x3f]);
    i += 3;
  }
  auto remaining = bytes.size() - i;
  if (remaining == 1) {
    auto v = static_cast<unsigned int>(bytes[i]) << 16;
    out.push_back(alphabet[(v >> 18) & 0x3f]);
    out.push_back(alphabet[(v >> 12) & 0x3f]);
  }
  else if (remaining == 2) {
    auto v = (static_cast<unsigned int>(bytes[i]) << 16) | (static_cast<unsigned int>(bytes[i + 1]) << 8);
    out.push_back(alphabet[(v >> 18) & 0x3f]);
    out.push_back(alphabet[(v >> 12) & 0x3f]);
    out.push_back(alphabet[(v >> 6) & 0x3f]);
  }
  return out;
}

inline int base64url_value(char c)
{
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '-') {
    return 62;
  }
  if (c == '_') {
    return 63;
  }
  return -1;
}

inline byte_vector base64url_decode(const std::string& raw, std::size_t expected_size, const std::string& field)
{
  auto value = trim_copy(raw);
  if (value.empty()) {
    throw std::runtime_error(field + " is empty");
  }
  if (value.find('=') != std::string::npos || value.size() % 4 == 1) {
    throw std::runtime_error("invalid " + field);
  }
  byte_vector out;
  std::size_t i = 0;
  while (i + 4 <= value.size()) {
    int a = base64url_value(value[i]);
    int b = base64url_value(value[i + 1]);
    int c = base64url_value(value[i + 2]);
    int d = base64url_value(value[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) {
      throw std::runtime_error("invalid " + field);
    }
    auto v = (static_cast<unsigned int>(a) << 18) | (static_cast<unsigned int>(b) << 12) | (static_cast<unsigned int>(c) << 6) | static_cast<unsigned int>(d);
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(v & 0xff));
    i += 4;
  }
  auto remaining = value.size() - i;
  if (remaining == 2) {
    int a = base64url_value(value[i]);
    int b = base64url_value(value[i + 1]);
    if (a < 0 || b < 0) {
      throw std::runtime_error("invalid " + field);
    }
    auto v = (static_cast<unsigned int>(a) << 18) | (static_cast<unsigned int>(b) << 12);
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
  }
  else if (remaining == 3) {
    int a = base64url_value(value[i]);
    int b = base64url_value(value[i + 1]);
    int c = base64url_value(value[i + 2]);
    if (a < 0 || b < 0 || c < 0) {
      throw std::runtime_error("invalid " + field);
    }
    auto v = (static_cast<unsigned int>(a) << 18) | (static_cast<unsigned int>(b) << 12) | (static_cast<unsigned int>(c) << 6);
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
  }
  if (expected_size > 0 && out.size() != expected_size) {
    throw std::runtime_error(field + " must decode to " + std::to_string(expected_size) + " bytes");
  }
  if (base64url_encode(out) != value) {
    throw std::runtime_error(field + " must be canonical base64url without padding");
  }
  return out;
}

inline std::string server_public_key_fingerprint(const byte_vector& public_key)
{
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(public_key.data(), public_key.size(), digest);
  return "sha256:" + base64url_encode(byte_vector(digest, digest + SHA256_DIGEST_LENGTH));
}

inline void append_uint32_be(byte_vector& dst, std::uint32_t value)
{
  dst.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
  dst.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
  dst.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
  dst.push_back(static_cast<unsigned char>(value & 0xff));
}

inline void append_length_prefixed(byte_vector& dst, const byte_vector& value)
{
  append_uint32_be(dst, static_cast<std::uint32_t>(value.size()));
  dst.insert(dst.end(), value.begin(), value.end());
}

inline void append_length_prefixed(byte_vector& dst, const std::string& value)
{
  append_uint32_be(dst, static_cast<std::uint32_t>(value.size()));
  dst.insert(dst.end(), value.begin(), value.end());
}

inline byte_vector webtty_server_admission_labels_hash(const std::map<std::string, std::string>& labels)
{
  byte_vector canonical;
  const std::string domain = "rstream-webtty-server-admission-labels-v1";
  canonical.insert(canonical.end(), domain.begin(), domain.end());
  std::vector<std::pair<std::string, std::string>> entries;
  for (const auto& label : labels) {
    if (label.first == "rstream.webtty.server_admission") {
      continue;
    }
    entries.push_back(label);
  }
  append_uint32_be(canonical, static_cast<std::uint32_t>(entries.size()));
  for (const auto& entry : entries) {
    append_length_prefixed(canonical, entry.first);
    append_length_prefixed(canonical, entry.second);
  }
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(canonical.data(), canonical.size(), digest);
  return byte_vector(digest, digest + SHA256_DIGEST_LENGTH);
}

inline byte_vector webtty_server_admission_transcript(const nlohmann::json& proof)
{
  byte_vector canonical;
  const std::string domain = "rstream-webtty-server-admission-v1";
  canonical.insert(canonical.end(), domain.begin(), domain.end());
  append_uint32_be(canonical, static_cast<std::uint32_t>(proof.at("version").get<int>()));
  append_length_prefixed(canonical, proof.at("suite").get<std::string>());
  append_length_prefixed(canonical, proof.at("workspace_id").get<std::string>());
  append_length_prefixed(canonical, proof.at("project_id").get<std::string>());
  append_length_prefixed(canonical, proof.at("server_id").get<std::string>());
  append_length_prefixed(canonical, proof.at("tunnel_protocol").get<std::string>());
  append_length_prefixed(canonical, proof.at("tunnel_type").get<std::string>());
  append_length_prefixed(canonical, proof.at("labels_sha256").get<std::string>());
  append_length_prefixed(canonical, proof.at("signing_key_id").get<std::string>());
  append_length_prefixed(canonical, proof.at("issued_at").get<std::string>());
  append_length_prefixed(canonical, proof.at("expires_at").get<std::string>());
  append_length_prefixed(canonical, proof.at("nonce").get<std::string>());
  return canonical;
}

inline byte_vector sign_webtty_server_admission_digest(const endpoint_identity& identity, const byte_vector& digest)
{
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    throw std::runtime_error("WebTTY server admission digest is invalid");
  }
  const unsigned char* raw = identity.m_signing.m_private_key.data();
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(d2i_AutoPrivateKey(nullptr, &raw, static_cast<long>(identity.m_signing.m_private_key.size())), EVP_PKEY_free);
  if (!key || EVP_PKEY_id(key.get()) != EVP_PKEY_EC) {
    throw std::runtime_error("WebTTY server admission signing key is invalid");
  }
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  if (!ctx || EVP_PKEY_sign_init(ctx.get()) <= 0 || EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0) {
    throw std::runtime_error("WebTTY server admission signing failed");
  }
  std::size_t size = 0;
  if (EVP_PKEY_sign(ctx.get(), nullptr, &size, digest.data(), digest.size()) <= 0 || size == 0) {
    throw std::runtime_error("WebTTY server admission signing failed");
  }
  byte_vector signature(size);
  if (EVP_PKEY_sign(ctx.get(), signature.data(), &size, digest.data(), digest.size()) <= 0) {
    throw std::runtime_error("WebTTY server admission signing failed");
  }
  signature.resize(size);
  return signature;
}

inline std::string create_server_admission_label(const server_enrollment& enrollment, const endpoint_identity& identity, const std::map<std::string, std::string>& labels)
{
  auto now     = std::chrono::system_clock::now();
  auto issued  = utc_rfc3339(now);
  auto expires = utc_rfc3339(now + std::chrono::seconds(90));
  byte_vector nonce(16);
  if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
    throw std::runtime_error("failed to generate WebTTY server admission nonce");
  }
  nlohmann::json proof = {
      {"version", 1},
      {"suite", "webtty-server-admission-ecdsa-p256-sha256-v1"},
      {"workspace_id", enrollment.m_workspace_id},
      {"project_id", enrollment.m_project_id},
      {"server_id", enrollment.m_server_id},
      {"tunnel_protocol", "webtty"},
      {"tunnel_type", "bytestream"},
      {"labels_sha256", base64url_encode(webtty_server_admission_labels_hash(labels))},
      {"signing_key_id", base64url_encode(identity.m_signing.m_key_id)},
      {"issued_at", issued},
      {"expires_at", expires},
      {"nonce", base64url_encode(nonce)},
  };
  auto transcript                            = webtty_server_admission_transcript(proof);
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(transcript.data(), transcript.size(), digest);
  proof["signature"] = base64url_encode(sign_webtty_server_admission_digest(identity, byte_vector(digest, digest + SHA256_DIGEST_LENGTH)));
  auto serialized    = proof.dump();
  return base64url_encode(byte_vector(serialized.begin(), serialized.end()));
}

inline endpoint_identity identity_from_json(const nlohmann::json& json)
{
  if (!json.is_object()) {
    throw std::runtime_error("WebTTY identity JSON must be an object");
  }
  if (json.value("version", 0) != key_file_version) {
    throw std::runtime_error("unsupported WebTTY identity version");
  }
  if (json.value("crypto_suite", std::string()) != endpoint_identity_crypto_suite) {
    throw std::runtime_error("unsupported WebTTY identity crypto suite");
  }
  endpoint_identity identity;
  identity.m_encryption.m_key_id      = base64url_decode(json.at("encryption_key_id").get<std::string>(), 16, "WebTTY endpoint encryption key id");
  identity.m_encryption.m_public_key  = base64url_decode(json.at("encryption_public_key").get<std::string>(), 32, "WebTTY endpoint encryption public key");
  identity.m_encryption.m_private_key = base64url_decode(json.at("encryption_private_key").get<std::string>(), 32, "WebTTY endpoint encryption private key");
  e2e_identity normalized;
  std::error_code error_code;
  e2e_identity_from_private_key(normalized, identity.m_encryption.m_private_key, error_code);
  if (error_code || normalized.m_public_key != identity.m_encryption.m_public_key || normalized.m_key_id != identity.m_encryption.m_key_id) {
    throw std::runtime_error("WebTTY endpoint encryption public key does not match private key");
  }
  identity.m_signing.m_key_id      = base64url_decode(json.at("signing_key_id").get<std::string>(), 32, "WebTTY endpoint signing key id");
  identity.m_signing.m_public_key  = base64url_decode(json.at("signing_public_key").get<std::string>(), 0, "WebTTY endpoint signing public key");
  identity.m_signing.m_private_key = base64url_decode(json.at("signing_private_key").get<std::string>(), 0, "WebTTY endpoint signing private key");
  signing_identity normalized_signing;
  signing_identity_from_private_key(normalized_signing, identity.m_signing.m_private_key, error_code);
  if (error_code || normalized_signing.m_public_key != identity.m_signing.m_public_key || normalized_signing.m_key_id != identity.m_signing.m_key_id) {
    throw std::runtime_error("WebTTY endpoint signing public key does not match private key");
  }
  return identity;
}

inline nlohmann::json identity_to_json(const endpoint_identity& identity)
{
  return {
      {"version", key_file_version},
      {"crypto_suite", endpoint_identity_crypto_suite},
      {"encryption_key_id", base64url_encode(identity.m_encryption.m_key_id)},
      {"encryption_public_key", base64url_encode(identity.m_encryption.m_public_key)},
      {"encryption_private_key", base64url_encode(identity.m_encryption.m_private_key)},
      {"signing_key_id", base64url_encode(identity.m_signing.m_key_id)},
      {"signing_public_key", base64url_encode(identity.m_signing.m_public_key)},
      {"signing_private_key", base64url_encode(identity.m_signing.m_private_key)},
      {"created_at", utc_now_rfc3339()},
  };
}

inline endpoint_identity load_identity_json(const std::string& raw, const std::string& description)
{
  try {
    return identity_from_json(nlohmann::json::parse(raw));
  }
  catch (const nlohmann::json::exception& e) {
    throw std::runtime_error("failed to parse " + description + ": " + std::string(e.what()));
  }
}

inline endpoint_identity load_identity_file(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to read WebTTY identity file: " + path);
  }
  nlohmann::json json;
  file >> json;
  return identity_from_json(json);
}

inline void write_identity_file(const std::string& raw_path, const endpoint_identity& identity)
{
  auto path = std::filesystem::path(expand_path(raw_path));
  std::filesystem::create_directories(path.parent_path());
  protect_dir(path.parent_path());
  auto json = identity_to_json(identity);
  auto tmp  = path;
  tmp += ".tmp";
  {
    std::ofstream file(tmp, std::ios::trunc);
    if (!file.is_open()) {
      throw std::runtime_error("failed to write WebTTY identity file: " + tmp.string());
    }
    file << json.dump(2) << "\n";
  }
  protect_file(tmp);
  std::filesystem::rename(tmp, path);
  protect_file(path);
}

inline endpoint_identity load_or_create_identity_file(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  if (std::filesystem::exists(path)) {
    return load_identity_file(path);
  }
  std::error_code error_code;
  endpoint_identity identity;
  generate_endpoint_identity(identity, error_code);
  if (error_code) {
    throw std::runtime_error("failed to generate WebTTY identity");
  }
  write_identity_file(path, identity);
  return identity;
}

inline endpoint_identity load_identity_file_or_create(const std::string& identity_file)
{
  return load_or_create_identity_file(identity_file);
}

inline endpoint_identity_public parse_known_server_endpoint_identity(const std::string& raw)
{
  auto value = trim_copy(raw);
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    auto pos = value.find(':', start);
    if (pos == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, pos - start));
    start = pos + 1;
  }
  if (parts.size() != 4) {
    throw std::runtime_error("known WebTTY server endpoint identity must be encryption_key_id:encryption_public_key:signing_key_id:signing_public_key");
  }
  endpoint_identity_public identity;
  identity.m_encryption_key_id     = base64url_decode(parts[0], 16, "known WebTTY server encryption key id");
  identity.m_encryption_public_key = base64url_decode(parts[1], 32, "known WebTTY server encryption public key");
  identity.m_signing_key_id        = base64url_decode(parts[2], 32, "known WebTTY server signing key id");
  identity.m_signing_public_key    = base64url_decode(parts[3], 0, "known WebTTY server signing public key");
  byte_vector expected_encryption;
  byte_vector expected_signing;
  std::error_code error_code;
  e2e_key_id(expected_encryption, identity.m_encryption_public_key, error_code);
  if (error_code || expected_encryption != identity.m_encryption_key_id) {
    throw std::runtime_error("known WebTTY server encryption key id does not match public key");
  }
  signing_key_id(expected_signing, identity.m_signing_public_key, error_code);
  if (error_code || expected_signing != identity.m_signing_key_id) {
    throw std::runtime_error("known WebTTY server signing key id does not match public key");
  }
  return identity;
}

inline std::string endpoint_identity_string(const endpoint_identity_public& identity)
{
  return base64url_encode(identity.m_encryption_key_id) + ":" + base64url_encode(identity.m_encryption_public_key) + ":" + base64url_encode(identity.m_signing_key_id) + ":" + base64url_encode(identity.m_signing_public_key);
}

inline std::pair<byte_vector, byte_vector> parse_authorized_client_key(const std::string& raw)
{
  auto value = trim_copy(raw);
  if (std::count(value.begin(), value.end(), ':') == 3) {
    auto identity = parse_known_server_endpoint_identity(value);
    return std::make_pair(identity.m_signing_key_id, identity.m_signing_public_key);
  }
  auto pos = value.find(':');
  byte_vector key_id;
  byte_vector public_key;
  if (pos == std::string::npos) {
    public_key = base64url_decode(value, 0, "authorized WebTTY client signing public key");
    std::error_code error_code;
    signing_key_id(key_id, public_key, error_code);
    if (error_code) {
      throw std::runtime_error("invalid authorized WebTTY client signing public key");
    }
  }
  else {
    key_id     = base64url_decode(value.substr(0, pos), 32, "authorized WebTTY client signing key id");
    public_key = base64url_decode(value.substr(pos + 1), 0, "authorized WebTTY client signing public key");
    byte_vector expected;
    std::error_code error_code;
    signing_key_id(expected, public_key, error_code);
    if (error_code || expected != key_id) {
      throw std::runtime_error("authorized WebTTY client signing key id does not match public key");
    }
  }
  return std::make_pair(key_id, public_key);
}

inline e2e_recipient parse_known_server_key(const std::string& raw)
{
  auto value = trim_copy(raw);
  if (std::count(value.begin(), value.end(), ':') == 3) {
    auto identity = parse_known_server_endpoint_identity(value);
    e2e_recipient recipient;
    recipient.m_key_id     = identity.m_encryption_key_id;
    recipient.m_public_key = identity.m_encryption_public_key;
    return recipient;
  }
  auto pos = value.find(':');
  e2e_recipient recipient;
  if (pos == std::string::npos) {
    recipient.m_public_key = base64url_decode(value, 32, "known WebTTY server public key");
    std::error_code error_code;
    e2e_key_id(recipient.m_key_id, recipient.m_public_key, error_code);
    if (error_code) {
      throw std::runtime_error("invalid known WebTTY server public key");
    }
    return recipient;
  }
  recipient.m_key_id     = base64url_decode(value.substr(0, pos), 16, "known WebTTY server key id");
  recipient.m_public_key = base64url_decode(value.substr(pos + 1), 32, "known WebTTY server public key");
  byte_vector expected;
  std::error_code error_code;
  e2e_key_id(expected, recipient.m_public_key, error_code);
  if (error_code || expected != recipient.m_key_id) {
    throw std::runtime_error("known WebTTY server key id does not match public key");
  }
  return recipient;
}

inline known_server_entry known_server_entry_from_json(const nlohmann::json& entry, const std::string& description)
{
  known_server_entry parsed;
  parsed.m_name = trim_copy(entry.at("name").get<std::string>());
  validate_local_name(parsed.m_name, description + " name");
  parsed.m_recipient = parse_known_server_key(entry.at("key_id").get<std::string>() + ":" + entry.at("public_key").get<std::string>());
  if (entry.contains("signing_key_id") || entry.contains("signing_public_key")) {
    if (!entry.contains("signing_key_id") || !entry.contains("signing_public_key")) {
      throw std::runtime_error(description + " signing key id and public key must be set together");
    }
    auto identity = parse_known_server_endpoint_identity(
        entry.at("key_id").get<std::string>() + ":" + entry.at("public_key").get<std::string>() + ":" + entry.at("signing_key_id").get<std::string>() + ":" + entry.at("signing_public_key").get<std::string>());
    parsed.m_recipient.m_key_id     = identity.m_encryption_key_id;
    parsed.m_recipient.m_public_key = identity.m_encryption_public_key;
    parsed.m_endpoint_identity      = identity;
  }
  if (entry.contains("client_identity")) {
    parsed.m_client_identity = trim_copy(entry.at("client_identity").get<std::string>());
    if (!parsed.m_client_identity.empty()) {
      validate_local_name(parsed.m_client_identity, description + " client identity");
    }
  }
  return parsed;
}

inline std::vector<known_server_entry> load_known_server_entries_file(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to read known WebTTY servers file: " + path);
  }
  nlohmann::json json;
  file >> json;
  if (json.value("version", 0) != key_file_version) {
    throw std::runtime_error("unsupported known WebTTY servers version");
  }
  if (json.value("crypto_suite", std::string()) != key_file_crypto_suite) {
    throw std::runtime_error("unsupported known WebTTY servers crypto suite");
  }
  std::vector<known_server_entry> entries;
  auto index = 0;
  for (const auto& entry : json.at("known_servers")) {
    entries.push_back(known_server_entry_from_json(entry, "known WebTTY server " + std::to_string(index)));
    ++index;
  }
  return entries;
}

inline std::vector<endpoint_identity_public> load_known_server_endpoint_identities_file(const std::string& raw_path)
{
  std::vector<endpoint_identity_public> identities;
  for (const auto& entry : load_known_server_entries_file(raw_path)) {
    if (entry.m_endpoint_identity) {
      identities.push_back(*entry.m_endpoint_identity);
    }
  }
  return identities;
}

inline std::vector<e2e_recipient> load_known_servers_file(const std::string& raw_path)
{
  std::vector<e2e_recipient> recipients;
  for (const auto& entry : load_known_server_entries_file(raw_path)) {
    recipients.push_back(entry.m_recipient);
  }
  return recipients;
}

inline std::map<std::string, byte_vector> load_authorized_clients_file(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  std::ifstream file(path);
  if (!file.is_open()) {
    return {};
  }
  nlohmann::json json;
  file >> json;
  if (json.value("version", 0) != key_file_version) {
    throw std::runtime_error("unsupported authorized WebTTY clients version");
  }
  if (json.value("crypto_suite", std::string()) != key_file_crypto_suite) {
    throw std::runtime_error("unsupported authorized WebTTY clients crypto suite");
  }
  std::map<std::string, byte_vector> keys;
  for (const auto& entry : json.at("authorized_clients")) {
    auto parsed                                                 = parse_authorized_client_key(entry.at("signing_key_id").get<std::string>() + ":" + entry.at("signing_public_key").get<std::string>());
    keys[std::string(parsed.first.begin(), parsed.first.end())] = parsed.second;
  }
  return keys;
}

inline std::optional<std::string> yaml_string(const YAML::Node& node, const char* key)
{
  auto child = node[key];
  if (!child) {
    return std::nullopt;
  }
  return trim_copy(child.as<std::string>());
}

inline std::optional<bool> yaml_bool(const YAML::Node& node, const char* key)
{
  auto child = node[key];
  if (!child) {
    return std::nullopt;
  }
  return child.as<bool>();
}

inline void ensure_known_fields(const YAML::Node& node, const std::set<std::string>& known, const std::string& section)
{
  if (!node || !node.IsMap()) {
    return;
  }
  for (const auto& item : node) {
    auto key = item.first.as<std::string>();
    if (known.count(key) == 0) {
      std::string message = "unsupported WebTTY ";
      message.append(section).append(" field: ").append(key);
      throw std::runtime_error(message);
    }
  }
}

inline server_enrollment load_server_enrollment(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  auto root = YAML::LoadFile(path);
  server_enrollment enrollment;
  enrollment.m_version = root["version"].as<int>();
  if (enrollment.m_version != 1) {
    throw std::runtime_error("unsupported WebTTY server enrollment version");
  }
  enrollment.m_server_id                          = root["serverId"].as<std::string>();
  enrollment.m_workspace_id                       = root["workspaceId"] ? root["workspaceId"].as<std::string>() : "";
  enrollment.m_project_id                         = root["projectId"].as<std::string>();
  enrollment.m_api_url                            = root["apiUrl"] ? root["apiUrl"].as<std::string>() : "";
  enrollment.m_identity_file                      = root["identityFile"].as<std::string>();
  enrollment.m_server_public_key                  = root["serverPublicKey"].as<std::string>();
  enrollment.m_server_signing_key_id              = root["serverSigningKeyId"] ? root["serverSigningKeyId"].as<std::string>() : "";
  enrollment.m_server_signing_public_key          = root["serverSigningPublicKey"] ? root["serverSigningPublicKey"].as<std::string>() : "";
  enrollment.m_server_fingerprint                 = root["serverFingerprint"].as<std::string>();
  enrollment.m_server_key_algorithm               = root["serverKeyAlgorithm"].as<std::string>();
  enrollment.m_workspace_trust_keyset_id          = root["workspaceTrustKeysetId"] ? root["workspaceTrustKeysetId"].as<std::string>() : "";
  enrollment.m_workspace_trust_keyset_fingerprint = root["workspaceTrustKeysetFingerprint"] ? root["workspaceTrustKeysetFingerprint"].as<std::string>() : "";
  enrollment.m_workspace_trust_public_signing_key = root["workspaceTrustPublicSigningKey"] ? root["workspaceTrustPublicSigningKey"].as<std::string>() : "";
  enrollment.m_encryption_policy                  = root["encryptionPolicy"] ? root["encryptionPolicy"].as<std::string>() : "";
  enrollment.m_enrollment_status                  = root["enrollmentStatus"] ? root["enrollmentStatus"].as<std::string>() : "";
  validate_local_name(enrollment.m_server_id, "serverId");
  if (enrollment.m_project_id.empty() || enrollment.m_identity_file.empty()) {
    throw std::runtime_error("WebTTY server enrollment is incomplete");
  }
  if (enrollment.m_server_key_algorithm != "webtty-x25519-hpke-v1") {
    throw std::runtime_error("unsupported WebTTY server key algorithm");
  }
  auto public_key = base64url_decode(enrollment.m_server_public_key, 32, "WebTTY server public key");
  if (server_public_key_fingerprint(public_key) != enrollment.m_server_fingerprint) {
    throw std::runtime_error("WebTTY server enrollment fingerprint does not match public key");
  }
  if (!enrollment.m_server_signing_key_id.empty() || !enrollment.m_server_signing_public_key.empty()) {
    auto signing_key_id_value = base64url_decode(enrollment.m_server_signing_key_id, 32, "WebTTY server signing key id");
    auto signing_public_key   = base64url_decode(enrollment.m_server_signing_public_key, 0, "WebTTY server signing public key");
    byte_vector expected;
    std::error_code error_code;
    signing_key_id(expected, signing_public_key, error_code);
    if (error_code || expected != signing_key_id_value) {
      throw std::runtime_error("WebTTY server enrollment signing key id does not match public key");
    }
  }
  if (enrollment.m_encryption_policy == "workspace_managed") {
    if (enrollment.m_workspace_id.empty() || enrollment.m_workspace_trust_keyset_id.empty() || enrollment.m_workspace_trust_keyset_fingerprint.empty() || enrollment.m_workspace_trust_public_signing_key.empty()) {
      throw std::runtime_error("workspace-managed WebTTY server enrollment is missing workspace trust pins");
    }
    if (enrollment.m_workspace_trust_keyset_fingerprint.rfind("sha256:", 0) != 0) {
      throw std::runtime_error("workspace-managed WebTTY keyset fingerprint is invalid");
    }
    base64url_decode(enrollment.m_workspace_trust_public_signing_key, 0, "workspace-managed WebTTY keyset public signing key");
  }
  return enrollment;
}

inline bool enrollment_requires_e2e(const server_enrollment& enrollment)
{
  return enrollment.m_encryption_policy == "explicit_key" || enrollment.m_encryption_policy == "workspace_managed";
}

inline void validate_identity_matches_enrollment(const endpoint_identity& identity, const server_enrollment& enrollment)
{
  auto public_key = base64url_decode(enrollment.m_server_public_key, 32, "WebTTY server public key");
  if (identity.m_encryption.m_public_key != public_key || server_public_key_fingerprint(identity.m_encryption.m_public_key) != enrollment.m_server_fingerprint) {
    throw std::runtime_error("WebTTY server identity does not match registered server enrollment");
  }
  if (!enrollment.m_server_signing_key_id.empty() || !enrollment.m_server_signing_public_key.empty()) {
    auto signing_key_id_value = base64url_decode(enrollment.m_server_signing_key_id, 32, "WebTTY server signing key id");
    auto signing_public_key   = base64url_decode(enrollment.m_server_signing_public_key, 0, "WebTTY server signing public key");
    if (identity.m_signing.m_key_id != signing_key_id_value || identity.m_signing.m_public_key != signing_public_key) {
      throw std::runtime_error("WebTTY server identity does not match registered server enrollment");
    }
  }
  else {
    throw std::runtime_error("WebTTY server identity does not match registered server enrollment");
  }
}

inline server_runtime_config load_server_runtime_config(const std::string& raw_path)
{
  auto path = expand_path(raw_path);
  auto root = YAML::LoadFile(path);
  ensure_known_fields(root, {"version", "server", "e2e"}, "runtime config");
  if (root["version"] && root["version"].as<int>() != 1) {
    throw std::runtime_error("unsupported WebTTY runtime config version");
  }
  server_runtime_config config;
  auto server = root["server"];
  if (server) {
    ensure_known_fields(server, {"rstream", "publish", "listen", "serverId", "serverEnrollment", "transport", "executionMode", "labels", "loginUser", "allowClientUser", "authTokenFile", "allowUnauthenticated"}, "runtime config server");
    config.m_uri                   = yaml_string(server, "listen");
    config.m_web                   = yaml_bool(server, "rstream");
    config.m_publish               = yaml_bool(server, "publish");
    config.m_server_id             = yaml_string(server, "serverId");
    config.m_server_enrollment     = yaml_string(server, "serverEnrollment");
    config.m_transport             = yaml_string(server, "transport");
    config.m_execution_mode        = yaml_string(server, "executionMode");
    config.m_login_user            = yaml_string(server, "loginUser");
    config.m_allow_client_user     = yaml_bool(server, "allowClientUser");
    config.m_auth_token_file       = yaml_string(server, "authTokenFile");
    config.m_allow_unauthenticated = yaml_bool(server, "allowUnauthenticated");
    auto labels                    = server["labels"];
    if (labels) {
      for (const auto& item : labels) {
        config.m_labels[item.first.as<std::string>()] = item.second.as<std::string>();
      }
    }
  }
  auto e2e = root["e2e"];
  if (e2e) {
    ensure_known_fields(e2e, {"enabled", "identity", "identityFile", "authorizedClientsFile"}, "runtime config e2e");
    config.m_e2e                     = yaml_bool(e2e, "enabled");
    config.m_identity                = yaml_string(e2e, "identity");
    config.m_identity_file           = yaml_string(e2e, "identityFile");
    config.m_authorized_clients_file = yaml_string(e2e, "authorizedClientsFile");
  }
  if (config.m_web && !*config.m_web && (config.m_server_id || config.m_server_enrollment)) {
    throw std::runtime_error("serverId and serverEnrollment imply rstream mode");
  }
  return config;
}

inline std::string config_path_from_arg_or_env(const std::string& raw_arg)
{
  auto value = trim_copy(raw_arg);
  if (!value.empty()) {
    return expand_path(value);
  }
  value = getenv_trimmed(webtty_config_env);
  return value.empty() ? "" : expand_path(value);
}

inline bool argv_has(int argc, char** argv, const std::string& long_name, const std::string& short_name = "")
{
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == long_name || arg.rfind(long_name + "=", 0) == 0) {
      return true;
    }
    if (!short_name.empty() && arg == short_name) {
      return true;
    }
  }
  return false;
}

inline std::string optional_value(bool present, const std::string& value)
{
  return present ? trim_copy(value) : "";
}

}  // namespace cli
}  // namespace webtty
}  // namespace rstream
