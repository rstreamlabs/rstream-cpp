// See LICENSE file in the project root for license information.

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "error.hpp"
#include "webtty.hpp"

namespace rstream {
namespace webtty {

namespace {

constexpr std::size_t x25519_public_key_size   = 32;
constexpr std::size_t x25519_private_key_size  = 32;
constexpr std::size_t payload_key_size         = 32;
constexpr std::size_t payload_key_id_size      = 16;
constexpr std::size_t aes_gcm_nonce_size       = 12;
constexpr std::size_t aes_gcm_tag_size         = 16;
constexpr std::uint16_t hpke_kem_x25519_sha256 = 0x0020;
constexpr std::uint16_t hpke_kdf_hkdf_sha256   = 0x0001;
constexpr std::uint16_t hpke_aead_aes_256_gcm  = 0x0002;
constexpr const char* hpke_info_domain         = "rstream-webtty-e2e-key/v1";
constexpr const char* payload_aad_domain       = "rstream-webtty-e2e-payload/v1";
constexpr const char* key_id_domain            = "rstream-webtty-e2e-key-id/v1";
constexpr const char* hpke_version_label       = "HPKE-v1";

template <typename T, void (*Free)(T*)>
using openssl_ptr = std::unique_ptr<T, decltype(Free)>;

const unsigned char* data_ptr(const byte_vector& value)
{
  return value.empty() ? nullptr : value.data();
}

unsigned char* data_ptr(byte_vector& value)
{
  return value.empty() ? nullptr : value.data();
}

void set_crypto_error(std::error_code& error_code)
{
  if (!error_code) {
    error_code = error::code::crypto_error;
  }
}

byte_vector bytes_from_string(const std::string& value)
{
  return byte_vector(value.begin(), value.end());
}

void append(byte_vector& dst, const byte_vector& src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

void append(byte_vector& dst, const std::string& src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

void append(byte_vector& dst, std::initializer_list<unsigned char> src)
{
  dst.insert(dst.end(), src.begin(), src.end());
}

byte_vector be16(std::uint16_t value)
{
  return {
      static_cast<unsigned char>((value >> 8) & 0xff),
      static_cast<unsigned char>(value & 0xff),
  };
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

byte_vector length_prefixed(const byte_vector& value)
{
  byte_vector out = be32(static_cast<std::uint32_t>(value.size()));
  append(out, value);
  return out;
}

byte_vector length_prefixed(const std::string& value)
{
  return length_prefixed(bytes_from_string(value));
}

bool bytes_equal(const byte_vector& a, const byte_vector& b)
{
  if (a.size() != b.size()) {
    return false;
  }
  if (a.empty()) {
    return true;
  }
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

void random_bytes(byte_vector& dst, std::size_t size, std::error_code& error_code)
{
  dst.assign(size, 0);
  if (size == 0) {
    return;
  }
  if (RAND_bytes(dst.data(), static_cast<int>(dst.size())) != 1) {
    dst.clear();
    set_crypto_error(error_code);
  }
}

void sha256(byte_vector& dst, const byte_vector& input)
{
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  SHA256(data_ptr(input), input.size(), digest.data());
  dst.assign(digest.begin(), digest.end());
}

void hmac_sha256(byte_vector& dst, const byte_vector& key, const byte_vector& input, std::error_code& error_code)
{
  std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
  unsigned int digest_size = 0;
  if (key.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    set_crypto_error(error_code);
    return;
  }
  if (HMAC(EVP_sha256(), data_ptr(key), static_cast<int>(key.size()), data_ptr(input), input.size(), digest.data(), &digest_size) == nullptr || digest_size != digest.size()) {
    set_crypto_error(error_code);
    return;
  }
  dst.assign(digest.begin(), digest.end());
}

void hkdf_extract(byte_vector& dst, const byte_vector& salt, const byte_vector& ikm, std::error_code& error_code)
{
  byte_vector effective_salt = salt;
  if (effective_salt.empty()) {
    effective_salt.assign(SHA256_DIGEST_LENGTH, 0);
  }
  hmac_sha256(dst, effective_salt, ikm, error_code);
}

void hkdf_expand(byte_vector& dst, const byte_vector& prk, const byte_vector& info, std::size_t length, std::error_code& error_code)
{
  dst.clear();
  if (length == 0) {
    return;
  }
  if (length > 255 * SHA256_DIGEST_LENGTH) {
    set_crypto_error(error_code);
    return;
  }
  byte_vector previous;
  unsigned char counter = 1;
  while (dst.size() < length) {
    byte_vector input = previous;
    append(input, info);
    append(input, {counter});
    hmac_sha256(previous, prk, input, error_code);
    if (error_code) {
      return;
    }
    append(dst, previous);
    ++counter;
  }
  dst.resize(length);
}

byte_vector dhkem_suite_id()
{
  byte_vector out;
  append(out, "KEM");
  append(out, be16(hpke_kem_x25519_sha256));
  return out;
}

byte_vector hpke_suite_id()
{
  byte_vector out;
  append(out, "HPKE");
  append(out, be16(hpke_kem_x25519_sha256));
  append(out, be16(hpke_kdf_hkdf_sha256));
  append(out, be16(hpke_aead_aes_256_gcm));
  return out;
}

void labeled_extract(byte_vector& dst, const byte_vector& suite_id, const byte_vector& salt, const std::string& label, const byte_vector& ikm, std::error_code& error_code)
{
  byte_vector labeled_ikm;
  append(labeled_ikm, hpke_version_label);
  append(labeled_ikm, suite_id);
  append(labeled_ikm, label);
  append(labeled_ikm, ikm);
  hkdf_extract(dst, salt, labeled_ikm, error_code);
}

void labeled_expand(byte_vector& dst, const byte_vector& suite_id, const byte_vector& prk, const std::string& label, const byte_vector& info, std::size_t length, std::error_code& error_code)
{
  byte_vector labeled_info = be16(static_cast<std::uint16_t>(length));
  append(labeled_info, hpke_version_label);
  append(labeled_info, suite_id);
  append(labeled_info, label);
  append(labeled_info, info);
  hkdf_expand(dst, prk, labeled_info, length, error_code);
}

void validate_payload_suite(payload_cipher_suite suite, std::error_code& error_code)
{
  if (suite != payload_cipher_suite::aes_256_gcm) {
    error_code = error::code::crypto_error;
  }
}

void validate_key_envelope_suite(key_envelope_suite suite, std::error_code& error_code)
{
  if (suite != key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm) {
    error_code = error::code::crypto_error;
  }
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

byte_vector hpke_info(payload_cipher_suite payload_suite, const byte_vector& payload_key_id, const byte_vector& key_context, key_envelope_suite envelope_suite, std::error_code& error_code)
{
  byte_vector out = length_prefixed(hpke_info_domain);
  append(out, be32(payload_suite_code(payload_suite, error_code)));
  append(out, be32(key_envelope_suite_code(envelope_suite, error_code)));
  append(out, length_prefixed(payload_key_id));
  append(out, length_prefixed(key_context));
  return error_code ? byte_vector{} : out;
}

byte_vector hpke_aad(const byte_vector& recipient_key_id, payload_cipher_suite payload_suite, const byte_vector& payload_key_id, const byte_vector& key_context, key_envelope_suite envelope_suite, std::error_code& error_code)
{
  byte_vector out = length_prefixed("key-wrap");
  append(out, be32(payload_suite_code(payload_suite, error_code)));
  append(out, be32(key_envelope_suite_code(envelope_suite, error_code)));
  append(out, length_prefixed(recipient_key_id));
  append(out, length_prefixed(payload_key_id));
  append(out, length_prefixed(key_context));
  return error_code ? byte_vector{} : out;
}

const char* payload_stream_name(payload_stream stream)
{
  switch (stream) {
    case payload_stream::std_in:
      return "stdin";
    case payload_stream::std_out:
      return "stdout";
    case payload_stream::std_err:
      return "stderr";
    default:
      return "";
  }
}

byte_vector payload_aad(payload_stream stream, payload_cipher_suite suite, const byte_vector& payload_key_id, const byte_vector& key_context, const byte_vector& nonce, std::uint32_t plaintext_length, std::error_code& error_code)
{
  auto stream_name = payload_stream_name(stream);
  if (std::strlen(stream_name) == 0) {
    error_code = error::code::crypto_error;
    return {};
  }
  byte_vector out = length_prefixed(payload_aad_domain);
  append(out, length_prefixed(stream_name));
  append(out, be32(payload_suite_code(suite, error_code)));
  append(out, length_prefixed(payload_key_id));
  append(out, length_prefixed(key_context));
  append(out, length_prefixed(nonce));
  append(out, be32(plaintext_length));
  return error_code ? byte_vector{} : out;
}

openssl_ptr<EVP_PKEY, EVP_PKEY_free> x25519_private_key(const byte_vector& private_key, std::error_code& error_code)
{
  if (private_key.size() != x25519_private_key_size) {
    error_code = error::code::crypto_error;
    return {nullptr, EVP_PKEY_free};
  }
  return {EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, private_key.data(), private_key.size()), EVP_PKEY_free};
}

openssl_ptr<EVP_PKEY, EVP_PKEY_free> x25519_public_key(const byte_vector& public_key, std::error_code& error_code)
{
  if (public_key.size() != x25519_public_key_size) {
    error_code = error::code::crypto_error;
    return {nullptr, EVP_PKEY_free};
  }
  return {EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, public_key.data(), public_key.size()), EVP_PKEY_free};
}

void x25519_get_raw_public_key(byte_vector& dst, EVP_PKEY* key, std::error_code& error_code)
{
  dst.assign(x25519_public_key_size, 0);
  auto size = dst.size();
  if (EVP_PKEY_get_raw_public_key(key, dst.data(), &size) != 1 || size != x25519_public_key_size) {
    dst.clear();
    set_crypto_error(error_code);
  }
}

void x25519_get_raw_private_key(byte_vector& dst, EVP_PKEY* key, std::error_code& error_code)
{
  dst.assign(x25519_private_key_size, 0);
  auto size = dst.size();
  if (EVP_PKEY_get_raw_private_key(key, dst.data(), &size) != 1 || size != x25519_private_key_size) {
    dst.clear();
    set_crypto_error(error_code);
  }
}

void x25519_public_from_private(byte_vector& dst, const byte_vector& private_key, std::error_code& error_code)
{
  auto private_pkey = x25519_private_key(private_key, error_code);
  if (error_code || !private_pkey) {
    return;
  }
  x25519_get_raw_public_key(dst, private_pkey.get(), error_code);
}

void normalize_identity(e2e_identity& dst, const e2e_identity& src, std::error_code& error_code)
{
  dst = src;
  if (dst.m_private_key.size() != x25519_private_key_size) {
    error_code = error::code::crypto_error;
    return;
  }
  if (dst.m_public_key.empty()) {
    x25519_public_from_private(dst.m_public_key, dst.m_private_key, error_code);
    if (error_code) {
      return;
    }
  }
  if (dst.m_public_key.size() != x25519_public_key_size) {
    error_code = error::code::crypto_error;
    return;
  }
  if (dst.m_key_id.empty()) {
    e2e_key_id(dst.m_key_id, dst.m_public_key, error_code);
  }
  else if (dst.m_key_id.size() != payload_key_id_size) {
    error_code = error::code::crypto_error;
  }
}

void x25519_dh(byte_vector& dst, const byte_vector& private_key, const byte_vector& peer_public_key, std::error_code& error_code)
{
  auto private_pkey = x25519_private_key(private_key, error_code);
  auto public_pkey  = x25519_public_key(peer_public_key, error_code);
  if (error_code || !private_pkey || !public_pkey) {
    return;
  }
  openssl_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> ctx(EVP_PKEY_CTX_new(private_pkey.get(), nullptr), EVP_PKEY_CTX_free);
  if (!ctx || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), public_pkey.get()) != 1) {
    set_crypto_error(error_code);
    return;
  }
  std::size_t out_len = 0;
  if (EVP_PKEY_derive(ctx.get(), nullptr, &out_len) != 1 || out_len != x25519_public_key_size) {
    set_crypto_error(error_code);
    return;
  }
  dst.assign(out_len, 0);
  if (EVP_PKEY_derive(ctx.get(), dst.data(), &out_len) != 1 || out_len != x25519_public_key_size) {
    dst.clear();
    set_crypto_error(error_code);
    return;
  }
  if (std::all_of(dst.begin(), dst.end(), [](unsigned char value) { return value == 0; })) {
    dst.clear();
    set_crypto_error(error_code);
  }
}

void dhkem_extract_and_expand(byte_vector& dst, const byte_vector& dh, const byte_vector& kem_context, std::error_code& error_code)
{
  auto suite_id = dhkem_suite_id();
  byte_vector eae_prk;
  labeled_extract(eae_prk, suite_id, {}, "eae_prk", dh, error_code);
  if (error_code) {
    return;
  }
  labeled_expand(dst, suite_id, eae_prk, "shared_secret", kem_context, payload_key_size, error_code);
}

struct hpke_schedule {
  byte_vector m_key;
  byte_vector m_base_nonce;
};

void hpke_key_schedule(hpke_schedule& dst, const byte_vector& shared_secret, const byte_vector& info, std::error_code& error_code)
{
  auto suite_id = hpke_suite_id();
  byte_vector psk_id_hash;
  labeled_extract(psk_id_hash, suite_id, {}, "psk_id_hash", {}, error_code);
  byte_vector info_hash;
  labeled_extract(info_hash, suite_id, {}, "info_hash", info, error_code);
  if (error_code) {
    return;
  }
  byte_vector key_schedule_context{0};
  append(key_schedule_context, psk_id_hash);
  append(key_schedule_context, info_hash);
  byte_vector secret;
  labeled_extract(secret, suite_id, shared_secret, "secret", {}, error_code);
  if (error_code) {
    return;
  }
  labeled_expand(dst.m_base_nonce, suite_id, secret, "base_nonce", key_schedule_context, aes_gcm_nonce_size, error_code);
  labeled_expand(dst.m_key, suite_id, secret, "key", key_schedule_context, payload_key_size, error_code);
}

void aes_gcm_encrypt(byte_vector& ciphertext, const byte_vector& key, const byte_vector& nonce, const byte_vector& plaintext, const byte_vector& aad, std::error_code& error_code)
{
  ciphertext.clear();
  if (key.size() != payload_key_size || nonce.size() != aes_gcm_nonce_size || plaintext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    error_code = error::code::crypto_error;
    return;
  }
  openssl_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 || EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
    set_crypto_error(error_code);
    return;
  }
  int out_len = 0;
  if (!aad.empty() && EVP_EncryptUpdate(ctx.get(), nullptr, &out_len, aad.data(), static_cast<int>(aad.size())) != 1) {
    set_crypto_error(error_code);
    return;
  }
  ciphertext.assign(plaintext.size() + aes_gcm_tag_size, 0);
  if (!plaintext.empty() && EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
    ciphertext.clear();
    set_crypto_error(error_code);
    return;
  }
  int total = out_len;
  if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + total, &out_len) != 1) {
    ciphertext.clear();
    set_crypto_error(error_code);
    return;
  }
  total += out_len;
  ciphertext.resize(static_cast<std::size_t>(total) + aes_gcm_tag_size);
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, aes_gcm_tag_size, ciphertext.data() + total) != 1) {
    ciphertext.clear();
    set_crypto_error(error_code);
  }
}

