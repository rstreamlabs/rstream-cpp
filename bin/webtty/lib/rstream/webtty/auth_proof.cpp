// See LICENSE file in the project root for license information.

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "error.hpp"
#include "webtty.hpp"

namespace rstream {
namespace webtty {

namespace {

constexpr const char* client_auth_transcript_domain = "rstream-webtty-client-auth-v1";
constexpr const char* server_auth_transcript_domain = "rstream-webtty-server-auth-v1";
constexpr const char* signing_key_id_domain         = "rstream-webtty-signing-key-id-v1";

struct evp_pkey_deleter {
  void operator()(EVP_PKEY* value) const
  {
    EVP_PKEY_free(value);
  }
};

struct evp_pkey_ctx_deleter {
  void operator()(EVP_PKEY_CTX* value) const
  {
    EVP_PKEY_CTX_free(value);
  }
};

struct evp_md_ctx_deleter {
  void operator()(EVP_MD_CTX* value) const
  {
    EVP_MD_CTX_free(value);
  }
};

using evp_pkey_ptr     = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;
using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter>;
using evp_md_ctx_ptr   = std::unique_ptr<EVP_MD_CTX, evp_md_ctx_deleter>;

const unsigned char* data_ptr(const byte_vector& value)
{
  return value.empty() ? nullptr : value.data();
}

void append(byte_vector& dst, const byte_vector& src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

void append(byte_vector& dst, const std::string& src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

std::string trim_copy(const std::string& value)
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

byte_vector bytes_from_string(const std::string& value)
{
  return byte_vector(value.begin(), value.end());
}

byte_vector be32(std::uint32_t value)
{
  return {
      static_cast<unsigned char>((value >> 24) & 0xff),
      static_cast<unsigned char>((value >> 16) & 0xff),
      static_cast<unsigned char>((value >> 8) & 0xff),
      static_cast<unsigned char>(value & 0xff),
  };
}

void append_uint32(byte_vector& dst, std::uint32_t value)
{
  append(dst, be32(value));
}

void append_length_prefixed(byte_vector& dst, const byte_vector& value)
{
  append_uint32(dst, static_cast<std::uint32_t>(value.size()));
  append(dst, value);
}

void append_length_prefixed(byte_vector& dst, const std::string& value)
{
  append_length_prefixed(dst, bytes_from_string(value));
}

void append_length_prefixed_string(byte_vector& dst, const std::string& value)
{
  append_length_prefixed(dst, trim_copy(value));
}

std::uint32_t payload_suite_code(payload_cipher_suite suite, std::error_code& error_code)
{
  switch (suite) {
    case payload_cipher_suite::aes_256_gcm:
      return 1;
    case payload_cipher_suite::chacha20_poly1305:
      return 2;
    default:
      error_code = error::code::crypto_error;
      return 0;
  }
}

std::uint32_t key_envelope_suite_code(key_envelope_suite suite, std::error_code& error_code)
{
  switch (suite) {
    case key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm:
      return 1;
    case key_envelope_suite::hpke_x25519_hkdf_sha256_chacha20_poly1305:
      return 2;
    default:
      error_code = error::code::crypto_error;
      return 0;
  }
}

std::uint32_t signature_suite_code(signature_suite suite, std::error_code& error_code)
{
  switch (suite) {
    case signature_suite::ecdsa_p256_sha256:
      return 1;
    default:
      error_code = error::code::crypto_error;
      return 0;
  }
}

std::uint32_t protocol_version_code(protocol_version version, std::error_code& error_code)
{
  switch (version) {
    case protocol_version::webtty_1:
      return 1;
    default:
      error_code = error::code::protocol_error;
      return 0;
  }
}

std::uint32_t auth_requirement_code(auth_requirement requirement, std::error_code& error_code)
{
  switch (requirement) {
    case auth_requirement::none:
      return 1;
    case auth_requirement::client_proof:
      return 2;
    default:
      error_code = error::code::protocol_error;
      return 0;
  }
}

void append_payload_suites(byte_vector& dst, const std::vector<payload_cipher_suite>& values, std::error_code& error_code)
{
  append_uint32(dst, static_cast<std::uint32_t>(values.size()));
  for (auto value : values) {
    append_uint32(dst, payload_suite_code(value, error_code));
  }
}

void append_key_envelope_suites(byte_vector& dst, const std::vector<key_envelope_suite>& values, std::error_code& error_code)
{
  append_uint32(dst, static_cast<std::uint32_t>(values.size()));
  for (auto value : values) {
    append_uint32(dst, key_envelope_suite_code(value, error_code));
  }
}

void append_signature_suites(byte_vector& dst, const std::vector<signature_suite>& values, std::error_code& error_code)
{
  append_uint32(dst, static_cast<std::uint32_t>(values.size()));
  for (auto value : values) {
    append_uint32(dst, signature_suite_code(value, error_code));
  }
}

void sha256(byte_vector& dst, const byte_vector& input)
{
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(data_ptr(input), input.size(), digest.data());
  dst.assign(digest.begin(), digest.end());
}

void sha256(byte_vector& dst, const std::string& domain, const byte_vector& input)
{
  evp_md_ctx_ptr ctx(EVP_MD_CTX_new());
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  unsigned int digest_size = 0;
  if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 || EVP_DigestUpdate(ctx.get(), domain.data(), domain.size()) != 1) {
    dst.clear();
    return;
  }
  if (!input.empty() && EVP_DigestUpdate(ctx.get(), input.data(), input.size()) != 1) {
    dst.clear();
    return;
  }
  if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &digest_size) != 1 || digest_size != SHA256_DIGEST_LENGTH) {
    dst.clear();
    return;
  }
  dst.assign(digest.begin(), digest.end());
}

evp_pkey_ptr generate_p256_key(std::error_code& error_code)
{
  evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
  if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0 || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(), NID_X9_62_prime256v1) <= 0) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  EVP_PKEY* raw = nullptr;
  if (EVP_PKEY_keygen(ctx.get(), &raw) <= 0 || raw == nullptr) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  return evp_pkey_ptr(raw);
}

