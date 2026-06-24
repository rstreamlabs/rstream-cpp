// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant.hpp>

namespace rstream {
namespace webtty {

using executor_type = boost::asio::io_context::executor_type;

enum class execution_mode {
  spawn = 0,
  login = 1
};

void parse_execution_mode(execution_mode& dst, const std::string& src);

using byte_vector = std::vector<unsigned char>;

enum class payload_cipher_suite {
  unspecified       = 0,
  aes_256_gcm       = 1,
  chacha20_poly1305 = 2
};

enum class key_envelope_suite {
  unspecified                               = 0,
  hpke_x25519_hkdf_sha256_aes_256_gcm       = 1,
  hpke_x25519_hkdf_sha256_chacha20_poly1305 = 2
};

enum class protocol_version {
  webtty_1 = 1
};

enum class signature_suite {
  ecdsa_p256_sha256 = 1
};

enum class auth_requirement {
  none         = 1,
  client_proof = 2
};

enum class payload_stream {
  std_in  = 0,
  std_out = 1,
  std_err = 2
};

struct key_envelope {
  byte_vector m_recipient_key_id;
  byte_vector m_encapsulated_key;
  byte_vector m_wrapped_key;
};

struct session_key_grant {
  payload_cipher_suite m_payload_suite = payload_cipher_suite::unspecified;
  byte_vector m_payload_key_id;
  std::vector<key_envelope> m_key_envelopes;
  byte_vector m_key_context;
  key_envelope_suite m_key_envelope_suite = key_envelope_suite::unspecified;
};

struct payload_crypto_metadata {
  payload_cipher_suite m_payload_suite = payload_cipher_suite::unspecified;
  byte_vector m_payload_key_id;
  byte_vector m_nonce;
  byte_vector m_aad_context;
};

struct encrypted_payload {
  byte_vector m_ciphertext;
  std::uint32_t m_plaintext_length = 0;
  payload_crypto_metadata m_payload_crypto;
};

struct client_proof_transcript {
  protocol_version m_protocol_version = protocol_version::webtty_1;
  std::string m_transport;
  std::string m_workspace_id;
  std::string m_project_id;
  std::string m_server_id;
  std::string m_session_id;
  byte_vector m_server_signing_key_id;
  byte_vector m_server_encryption_key_id;
  byte_vector m_server_nonce;
  auth_requirement m_auth_requirement     = auth_requirement::client_proof;
  payload_cipher_suite m_payload_suite    = payload_cipher_suite::aes_256_gcm;
  key_envelope_suite m_key_envelope_suite = key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  byte_vector m_session_key_grant_hash;
  byte_vector m_command_config_hash;
  byte_vector m_attach_grant_hash;
  std::string m_requested_role;
  std::string m_client_principal_id;
  byte_vector m_client_signing_key_id;
  byte_vector m_client_credential_hash;
  std::string m_issued_at;
  std::string m_expires_at;
};

struct server_proof_transcript {
  protocol_version m_protocol_version = protocol_version::webtty_1;
  std::string m_transport;
  std::string m_workspace_id;
  std::string m_project_id;
  std::string m_server_id;
  std::string m_session_id;
  byte_vector m_server_signing_key_id;
  byte_vector m_server_encryption_key_id;
  byte_vector m_server_nonce;
  auth_requirement m_auth_requirement = auth_requirement::client_proof;
  std::vector<payload_cipher_suite> m_payload_suites;
  std::vector<key_envelope_suite> m_key_envelope_suites;
  std::vector<signature_suite> m_signature_suites;
};

void hash_webtty_client_proof_transcript(byte_vector& dst, const client_proof_transcript& transcript, std::error_code& error_code);

void hash_webtty_server_proof_transcript(byte_vector& dst, const server_proof_transcript& transcript, std::error_code& error_code);

void sign_webtty_client_proof_transcript(byte_vector& transcript_hash, byte_vector& signature, const struct signing_identity& identity, const client_proof_transcript& transcript, std::error_code& error_code);

void sign_webtty_server_proof_transcript(byte_vector& transcript_hash, byte_vector& signature, const struct signing_identity& identity, const server_proof_transcript& transcript, std::error_code& error_code);

void verify_p256_sha256_signature(const byte_vector& public_key, const byte_vector& message, const byte_vector& signature, std::error_code& error_code);

void verify_webtty_client_proof_transcript(const byte_vector& public_key, const client_proof_transcript& transcript, const byte_vector& signature, std::error_code& error_code);

void verify_webtty_server_proof_transcript(const byte_vector& public_key, const server_proof_transcript& transcript, const byte_vector& signature, std::error_code& error_code);

class payload_crypto {
 public:
  using ptr = std::shared_ptr<payload_crypto>;