void aes_gcm_decrypt(byte_vector& plaintext, const byte_vector& key, const byte_vector& nonce, const byte_vector& ciphertext, const byte_vector& aad, std::error_code& error_code)
{
  plaintext.clear();
  if (key.size() != payload_key_size || nonce.size() != aes_gcm_nonce_size || ciphertext.size() < aes_gcm_tag_size || ciphertext.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    error_code = error::code::crypto_error;
    return;
  }
  const auto encrypted_size = ciphertext.size() - aes_gcm_tag_size;
  openssl_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1 || EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1) {
    set_crypto_error(error_code);
    return;
  }
  int out_len = 0;
  if (!aad.empty() && EVP_DecryptUpdate(ctx.get(), nullptr, &out_len, aad.data(), static_cast<int>(aad.size())) != 1) {
    set_crypto_error(error_code);
    return;
  }
  plaintext.assign(encrypted_size, 0);
  if (encrypted_size > 0 && EVP_DecryptUpdate(ctx.get(), plaintext.data(), &out_len, ciphertext.data(), static_cast<int>(encrypted_size)) != 1) {
    plaintext.clear();
    set_crypto_error(error_code);
    return;
  }
  int total = out_len;
  unsigned char tag[aes_gcm_tag_size];
  std::memcpy(tag, ciphertext.data() + encrypted_size, aes_gcm_tag_size);
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, aes_gcm_tag_size, tag) != 1 || EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total, &out_len) != 1) {
    plaintext.clear();
    set_crypto_error(error_code);
    return;
  }
  total += out_len;
  plaintext.resize(static_cast<std::size_t>(total));
}