byte_vector marshal_public_key_der(EVP_PKEY* key, std::error_code& error_code)
{
  if (key == nullptr) {
    error_code = error::code::crypto_error;
    return {};
  }
  int size = i2d_PUBKEY(key, nullptr);
  if (size <= 0) {
    error_code = error::code::crypto_error;
    return {};
  }
  byte_vector out(static_cast<std::size_t>(size));
  unsigned char* cursor = out.data();
  if (i2d_PUBKEY(key, &cursor) != size) {
    error_code = error::code::crypto_error;
    return {};
  }
  return out;
}

byte_vector marshal_private_key_der(EVP_PKEY* key, std::error_code& error_code)
{
  if (key == nullptr) {
    error_code = error::code::crypto_error;
    return {};
  }
  int size = i2d_PrivateKey(key, nullptr);
  if (size <= 0) {
    error_code = error::code::crypto_error;
    return {};
  }
  byte_vector out(static_cast<std::size_t>(size));
  unsigned char* cursor = out.data();
  if (i2d_PrivateKey(key, &cursor) != size) {
    error_code = error::code::crypto_error;
    return {};
  }
  return out;
}

evp_pkey_ptr parse_private_key_der(const byte_vector& der, std::error_code& error_code)
{
  if (der.empty()) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  const unsigned char* cursor = der.data();
  EVP_PKEY* raw               = d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(der.size()));
  if (raw == nullptr || cursor != der.data() + der.size()) {
    if (raw != nullptr) {
      EVP_PKEY_free(raw);
    }
    error_code = error::code::crypto_error;
    return nullptr;
  }
  evp_pkey_ptr key(raw);
  if (EVP_PKEY_id(key.get()) != EVP_PKEY_EC) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  return key;
}

evp_pkey_ptr parse_public_key_der(const byte_vector& der, std::error_code& error_code)
{
  if (der.empty()) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  const unsigned char* cursor = der.data();
  EVP_PKEY* raw               = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(der.size()));
  if (raw == nullptr || cursor != der.data() + der.size()) {
    if (raw != nullptr) {
      EVP_PKEY_free(raw);
    }
    error_code = error::code::crypto_error;
    return nullptr;
  }
  evp_pkey_ptr key(raw);
  if (EVP_PKEY_id(key.get()) != EVP_PKEY_EC) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  return key;
}