  virtual ~payload_crypto() = default;

  virtual void get_session_key_grant(session_key_grant& dst, std::error_code& error_code) const = 0;

  virtual void encrypt(payload_stream stream, const byte_vector& plaintext, encrypted_payload& dst, std::error_code& error_code) const = 0;

  virtual void decrypt(payload_stream stream, const encrypted_payload& src, byte_vector& plaintext, std::error_code& error_code) const = 0;
};

class payload_crypto_resolver {
 public:
  using ptr = std::shared_ptr<payload_crypto_resolver>;

  virtual ~payload_crypto_resolver() = default;

  virtual payload_crypto::ptr resolve(const session_key_grant& grant, std::error_code& error_code) const = 0;
};

struct e2e_identity {
  byte_vector m_key_id;
  byte_vector m_public_key;
  byte_vector m_private_key;
};

struct signing_identity {
  byte_vector m_key_id;
  byte_vector m_public_key;
  byte_vector m_private_key;
};

struct endpoint_identity {
  e2e_identity m_encryption;
  signing_identity m_signing;
};

struct endpoint_identity_public {
  byte_vector m_encryption_key_id;
  byte_vector m_encryption_public_key;
  byte_vector m_signing_key_id;
  byte_vector m_signing_public_key;
};

struct e2e_recipient {
  byte_vector m_key_id;
  byte_vector m_public_key;
};

struct e2e_payload_crypto_config {
  payload_cipher_suite m_payload_suite = payload_cipher_suite::aes_256_gcm;
  byte_vector m_payload_key;
  byte_vector m_payload_key_id;
  byte_vector m_key_context;
  key_envelope_suite m_key_envelope_suite = key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  std::vector<e2e_recipient> m_recipients;
};

void generate_e2e_identity(e2e_identity& dst, std::error_code& error_code);

void e2e_identity_from_private_key(e2e_identity& dst, const byte_vector& private_key, std::error_code& error_code);

void e2e_key_id(byte_vector& dst, const byte_vector& public_key, std::error_code& error_code);

void generate_endpoint_identity(endpoint_identity& dst, std::error_code& error_code);

void signing_identity_from_private_key(signing_identity& dst, const byte_vector& private_key, std::error_code& error_code);

void signing_key_id(byte_vector& dst, const byte_vector& public_key, std::error_code& error_code);

endpoint_identity_public public_endpoint_identity(const endpoint_identity& identity);

payload_crypto::ptr make_e2e_client_payload_crypto(const e2e_payload_crypto_config& config, std::error_code& error_code);

payload_crypto::ptr make_e2e_server_payload_crypto(const session_key_grant& grant, const e2e_identity& identity, std::error_code& error_code);

payload_crypto_resolver::ptr make_e2e_server_payload_crypto_resolver(const e2e_identity& identity);

namespace protocol {

enum type {
  websocket = 0,
  plain     = 1
};

struct options {
  bool m_interactive;
  bool m_allocate_tty;
  bool m_send_heartbeat;
};

struct environment {
  std::string m_key;
  std::string m_value;
};

using env_vars = std::list<environment>;

using cmd_args = std::list<std::string>;

using workdir = boost::optional<std::string>;

using identifier = boost::variant<std::uint32_t, std::string>;

using username = boost::optional<identifier>;

struct user_info {
  std::string m_name;
  std::string m_shell;
  std::string m_home;
#ifndef _WIN32
  std::uint32_t m_uid;
  std::uint32_t m_gid;
  std::vector<std::uint32_t> m_groups;
#endif
};

struct config {
  type m_protocol_type;
  options m_options;
  env_vars m_env_vars;
  cmd_args m_cmd_args;
  workdir m_workdir;
  username m_username;
};

void parse_environment(env_vars& dst, const std::vector<std::string>& src);

env_vars::iterator find_environment_variable(env_vars& dst, const std::string& key);

void add_environment_variable(std::list<environment>& dst, const std::string& key, const std::string& value, bool force = false);

void add_environment_variable(std::list<environment>& dst, const std::string& key, const char* value, bool force = false);

void add_environment_variable(std::list<environment>& dst, const std::string& key, bool force = false);

void parse_type(type& dst, const std::string& src);

void parse_identifier(identifier& dst, const std::string& src);

void parse_username(username& dst, const std::string& src);

#ifdef _WIN32

void get_user_info(user_info& user_info, std::error_code& error_code);

#else

void get_user_info(user_info& user_info, const username& username, std::error_code& error_code);

#endif

}  // namespace protocol

struct webtty_uri_options {
  bool m_managed                  = false;
  bool m_publish                  = true;
  execution_mode m_execution_mode = execution_mode::spawn;
  std::string m_server_id;
  std::string m_host_key_id;
  std::string m_encryption_policy;
  std::string m_server_admission_label;
  std::map<std::string, std::string> m_labels;
};

std::map<std::string, std::string> build_webtty_labels(const webtty_uri_options& options);

// Build rstream URI for webtty tunnels with the default labels.
std::string build_webtty_uri();

std::string build_webtty_uri(const webtty_uri_options& options);

struct settings {
  std::uint32_t m_mtu;
  struct {
    std::uint32_t m_open;
    std::uint32_t m_close;
    std::uint32_t m_heartbeat;
  } m_timeouts_ms;
};

struct settings_client {
  settings m_common;
  std::uint32_t m_std_in_buffer_size;
  payload_crypto::ptr m_payload_crypto;
  boost::optional<endpoint_identity> m_endpoint_identity;
  boost::optional<endpoint_identity_public> m_expected_server_identity;
  byte_vector m_client_credential;
  std::string m_client_principal_id;
};

struct settings_server {
  settings m_common;
  std::uint32_t m_timeouts_start_ms;
  std::uint32_t m_std_out_buffer_size;
  std::uint32_t m_std_err_buffer_size;
  execution_mode m_execution_mode;
  protocol::username m_default_username;
  bool m_allow_client_user = false;
  boost::optional<std::string> m_auth_token;
  payload_crypto_resolver::ptr m_payload_crypto_resolver;
  boost::optional<endpoint_identity> m_endpoint_identity;
  bool m_require_client_proof = false;
  std::map<std::string, byte_vector> m_authorized_client_signing_keys;
  std::function<boost::optional<byte_vector>(const byte_vector&)> m_authorized_client_signing_key_resolver;
  std::function<boost::optional<byte_vector>(const byte_vector&, const byte_vector&, const byte_vector&)> m_client_proof_credential_verifier;
  std::string m_workspace_id;
  std::string m_project_id;
  std::string m_server_id;
};

struct terminal_size {
  unsigned short m_row;    /* rows, in characters        */
  unsigned short m_col;    /* columns, in characters     */
  unsigned short m_xpixel; /* horizontal size, pixels    */
  unsigned short m_ypixel; /* vertical size, pixels      */
};

}  // namespace webtty
}  // namespace rstream
