// See LICENSE file in the project root for license information.

#pragma once

#include <openssl/sha.h>

#include "webtty_cli.hpp"

namespace rstream {
namespace webtty {
namespace cli {

inline std::string workspace_json_string(const nlohmann::json& value, const std::string& key)
{
  auto it = value.find(key);
  if (it == value.end() || !it->is_string()) {
    return "";
  }
  return it->get<std::string>();
}

inline std::string workspace_canonical_json(const nlohmann::json& value)
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

inline std::string workspace_sha256_base64url(const nlohmann::json& value)
{
  auto canonical                             = workspace_canonical_json(value);
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(canonical.data()), canonical.size(), digest);
  return rstream::webtty::cli::base64url_encode(rstream::webtty::byte_vector(digest, digest + SHA256_DIGEST_LENGTH));
}

inline std::string workspace_public_key_fingerprint(const std::string& public_encryption_key, const std::string& public_signing_key)
{
  nlohmann::json payload = {
      {"public_encryption_key", public_encryption_key},
      {"public_signing_key", public_signing_key},
      {"type", "workspace.public_keys"},
      {"v", 1},
  };
  return "sha256:" + workspace_sha256_base64url(payload);
}

inline void verify_workspace_signature(const std::string& public_signing_key, const nlohmann::json& payload, const std::string& signature, const std::string& label)
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

inline bool workspace_trust_payload_matches(const rstream::webtty::cli::server_enrollment& enrollment, const nlohmann::json& credential, const nlohmann::json& trust_payload)
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

inline boost::optional<rstream::webtty::byte_vector> verify_workspace_client_credential(const rstream::webtty::cli::server_enrollment& enrollment,
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

}  // namespace cli
}  // namespace webtty
}  // namespace rstream