void hpke_seal(key_envelope& dst, const byte_vector& recipient_key_id, const byte_vector& recipient_public_key, const byte_vector& info, const byte_vector& aad, const byte_vector& plaintext, std::error_code& error_code)
{
  e2e_identity ephemeral;
  generate_e2e_identity(ephemeral, error_code);
  if (error_code) {
    return;
  }
  byte_vector dh;
  x25519_dh(dh, ephemeral.m_private_key, recipient_public_key, error_code);
  byte_vector kem_context = ephemeral.m_public_key;
  append(kem_context, recipient_public_key);
  byte_vector shared_secret;
  dhkem_extract_and_expand(shared_secret, dh, kem_context, error_code);
  hpke_schedule schedule;
  hpke_key_schedule(schedule, shared_secret, info, error_code);
  byte_vector wrapped_key;
  aes_gcm_encrypt(wrapped_key, schedule.m_key, schedule.m_base_nonce, plaintext, aad, error_code);
  if (error_code) {
    return;
  }
  dst.m_recipient_key_id = recipient_key_id;
  dst.m_encapsulated_key = ephemeral.m_public_key;
  dst.m_wrapped_key      = wrapped_key;
}

void hpke_open(byte_vector& plaintext, const e2e_identity& identity, const key_envelope& envelope, const byte_vector& info, const byte_vector& aad, std::error_code& error_code)
{
  byte_vector dh;
  x25519_dh(dh, identity.m_private_key, envelope.m_encapsulated_key, error_code);
  byte_vector kem_context = envelope.m_encapsulated_key;
  append(kem_context, identity.m_public_key);
  byte_vector shared_secret;
  dhkem_extract_and_expand(shared_secret, dh, kem_context, error_code);
  hpke_schedule schedule;
  hpke_key_schedule(schedule, shared_secret, info, error_code);
  aes_gcm_decrypt(plaintext, schedule.m_key, schedule.m_base_nonce, envelope.m_wrapped_key, aad, error_code);
}

