// See LICENSE file in the project root for license information.

#include <cassert>
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

void check_e2e_payload_crypto_roundtrip()
{
  std::error_code error_code;
  rstream::webtty::e2e_identity server_identity;
  rstream::webtty::generate_e2e_identity(server_identity, error_code);
  assert(!error_code);
  assert(server_identity.m_key_id.size() == 16);
  assert(server_identity.m_public_key.size() == 32);
  assert(server_identity.m_private_key.size() == 32);

  rstream::webtty::e2e_payload_crypto_config client_config;
  client_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x42);
  client_config.m_payload_key_id = bytes("payload-key-0001");
  client_config.m_key_context    = bytes("{\"workspace_id\":\"workspace-1\"}");
  client_config.m_recipients     = {{server_identity.m_key_id, server_identity.m_public_key}};

  auto client_crypto = rstream::webtty::make_e2e_client_payload_crypto(client_config, error_code);
  assert(!error_code);
  assert(client_crypto);

  rstream::webtty::session_key_grant session_key_grant;
  client_crypto->get_session_key_grant(session_key_grant, error_code);
  assert(!error_code);
  assert(session_key_grant.m_payload_suite == rstream::webtty::payload_cipher_suite::aes_256_gcm);
  assert(session_key_grant.m_key_envelope_suite == rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm);
  assert(session_key_grant.m_key_envelopes.size() == 1);

  auto server_crypto = rstream::webtty::make_e2e_server_payload_crypto(session_key_grant, server_identity, error_code);
  assert(!error_code);
  assert(server_crypto);

  rstream::webtty::encrypted_payload encrypted_stdin;
  client_crypto->encrypt(rstream::webtty::payload_stream::std_in, bytes("echo hello\n"), encrypted_stdin, error_code);
  assert(!error_code);
  assert(!encrypted_stdin.m_ciphertext.empty());
  assert(encrypted_stdin.m_plaintext_length == 11);
  assert(encrypted_stdin.m_payload_crypto.m_nonce.size() == 12);

  rstream::webtty::byte_vector decrypted_stdin;
  server_crypto->decrypt(rstream::webtty::payload_stream::std_in, encrypted_stdin, decrypted_stdin, error_code);
  assert(!error_code);
  assert(decrypted_stdin == bytes("echo hello\n"));

  rstream::webtty::encrypted_payload encrypted_stdout;
  server_crypto->encrypt(rstream::webtty::payload_stream::std_out, bytes("hello\n"), encrypted_stdout, error_code);
  assert(!error_code);

  rstream::webtty::byte_vector decrypted_stdout;
  client_crypto->decrypt(rstream::webtty::payload_stream::std_out, encrypted_stdout, decrypted_stdout, error_code);
  assert(!error_code);
  assert(decrypted_stdout == bytes("hello\n"));

  rstream::webtty::byte_vector wrong_stream;
  client_crypto->decrypt(rstream::webtty::payload_stream::std_err, encrypted_stdout, wrong_stream, error_code);
  assert(error_code);

  auto tampered_suite                             = encrypted_stdout;
  tampered_suite.m_payload_crypto.m_payload_suite = rstream::webtty::payload_cipher_suite::unspecified;
  rstream::webtty::byte_vector tampered_plaintext;
  client_crypto->decrypt(rstream::webtty::payload_stream::std_out, tampered_suite, tampered_plaintext, error_code);
  assert(error_code);
}

void check_e2e_payload_crypto_rejects_unsupported_suites()
{
  std::error_code error_code;
  rstream::webtty::e2e_identity server_identity;
  rstream::webtty::generate_e2e_identity(server_identity, error_code);
  assert(!error_code);

  rstream::webtty::e2e_payload_crypto_config client_config;
  client_config.m_payload_suite = rstream::webtty::payload_cipher_suite::chacha20_poly1305;
  client_config.m_recipients    = {{server_identity.m_key_id, server_identity.m_public_key}};
  auto client_crypto            = rstream::webtty::make_e2e_client_payload_crypto(client_config, error_code);
  assert(error_code);
  assert(!client_crypto);
}