void sign_digest(byte_vector& signature, const signing_identity& identity, const byte_vector& digest, std::error_code& error_code)
{
  signature.clear();
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    error_code = error::code::crypto_error;
    return;
  }
  auto key = parse_private_key_der(identity.m_private_key, error_code);
  if (error_code) {
    return;
  }
  evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new(key.get(), nullptr));
  if (!ctx || EVP_PKEY_sign_init(ctx.get()) <= 0 || EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0) {
    error_code = error::code::crypto_error;
    return;
  }
  std::size_t size = 0;
  if (EVP_PKEY_sign(ctx.get(), nullptr, &size, digest.data(), digest.size()) <= 0 || size == 0) {
    error_code = error::code::crypto_error;
    return;
  }
  signature.resize(size);
  if (EVP_PKEY_sign(ctx.get(), signature.data(), &size, digest.data(), digest.size()) <= 0) {
    error_code = error::code::crypto_error;
    return;
  }
  signature.resize(size);
}

void verify_digest_signature(const byte_vector& public_key, const byte_vector& digest, const byte_vector& signature, std::error_code& error_code)
{
  if (digest.size() != SHA256_DIGEST_LENGTH || signature.empty()) {
    error_code = error::code::crypto_error;
    return;
  }
  auto key = parse_public_key_der(public_key, error_code);
  if (error_code) {
    return;
  }
  evp_pkey_ctx_ptr ctx(EVP_PKEY_CTX_new(key.get(), nullptr));
  if (!ctx || EVP_PKEY_verify_init(ctx.get()) <= 0 || EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0) {
    error_code = error::code::crypto_error;
    return;
  }
  if (EVP_PKEY_verify(ctx.get(), signature.data(), signature.size(), digest.data(), digest.size()) != 1) {
    error_code = error::code::crypto_error;
  }
}

}  // namespace

void hash_webtty_client_proof_transcript(byte_vector& dst, const client_proof_transcript& transcript, std::error_code& error_code)
{
  dst.clear();
  byte_vector canonical;
  append_length_prefixed(canonical, client_auth_transcript_domain);
  append_uint32(canonical, protocol_version_code(transcript.m_protocol_version, error_code));
  append_length_prefixed_string(canonical, transcript.m_transport);
  append_length_prefixed_string(canonical, transcript.m_workspace_id);
  append_length_prefixed_string(canonical, transcript.m_project_id);
  append_length_prefixed_string(canonical, transcript.m_server_id);
  append_length_prefixed_string(canonical, transcript.m_session_id);
  append_length_prefixed(canonical, transcript.m_server_signing_key_id);
  append_length_prefixed(canonical, transcript.m_server_encryption_key_id);
  append_length_prefixed(canonical, transcript.m_server_nonce);
  append_uint32(canonical, auth_requirement_code(transcript.m_auth_requirement, error_code));
  append_uint32(canonical, payload_suite_code(transcript.m_payload_suite, error_code));
  append_uint32(canonical, key_envelope_suite_code(transcript.m_key_envelope_suite, error_code));
  append_length_prefixed(canonical, transcript.m_session_key_grant_hash);
  append_length_prefixed(canonical, transcript.m_command_config_hash);
  append_length_prefixed(canonical, transcript.m_attach_grant_hash);
  append_length_prefixed_string(canonical, transcript.m_requested_role);
  append_length_prefixed_string(canonical, transcript.m_client_principal_id);
  append_length_prefixed(canonical, transcript.m_client_signing_key_id);
  append_length_prefixed(canonical, transcript.m_client_credential_hash);
  append_length_prefixed_string(canonical, transcript.m_issued_at);
  append_length_prefixed_string(canonical, transcript.m_expires_at);
  if (error_code) {
    return;
  }
  sha256(dst, canonical);
}

void hash_webtty_server_proof_transcript(byte_vector& dst, const server_proof_transcript& transcript, std::error_code& error_code)
{
  dst.clear();
  byte_vector canonical;
  append_length_prefixed(canonical, server_auth_transcript_domain);
  append_uint32(canonical, protocol_version_code(transcript.m_protocol_version, error_code));
  append_length_prefixed_string(canonical, transcript.m_transport);
  append_length_prefixed_string(canonical, transcript.m_workspace_id);
  append_length_prefixed_string(canonical, transcript.m_project_id);
  append_length_prefixed_string(canonical, transcript.m_server_id);
  append_length_prefixed_string(canonical, transcript.m_session_id);
  append_length_prefixed(canonical, transcript.m_server_signing_key_id);
  append_length_prefixed(canonical, transcript.m_server_encryption_key_id);
  append_length_prefixed(canonical, transcript.m_server_nonce);
  append_uint32(canonical, auth_requirement_code(transcript.m_auth_requirement, error_code));
  append_payload_suites(canonical, transcript.m_payload_suites, error_code);
  append_key_envelope_suites(canonical, transcript.m_key_envelope_suites, error_code);
  append_signature_suites(canonical, transcript.m_signature_suites, error_code);
  if (error_code) {
    return;
  }
  sha256(dst, canonical);
}