class e2e_payload_crypto_impl : public payload_crypto {
 public:
  e2e_payload_crypto_impl(payload_cipher_suite payload_suite, byte_vector payload_key, byte_vector payload_key_id, byte_vector key_context, key_envelope_suite envelope_suite, std::vector<key_envelope> key_envelopes)
      : m_payload_suite(payload_suite),
        m_payload_key(std::move(payload_key)),
        m_payload_key_id(std::move(payload_key_id)),
        m_key_context(std::move(key_context)),
        m_key_envelope_suite(envelope_suite),
        m_key_envelopes(std::move(key_envelopes))
  {
  }

  void get_session_key_grant(session_key_grant& dst, std::error_code& error_code) const override
  {
    error_code.clear();
    dst.m_payload_suite      = m_payload_suite;
    dst.m_payload_key_id     = m_payload_key_id;
    dst.m_key_envelopes      = m_key_envelopes;
    dst.m_key_context        = m_key_context;
    dst.m_key_envelope_suite = m_key_envelope_suite;
  }

  void encrypt(payload_stream stream, const byte_vector& plaintext, encrypted_payload& dst, std::error_code& error_code) const override
  {
    error_code.clear();
    if (plaintext.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
      error_code = error::code::crypto_error;
      return;
    }
    byte_vector nonce;
    random_bytes(nonce, aes_gcm_nonce_size, error_code);
    auto plain_length                     = static_cast<std::uint32_t>(plaintext.size());
    dst.m_plaintext_length                = plain_length;
    dst.m_payload_crypto.m_payload_suite  = m_payload_suite;
    dst.m_payload_crypto.m_payload_key_id = m_payload_key_id;
    dst.m_payload_crypto.m_nonce          = nonce;
    dst.m_payload_crypto.m_aad_context    = m_key_context;
    auto aad                              = payload_aad(stream, m_payload_suite, m_payload_key_id, m_key_context, nonce, plain_length, error_code);
    aes_gcm_encrypt(dst.m_ciphertext, m_payload_key, nonce, plaintext, aad, error_code);
  }