void check_e2e_payload_crypto_rejects_invalid_key_ids()
{
  std::error_code error_code;
  rstream::webtty::e2e_identity server_identity;
  rstream::webtty::generate_e2e_identity(server_identity, error_code);
  assert(!error_code);

  rstream::webtty::e2e_payload_crypto_config client_config;
  client_config.m_payload_key_id = bytes("short");
  client_config.m_recipients     = {{server_identity.m_key_id, server_identity.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(client_config, error_code);
  assert(error_code);
  assert(!client_crypto);

  error_code.clear();
  client_config              = {};
  client_config.m_recipients = {{bytes("short"), server_identity.m_public_key}};
  client_crypto              = rstream::webtty::make_e2e_client_payload_crypto(client_config, error_code);
  assert(error_code);
  assert(!client_crypto);

  error_code.clear();
  client_config              = {};
  client_config.m_recipients = {{server_identity.m_key_id, server_identity.m_public_key}};
  client_crypto              = rstream::webtty::make_e2e_client_payload_crypto(client_config, error_code);
  assert(!error_code);
  assert(client_crypto);

  rstream::webtty::session_key_grant session_key_grant;
  client_crypto->get_session_key_grant(session_key_grant, error_code);
  assert(!error_code);
  session_key_grant.m_payload_key_id = bytes("short");
  auto server_crypto                 = rstream::webtty::make_e2e_server_payload_crypto(session_key_grant, server_identity, error_code);
  assert(error_code);
  assert(!server_crypto);
}

void check_go_e2e_payload_crypto_vector()
{
  std::error_code error_code;
  rstream::webtty::e2e_identity identity;
  identity.m_key_id      = bytes({0x50, 0xa0, 0x8e, 0xb2, 0x29, 0x51, 0x33, 0x1c, 0x72, 0xaf, 0x60, 0xa8, 0xf8, 0xb1, 0x2d, 0x9f});
  identity.m_public_key  = bytes({0x22, 0xd4, 0x27, 0x45, 0x37, 0x82, 0x44, 0x95, 0xd6, 0x25, 0x91, 0x40, 0x63, 0xad, 0x7b, 0xbc, 0xf2, 0x5e, 0xc1, 0x50, 0xe9, 0xe9, 0x20, 0x3c, 0x68, 0xc7, 0x83, 0x97, 0xb2, 0xb7, 0x30, 0x0b});
  identity.m_private_key = bytes({0x78, 0xc1, 0xf1, 0x6b, 0x4c, 0x8e, 0xea, 0x75, 0x6f, 0x2a, 0x33, 0xc1, 0xb9, 0xf1, 0x1f, 0xb7, 0xb5, 0xc6, 0x10, 0x9f, 0x7d, 0xc8, 0x7f, 0x2d, 0xce, 0x01, 0xb1, 0x7c, 0xe4, 0x20, 0xb6, 0xa1});

  rstream::webtty::byte_vector computed_key_id;
  rstream::webtty::e2e_key_id(computed_key_id, identity.m_public_key, error_code);
  assert(!error_code);
  assert(computed_key_id == identity.m_key_id);

  rstream::webtty::session_key_grant session_key_grant;
  session_key_grant.m_payload_suite      = rstream::webtty::payload_cipher_suite::aes_256_gcm;
  session_key_grant.m_payload_key_id     = bytes({0x70, 0x61, 0x79, 0x6c, 0x6f, 0x61, 0x64, 0x2d, 0x6b, 0x65, 0x79, 0x2d, 0x67, 0x6f, 0x30, 0x31});
  session_key_grant.m_key_context        = bytes({0x7b, 0x22, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x6f, 0x70, 0x22, 0x3a, 0x22, 0x67, 0x6f, 0x2d, 0x74, 0x6f, 0x2d, 0x63, 0x70, 0x70, 0x22, 0x7d});
  session_key_grant.m_key_envelope_suite = rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  session_key_grant.m_key_envelopes.push_back({
      bytes({0x50, 0xa0, 0x8e, 0xb2, 0x29, 0x51, 0x33, 0x1c, 0x72, 0xaf, 0x60, 0xa8, 0xf8, 0xb1, 0x2d, 0x9f}),
      bytes({0xa6, 0x52, 0x91, 0x59, 0xe4, 0x45, 0xf7, 0x43, 0x59, 0x92, 0xef, 0xb2, 0x27, 0x47, 0xc7, 0x45, 0x4d, 0xf1, 0xf6, 0xfa, 0xbd, 0xdd, 0xc5, 0x26, 0x01, 0x34, 0x95, 0xff, 0x6b, 0xbf, 0xee, 0x32}),
      bytes({0xe9, 0xe6, 0xcc, 0x76, 0x2c, 0x06, 0xf9, 0x41, 0x94, 0xb5, 0xea, 0x36, 0x4f, 0x9c, 0x14, 0x7c, 0xe5, 0x1f, 0x88, 0x63, 0x68, 0x50, 0x84, 0x5e, 0x42, 0xc2, 0x6b, 0x0e, 0x0c, 0xb8, 0xe7, 0x5d, 0xb4, 0x09, 0xcb, 0x4e, 0x6e, 0x08, 0x7b, 0xd7, 0xbb, 0x7a, 0x11, 0x73, 0x77, 0x4d, 0x0e, 0x9d}),
  });

  auto server_crypto = rstream::webtty::make_e2e_server_payload_crypto(session_key_grant, identity, error_code);
  assert(!error_code);
  assert(server_crypto);

  rstream::webtty::encrypted_payload encrypted;
  encrypted.m_ciphertext                      = bytes({0x98, 0xb2, 0x67, 0x1e, 0x5b, 0x71, 0x8c, 0x7f, 0x36, 0x77, 0x75, 0xb2, 0x28, 0xa7, 0x55, 0x77, 0x1b, 0x49, 0x19, 0x6c, 0x8d, 0xf5, 0x84, 0x3d, 0x9d, 0x6e, 0x11, 0x25, 0x9a, 0x1e, 0xec});
  encrypted.m_plaintext_length                = 15;
  encrypted.m_payload_crypto.m_payload_suite  = rstream::webtty::payload_cipher_suite::aes_256_gcm;
  encrypted.m_payload_crypto.m_payload_key_id = session_key_grant.m_payload_key_id;
  encrypted.m_payload_crypto.m_nonce          = bytes({0x64, 0x7e, 0x2c, 0xc6, 0xde, 0x47, 0x28, 0xf5, 0x7b, 0x89, 0x85, 0xb0});
  encrypted.m_payload_crypto.m_aad_context    = session_key_grant.m_key_context;

  rstream::webtty::byte_vector plaintext;
  server_crypto->decrypt(rstream::webtty::payload_stream::std_in, encrypted, plaintext, error_code);
  assert(!error_code);
  assert(plaintext == bytes("go-to-cpp-stdin"));
}

}  // namespace

int main()
{
  check_e2e_payload_crypto_roundtrip();
  check_e2e_payload_crypto_rejects_unsupported_suites();
  check_e2e_payload_crypto_rejects_invalid_key_ids();
  check_go_e2e_payload_crypto_vector();
  return 0;
}