void generate_endpoint_identity(endpoint_identity& dst, std::error_code& error_code)
{
  error_code.clear();
  generate_e2e_identity(dst.m_encryption, error_code);
  if (error_code) {
    return;
  }
  auto key = generate_p256_key(error_code);
  if (error_code) {
    return;
  }
  dst.m_signing.m_public_key  = marshal_public_key_der(key.get(), error_code);
  dst.m_signing.m_private_key = marshal_private_key_der(key.get(), error_code);
  if (error_code) {
    return;
  }
  signing_key_id(dst.m_signing.m_key_id, dst.m_signing.m_public_key, error_code);
}

void signing_identity_from_private_key(signing_identity& dst, const byte_vector& private_key, std::error_code& error_code)
{
  error_code.clear();
  auto key = parse_private_key_der(private_key, error_code);
  if (error_code) {
    return;
  }
  dst.m_private_key = private_key;
  dst.m_public_key  = marshal_public_key_der(key.get(), error_code);
  if (error_code) {
    return;
  }
  signing_key_id(dst.m_key_id, dst.m_public_key, error_code);
}

void signing_key_id(byte_vector& dst, const byte_vector& public_key, std::error_code& error_code)
{
  error_code.clear();
  if (public_key.empty()) {
    error_code = error::code::crypto_error;
    return;
  }
  auto key = parse_public_key_der(public_key, error_code);
  if (error_code) {
    return;
  }
  sha256(dst, signing_key_id_domain, public_key);
}

endpoint_identity_public public_endpoint_identity(const endpoint_identity& identity)
{
  endpoint_identity_public out;
  out.m_encryption_key_id     = identity.m_encryption.m_key_id;
  out.m_encryption_public_key = identity.m_encryption.m_public_key;
  out.m_signing_key_id        = identity.m_signing.m_key_id;
  out.m_signing_public_key    = identity.m_signing.m_public_key;
  return out;
}

void sign_webtty_client_proof_transcript(byte_vector& transcript_hash, byte_vector& signature, const signing_identity& identity, const client_proof_transcript& transcript, std::error_code& error_code)
{
  hash_webtty_client_proof_transcript(transcript_hash, transcript, error_code);
  if (error_code) {
    return;
  }
  sign_digest(signature, identity, transcript_hash, error_code);
}

void sign_webtty_server_proof_transcript(byte_vector& transcript_hash, byte_vector& signature, const signing_identity& identity, const server_proof_transcript& transcript, std::error_code& error_code)
{
  hash_webtty_server_proof_transcript(transcript_hash, transcript, error_code);
  if (error_code) {
    return;
  }
  sign_digest(signature, identity, transcript_hash, error_code);
}

void verify_p256_sha256_signature(const byte_vector& public_key, const byte_vector& message, const byte_vector& signature, std::error_code& error_code)
{
  error_code.clear();
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(data_ptr(message), message.size(), digest);
  verify_digest_signature(public_key, byte_vector(digest, digest + SHA256_DIGEST_LENGTH), signature, error_code);
}

void verify_webtty_client_proof_transcript(const byte_vector& public_key, const client_proof_transcript& transcript, const byte_vector& signature, std::error_code& error_code)
{
  byte_vector digest;
  hash_webtty_client_proof_transcript(digest, transcript, error_code);
  if (error_code) {
    return;
  }
  verify_digest_signature(public_key, digest, signature, error_code);
}

void verify_webtty_server_proof_transcript(const byte_vector& public_key, const server_proof_transcript& transcript, const byte_vector& signature, std::error_code& error_code)
{
  byte_vector digest;
  hash_webtty_server_proof_transcript(digest, transcript, error_code);
  if (error_code) {
    return;
  }
  verify_digest_signature(public_key, digest, signature, error_code);
}

}  // namespace webtty
}  // namespace rstream