  void decrypt(payload_stream stream, const encrypted_payload& src, byte_vector& plaintext, std::error_code& error_code) const override
  {
    error_code.clear();
    if (src.m_payload_crypto.m_payload_suite != m_payload_suite || !bytes_equal(src.m_payload_crypto.m_payload_key_id, m_payload_key_id) || !bytes_equal(src.m_payload_crypto.m_aad_context, m_key_context) || src.m_payload_crypto.m_nonce.size() != aes_gcm_nonce_size) {
      error_code = error::code::crypto_error;
      return;
    }
    auto aad = payload_aad(stream, src.m_payload_crypto.m_payload_suite, src.m_payload_crypto.m_payload_key_id, src.m_payload_crypto.m_aad_context, src.m_payload_crypto.m_nonce, src.m_plaintext_length, error_code);
    aes_gcm_decrypt(plaintext, m_payload_key, src.m_payload_crypto.m_nonce, src.m_ciphertext, aad, error_code);
    if (!error_code && plaintext.size() != src.m_plaintext_length) {
      plaintext.clear();
      error_code = error::code::crypto_error;
    }
  }

 private:
  payload_cipher_suite m_payload_suite;
  byte_vector m_payload_key;
  byte_vector m_payload_key_id;
  byte_vector m_key_context;
  key_envelope_suite m_key_envelope_suite;
  std::vector<key_envelope> m_key_envelopes;
};

