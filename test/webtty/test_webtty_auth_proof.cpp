// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstring>
#include <system_error>

#include <rstream/webtty/webtty.hpp>

namespace {

rstream::webtty::byte_vector bytes(const char* value)
{
  return rstream::webtty::byte_vector(value, value + std::char_traits<char>::length(value));
}

rstream::webtty::byte_vector bytes(std::initializer_list<unsigned char> value)
{
  return rstream::webtty::byte_vector(value);
}

void check_server_proof_transcript_hash_matches_go_js_vector()
{
  rstream::webtty::server_proof_transcript transcript;
  transcript.m_transport                = "websocket";
  transcript.m_workspace_id             = "workspace-1";
  transcript.m_project_id               = "project-1";
  transcript.m_server_id                = "server-1";
  transcript.m_session_id               = "session-1";
  transcript.m_server_signing_key_id    = bytes("server-signing-key-id");
  transcript.m_server_encryption_key_id = bytes("server-encryption-key-id");
  transcript.m_server_nonce             = bytes("nonce-1");
  transcript.m_payload_suites           = {rstream::webtty::payload_cipher_suite::aes_256_gcm};
  transcript.m_key_envelope_suites      = {rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm};
  transcript.m_signature_suites         = {rstream::webtty::signature_suite::ecdsa_p256_sha256};

  std::error_code error_code;
  rstream::webtty::byte_vector hash;
  rstream::webtty::hash_webtty_server_proof_transcript(hash, transcript, error_code);
  assert(!error_code);
  assert(hash == bytes({0xb8, 0x3c, 0xbe, 0xd5, 0x8e, 0xec, 0xed, 0xa3, 0x9a, 0xd5, 0xd0, 0xab, 0x93, 0xd7, 0x50, 0xd1, 0xcd, 0xdb, 0x03, 0xa9, 0x99, 0x5b, 0x23, 0xbb, 0xfe, 0xa6, 0x72, 0x72, 0xe3, 0x8f, 0x81, 0x3d}));

  transcript.m_server_id = "server-2";
  rstream::webtty::byte_vector changed_hash;
  rstream::webtty::hash_webtty_server_proof_transcript(changed_hash, transcript, error_code);
  assert(!error_code);
  assert(changed_hash != hash);
}

void check_client_proof_transcript_hash_matches_go_js_vector()
{
  rstream::webtty::client_proof_transcript transcript;
  transcript.m_transport                = "websocket";
  transcript.m_workspace_id             = "workspace-1";
  transcript.m_project_id               = "project-1";
  transcript.m_server_id                = "server-1";
  transcript.m_session_id               = "session-1";
  transcript.m_server_signing_key_id    = bytes("server-signing-key-id");
  transcript.m_server_encryption_key_id = bytes("server-encryption-key-id");
  transcript.m_server_nonce             = bytes("nonce-1");
  transcript.m_session_key_grant_hash   = bytes({0x82, 0x1b, 0x17, 0xe3, 0x65, 0x30, 0x44, 0xb1, 0xdf, 0xbe, 0xa3, 0x09, 0x1d, 0x40, 0xee, 0xa2, 0x8b, 0x2f, 0xdc, 0xb8, 0xbf, 0x49, 0x25, 0x08, 0xa0, 0x83, 0x82, 0x0d, 0x8d, 0x08, 0x78, 0x2a});
  transcript.m_command_config_hash      = bytes({0xf3, 0xdd, 0x97, 0x0c, 0xcc, 0xd7, 0xf5, 0x19, 0xf0, 0x91, 0xd7, 0xb3, 0xfc, 0x5a, 0x08, 0x23, 0xa9, 0xd1, 0x04, 0x94, 0x5e, 0x52, 0x49, 0x2f, 0xf5, 0xe3, 0xc2, 0x82, 0xad, 0x2a, 0xf8, 0x9d});
  transcript.m_client_principal_id      = "user-1";
  transcript.m_client_signing_key_id    = bytes("client-signing-key-id");
  transcript.m_issued_at                = "2026-06-12T10:00:00Z";
  transcript.m_expires_at               = "2026-06-12T10:01:00Z";

  std::error_code error_code;
  rstream::webtty::byte_vector hash;
  rstream::webtty::hash_webtty_client_proof_transcript(hash, transcript, error_code);
  assert(!error_code);
  assert(hash == bytes({0x28, 0x43, 0x55, 0x6f, 0x21, 0x85, 0x8f, 0xe7, 0x7a, 0x53, 0x57, 0x8f, 0x9e, 0x2c, 0xf4, 0x66, 0x78, 0x17, 0x8d, 0xa7, 0x72, 0x11, 0x92, 0x74, 0x21, 0x62, 0xb6, 0x20, 0xe3, 0x83, 0x1b, 0xc6}));

  transcript.m_server_nonce = bytes("different");
  rstream::webtty::byte_vector changed_hash;
  rstream::webtty::hash_webtty_client_proof_transcript(changed_hash, transcript, error_code);
  assert(!error_code);
  assert(changed_hash != hash);
}

void check_endpoint_identity_signs_and_verifies_client_proof()
{
  rstream::webtty::endpoint_identity identity;
  std::error_code error_code;
  rstream::webtty::generate_endpoint_identity(identity, error_code);
  assert(!error_code);

  rstream::webtty::client_proof_transcript transcript;
  transcript.m_transport                = "websocket";
  transcript.m_session_id               = "session-1";
  transcript.m_server_signing_key_id    = bytes("server-signing-key-id");
  transcript.m_server_encryption_key_id = bytes("server-encryption-key-id");
  transcript.m_server_nonce             = bytes("nonce-1");
  transcript.m_session_key_grant_hash   = bytes("grant-hash");
  transcript.m_command_config_hash      = bytes("config-hash");
  transcript.m_client_signing_key_id    = identity.m_signing.m_key_id;
  transcript.m_issued_at                = "2026-06-12T10:00:00Z";
  transcript.m_expires_at               = "2026-06-12T10:00:30Z";

  rstream::webtty::byte_vector transcript_hash;
  rstream::webtty::byte_vector signature;
  rstream::webtty::sign_webtty_client_proof_transcript(transcript_hash, signature, identity.m_signing, transcript, error_code);
  assert(!error_code);
  assert(!signature.empty());

  rstream::webtty::verify_webtty_client_proof_transcript(identity.m_signing.m_public_key, transcript, signature, error_code);
  assert(!error_code);

  transcript.m_server_nonce = bytes("different");
  rstream::webtty::verify_webtty_client_proof_transcript(identity.m_signing.m_public_key, transcript, signature, error_code);
  assert(error_code);
}

}  // namespace

int main()
{
  check_server_proof_transcript_hash_matches_go_js_vector();
  check_client_proof_transcript_hash_matches_go_js_vector();
  check_endpoint_identity_signs_and_verifies_client_proof();
  return 0;
}