class e2e_payload_crypto_resolver_impl : public payload_crypto_resolver {
 public:
  explicit e2e_payload_crypto_resolver_impl(e2e_identity identity)
      : m_identity(std::move(identity))
  {
  }

  payload_crypto::ptr resolve(const session_key_grant& session_key_grant, std::error_code& error_code) const override
  {
    return make_e2e_server_payload_crypto(session_key_grant, m_identity, error_code);
  }

 private:
  e2e_identity m_identity;
};

}  // namespace

void generate_e2e_identity(e2e_identity& dst, std::error_code& error_code)
{
  error_code.clear();
  openssl_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), EVP_PKEY_CTX_free);
  EVP_PKEY* raw_key = nullptr;
  if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_keygen(ctx.get(), &raw_key) != 1) {
    set_crypto_error(error_code);
    return;
  }
  openssl_ptr<EVP_PKEY, EVP_PKEY_free> key(raw_key, EVP_PKEY_free);
  x25519_get_raw_private_key(dst.m_private_key, key.get(), error_code);
  x25519_get_raw_public_key(dst.m_public_key, key.get(), error_code);
  e2e_key_id(dst.m_key_id, dst.m_public_key, error_code);
}

void e2e_identity_from_private_key(e2e_identity& dst, const byte_vector& private_key, std::error_code& error_code)
{
  error_code.clear();
  e2e_identity identity;
  identity.m_private_key = private_key;
  normalize_identity(dst, identity, error_code);
}

void e2e_key_id(byte_vector& dst, const byte_vector& public_key, std::error_code& error_code)
{
  error_code.clear();
  if (public_key.size() != x25519_public_key_size) {
    error_code = error::code::crypto_error;
    return;
  }
  byte_vector input = bytes_from_string(key_id_domain);
  append(input, public_key);
  byte_vector digest;
  sha256(digest, input);
  dst.assign(digest.begin(), digest.begin() + payload_key_id_size);
}

payload_crypto::ptr make_e2e_client_payload_crypto(const e2e_payload_crypto_config& config, std::error_code& error_code)
{
  error_code.clear();
  validate_payload_suite(config.m_payload_suite, error_code);
  validate_key_envelope_suite(config.m_key_envelope_suite, error_code);
  if (error_code) {
    return nullptr;
  }
  byte_vector payload_key = config.m_payload_key;
  if (payload_key.empty()) {
    random_bytes(payload_key, payload_key_size, error_code);
  }
  if (payload_key.size() != payload_key_size) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  byte_vector payload_key_id = config.m_payload_key_id;
  if (payload_key_id.empty()) {
    random_bytes(payload_key_id, payload_key_id_size, error_code);
  }
  if (payload_key_id.size() != payload_key_id_size || config.m_recipients.empty()) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  std::vector<key_envelope> key_envelopes;
  for (const auto& recipient : config.m_recipients) {
    if (recipient.m_public_key.size() != x25519_public_key_size) {
      error_code = error::code::crypto_error;
      return nullptr;
    }
    auto recipient_key_id = recipient.m_key_id;
    if (recipient_key_id.empty()) {
      e2e_key_id(recipient_key_id, recipient.m_public_key, error_code);
      if (error_code) {
        return nullptr;
      }
    }
    if (recipient_key_id.size() != payload_key_id_size) {
      error_code = error::code::crypto_error;
      return nullptr;
    }
    auto info = hpke_info(config.m_payload_suite, payload_key_id, config.m_key_context, config.m_key_envelope_suite, error_code);
    auto aad  = hpke_aad(recipient_key_id, config.m_payload_suite, payload_key_id, config.m_key_context, config.m_key_envelope_suite, error_code);
    if (error_code) {
      return nullptr;
    }
    key_envelope envelope;
    hpke_seal(envelope, recipient_key_id, recipient.m_public_key, info, aad, payload_key, error_code);
    if (error_code) {
      return nullptr;
    }
    key_envelopes.push_back(std::move(envelope));
  }
  return std::make_shared<e2e_payload_crypto_impl>(config.m_payload_suite, std::move(payload_key), std::move(payload_key_id), config.m_key_context, config.m_key_envelope_suite, std::move(key_envelopes));
}

payload_crypto::ptr make_e2e_server_payload_crypto(const session_key_grant& session_key_grant, const e2e_identity& identity, std::error_code& error_code)
{
  error_code.clear();
  validate_payload_suite(session_key_grant.m_payload_suite, error_code);
  validate_key_envelope_suite(session_key_grant.m_key_envelope_suite, error_code);
  if (session_key_grant.m_payload_key_id.size() != payload_key_id_size) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  e2e_identity normalized_identity;
  normalize_identity(normalized_identity, identity, error_code);
  if (error_code) {
    return nullptr;
  }
  const key_envelope* matched = nullptr;
  for (const auto& envelope : session_key_grant.m_key_envelopes) {
    if (bytes_equal(envelope.m_recipient_key_id, normalized_identity.m_key_id)) {
      matched = &envelope;
      break;
    }
  }
  if (matched == nullptr) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  auto info = hpke_info(session_key_grant.m_payload_suite, session_key_grant.m_payload_key_id, session_key_grant.m_key_context, session_key_grant.m_key_envelope_suite, error_code);
  auto aad  = hpke_aad(matched->m_recipient_key_id, session_key_grant.m_payload_suite, session_key_grant.m_payload_key_id, session_key_grant.m_key_context, session_key_grant.m_key_envelope_suite, error_code);
  byte_vector payload_key;
  hpke_open(payload_key, normalized_identity, *matched, info, aad, error_code);
  if (error_code || payload_key.size() != payload_key_size) {
    error_code = error::code::crypto_error;
    return nullptr;
  }
  return std::make_shared<e2e_payload_crypto_impl>(session_key_grant.m_payload_suite, std::move(payload_key), session_key_grant.m_payload_key_id, session_key_grant.m_key_context, session_key_grant.m_key_envelope_suite, std::vector<key_envelope>{});
}

payload_crypto_resolver::ptr make_e2e_server_payload_crypto_resolver(const e2e_identity& identity)
{
  return std::make_shared<e2e_payload_crypto_resolver_impl>(identity);
}

}  // namespace webtty
}  // namespace rstream
