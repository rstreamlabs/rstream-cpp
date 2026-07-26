// See LICENSE file in the project root for license information.

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <boost/url.hpp>

#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/webtty/error.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/webtty.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace protocol = rstream::webtty::protocol;
namespace protobuf = rstream::webtty::protobuf;

class env_guard {
 public:
  explicit env_guard(const char* key)
      : m_key(key)
  {
    const char* value = std::getenv(key);
    if (value != nullptr) {
      m_present = true;
      m_value   = value;
    }
  }

  ~env_guard()
  {
    if (m_present) {
      set(m_value);
    }
    else {
      unset();
    }
  }

  void set(const std::string& value)
  {
#ifdef _WIN32
    _putenv_s(m_key, value.c_str());
#else
    setenv(m_key, value.c_str(), 1);
#endif
  }

  void unset()
  {
#ifdef _WIN32
    _putenv_s(m_key, "");
#else
    unsetenv(m_key);
#endif
  }

 private:
  const char* m_key;
  bool m_present = false;
  std::string m_value;
};

static boost::urls::url parse_url(const std::string& uri)
{
  auto parsed = boost::urls::parse_uri(uri);
  assert(parsed);
  return boost::urls::url(parsed.value());
}

static const protocol::environment& env_at(const protocol::env_vars& env_vars, std::size_t index)
{
  auto it = env_vars.begin();
  std::advance(it, index);
  return *it;
}

static void check_environment_parsing_and_updates()
{
  env_guard inherited("RSTREAM_CPP_TEST_INHERITED");
  env_guard missing("RSTREAM_CPP_TEST_MISSING");
  inherited.set("from-process");
  missing.unset();

  protocol::env_vars env_vars;
  protocol::parse_environment(env_vars, {
                                            "A=1",
                                            "",
                                            "EMPTY=",
                                            "COMPLEX=a=b=c",
                                            "RSTREAM_CPP_TEST_INHERITED",
                                            "RSTREAM_CPP_TEST_MISSING",
                                        });

  assert(env_vars.size() == 4);
  assert(env_at(env_vars, 0).m_key == "A");
  assert(env_at(env_vars, 0).m_value == "1");
  assert(env_at(env_vars, 1).m_key == "EMPTY");
  assert(env_at(env_vars, 1).m_value.empty());
  assert(env_at(env_vars, 2).m_key == "COMPLEX");
  assert(env_at(env_vars, 2).m_value == "a=b=c");
  assert(env_at(env_vars, 3).m_key == "RSTREAM_CPP_TEST_INHERITED");
  assert(env_at(env_vars, 3).m_value == "from-process");

  auto found = protocol::find_environment_variable(env_vars, "A");
  assert(found != env_vars.end());
  protocol::add_environment_variable(env_vars, "A", "ignored");
  assert(found->m_value == "1");
  protocol::add_environment_variable(env_vars, "A", "overwritten", true);
  assert(found->m_value == "overwritten");
  protocol::add_environment_variable(env_vars, "B", "2");
  assert(protocol::find_environment_variable(env_vars, "B") != env_vars.end());
}

static void check_protocol_type_parsing()
{
  protocol::type type;
  protocol::parse_type(type, "websocket");
  assert(type == protocol::type::websocket);
  protocol::parse_type(type, "plain");
  assert(type == protocol::type::plain);
  bool rejected = false;
  try {
    protocol::parse_type(type, "ssh");
  }
  catch (const std::runtime_error& e) {
    assert(std::string(e.what()) == "invalid --transport \"ssh\" (valid: plain, websocket)");
    rejected = true;
  }
  assert(rejected);
}

static void check_execution_mode_parsing()
{
  rstream::webtty::execution_mode mode;
  rstream::webtty::parse_execution_mode(mode, "spawn");
  assert(mode == rstream::webtty::execution_mode::spawn);
  rstream::webtty::parse_execution_mode(mode, "login");
  assert(mode == rstream::webtty::execution_mode::login);
  bool rejected = false;
  try {
    rstream::webtty::parse_execution_mode(mode, "sudo");
  }
  catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

static void check_identifier_and_username_parsing()
{
  protocol::identifier identifier;
  protocol::parse_identifier(identifier, "42");
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == 42);

  protocol::parse_identifier(identifier, "0042");
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == 42);

  protocol::parse_identifier(identifier, "alice42");
  assert(identifier.type() == typeid(std::string));
  assert(boost::get<std::string>(identifier) == "alice42");

  protocol::parse_identifier(identifier, std::to_string(std::numeric_limits<std::uint32_t>::max()));
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == std::numeric_limits<std::uint32_t>::max());

  bool rejected = false;
  try {
    protocol::parse_identifier(identifier, "4294967296");
  }
  catch (const std::out_of_range&) {
    rejected = true;
  }
  assert(rejected);

  protocol::username username = protocol::identifier(std::string("stale-user"));
  protocol::parse_username(username, "");
  assert(!username);

  protocol::parse_username(username, "1000");
  assert(username);
  assert(username->type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(*username) == 1000);

  protocol::parse_username(username, "operator");
  assert(username);
  assert(username->type() == typeid(std::string));
  assert(boost::get<std::string>(*username) == "operator");
}

#ifndef _WIN32
static void check_user_info_error_paths_do_not_report_success()
{
  protocol::user_info user_info;
  std::error_code error_code;
  protocol::get_user_info(user_info, boost::none, error_code);
  assert(!error_code);
  assert(!user_info.m_name.empty());
  assert(!user_info.m_home.empty());
  assert(std::find(user_info.m_groups.begin(), user_info.m_groups.end(), user_info.m_gid) != user_info.m_groups.end());
  int group_count = getgroups(0, nullptr);
  assert(group_count >= 0);
  std::vector<gid_t> groups(static_cast<std::size_t>(group_count));
  if (group_count > 0) {
    assert(getgroups(group_count, groups.data()) == group_count);
  }
  for (auto group : groups) {
    assert(std::find(user_info.m_groups.begin(), user_info.m_groups.end(), static_cast<std::uint32_t>(group)) != user_info.m_groups.end());
  }

  protocol::username missing_by_name = protocol::identifier(std::string("rstream-cpp-user-that-should-not-exist"));
  protocol::get_user_info(user_info, missing_by_name, error_code);
  assert(error_code);

  protocol::username missing_by_id = protocol::identifier(std::numeric_limits<std::uint32_t>::max());
  protocol::get_user_info(user_info, missing_by_id, error_code);
  assert(error_code);
}

static void check_user_info_is_reentrant()
{
  protocol::user_info expected;
  std::error_code error_code;
  protocol::get_user_info(expected, boost::none, error_code);
  assert(!error_code);
  constexpr std::size_t thread_count    = 8;
  constexpr std::size_t iteration_count = 64;
  std::atomic<bool> valid               = true;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
    threads.emplace_back([&expected, &valid]() {
      for (std::size_t iteration = 0; iteration < iteration_count; ++iteration) {
        protocol::user_info actual;
        std::error_code actual_error;
        protocol::get_user_info(actual, boost::none, actual_error);
        if (actual_error || actual.m_name != expected.m_name || actual.m_shell != expected.m_shell || actual.m_home != expected.m_home || actual.m_uid != expected.m_uid || actual.m_gid != expected.m_gid || actual.m_groups != expected.m_groups) {
          valid.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  assert(valid.load(std::memory_order_relaxed));
}
#endif

static void check_webtty_uri_is_publishable_and_labelled()
{
  const auto uri = rstream::webtty::build_webtty_uri();
  const auto url = parse_url(uri);
  assert(url.scheme() == "rstrm");

  auto params = url.params();
  assert(params.find("rstrm.publish") != params.end());
  assert((*params.find("rstrm.publish")).value == "true");
  assert((*params.find("rstrm.protocol")).value == "http");
  assert((*params.find("rstrm.token_auth")).value == "true");

  std::set<std::string> labels;
  for (const auto& param : params) {
    if (param.key == "rstrm.labels") {
      labels.insert(std::string(param.value));
    }
  }
  assert(labels.count("application-protocol=rstream.webtty") == 1);
  assert(labels.count("rstream.webtty.capabilities=exec") == 1);
  assert(labels.count("rstream.webtty.execution.mode=spawn") == 1);
  assert(labels.count("rstream.webtty.exec.path=/") == 1);
  assert(labels.count("rstream.webtty.e2e=disabled") == 1);
  assert(labels.count("rstream.webtty.client_proof=none") == 1);
  bool has_os_family = false;
  for (const auto& label : labels) {
    if (label.find("rstream.webtty.os_family=") == 0 && label.size() > std::string("rstream.webtty.os_family=").size()) {
      has_os_family = true;
      break;
    }
  }
  assert(has_os_family);
}

static void check_managed_webtty_uri_is_publishable_and_labelled()
{
  rstream::webtty::webtty_uri_options options;
  options.m_managed           = true;
  options.m_server_id         = "prod-shell";
  options.m_host_key_id       = "host-key-id";
  options.m_encryption_policy = "explicit_key";
  options.m_labels["env"]     = "production";
  const auto uri              = rstream::webtty::build_webtty_uri(options);
  const auto url              = parse_url(uri);
  auto params                 = url.params();
  assert(url.host() == "prod-shell");
  assert((*params.find("rstrm.publish")).value == "true");
  assert((*params.find("rstrm.protocol")).value == "webtty");
  assert((*params.find("rstrm.type")).value == "bytestream");
  assert((*params.find("rstrm.token_auth")).value == "true");
  std::set<std::string> labels;
  for (const auto& param : params) {
    if (param.key == "rstrm.labels") {
      labels.insert(std::string(param.value));
    }
  }
  assert(labels.count("application-protocol=rstream.webtty") == 1);
  assert(labels.count("rstream.webtty.server_id=prod-shell") == 1);
  assert(labels.count("rstream.webtty.host_key_id=host-key-id") == 1);
  assert(labels.count("rstream.webtty.e2e=required") == 1);
  assert(labels.count("rstream.webtty.client_proof=required") == 1);
  assert(labels.count("rstream.webtty.encryption_policy=explicit_key") == 1);
  assert(labels.count("rstream.webtty.label.env=production") == 1);
}

static void check_managed_webtty_admission_label_reaches_tunnel_properties()
{
  rstream::webtty::webtty_uri_options options;
  options.m_managed                = true;
  options.m_publish                = false;
  options.m_server_id              = "prod-shell";
  options.m_host_key_id            = "host-key-id";
  options.m_encryption_policy      = "workspace_managed";
  options.m_server_admission_label = "signed-admission-proof";
  options.m_labels["env"]          = "production";
  const auto uri                   = rstream::webtty::build_webtty_uri(options);
  rstream::io_rstrm::settings_acceptor settings;
  boost::system::error_code error_code;
  rstream::io_rstrm::parse_settings_acceptor(parse_url(uri), settings, error_code);
  assert(!error_code);
  assert(settings.m_tunnel_properties.m_protocol);
  assert(settings.m_tunnel_properties.m_protocol.value() == "webtty");
  assert(settings.m_tunnel_properties.m_labels.at("rstream.webtty.server_admission") == "signed-admission-proof");
  assert(settings.m_tunnel_properties.m_labels.at("rstream.webtty.server_id") == "prod-shell");
  assert(settings.m_tunnel_properties.m_labels.at("rstream.webtty.label.env") == "production");
}

static void check_private_managed_webtty_uri_omits_token_auth()
{
  rstream::webtty::webtty_uri_options options;
  options.m_managed   = true;
  options.m_publish   = false;
  options.m_server_id = "private-shell";
  const auto uri      = rstream::webtty::build_webtty_uri(options);
  const auto url      = parse_url(uri);
  auto params         = url.params();
  assert(url.host() == "private-shell");
  assert((*params.find("rstrm.publish")).value == "false");
  assert((*params.find("rstrm.protocol")).value == "webtty");
  assert((*params.find("rstrm.type")).value == "bytestream");
  assert(params.find("rstrm.token_auth") == params.end());
}

static void check_e2e_crypto_protobuf_contract()
{
  protobuf::Message message;
  auto* open = message.mutable_open();
  open->add_capabilities(protobuf::OPEN_CAPABILITY_ENCRYPTED_PAYLOAD);
  open->add_capabilities(protobuf::OPEN_CAPABILITY_SESSION_CRYPTO);
  auto* session_key_grant = open->mutable_session_key_grant();
  session_key_grant->set_payload_suite(protobuf::PAYLOAD_CIPHER_SUITE_AES_256_GCM);
  session_key_grant->set_payload_key_id("workspace-key");
  session_key_grant->set_key_envelope_suite(protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
  auto* key_envelope = session_key_grant->add_key_envelopes();
  key_envelope->set_recipient_key_id("recipient-key");
  key_envelope->set_encapsulated_key("hpke-enc");
  key_envelope->set_wrapped_key("wrapped-key");
  session_key_grant->set_key_context("key-context");

  std::string bytes;
  assert(message.SerializeToString(&bytes));
  protobuf::Message parsed;
  assert(parsed.ParseFromString(bytes));
  assert(parsed.open().capabilities_size() == 2);
  assert(parsed.open().capabilities(0) == protobuf::OPEN_CAPABILITY_ENCRYPTED_PAYLOAD);
  assert(parsed.open().capabilities(1) == protobuf::OPEN_CAPABILITY_SESSION_CRYPTO);
  assert(parsed.open().session_key_grant().payload_suite() == protobuf::PAYLOAD_CIPHER_SUITE_AES_256_GCM);
  assert(parsed.open().session_key_grant().payload_key_id() == "workspace-key");
  assert(parsed.open().session_key_grant().key_envelope_suite() == protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
  assert(parsed.open().session_key_grant().key_envelopes_size() == 1);
  assert(parsed.open().session_key_grant().key_envelopes(0).recipient_key_id() == "recipient-key");
  assert(parsed.open().session_key_grant().key_envelopes(0).encapsulated_key() == "hpke-enc");
  assert(parsed.open().session_key_grant().key_envelopes(0).wrapped_key() == "wrapped-key");
  assert(parsed.open().session_key_grant().key_context() == "key-context");

  protobuf::Message data_message;
  auto* data = data_message.mutable_data();
  data->set_type(protobuf::Data::TYPE_STDIN);
  auto* encrypted = data->mutable_encrypted_data();
  encrypted->set_ciphertext("ciphertext");
  encrypted->set_plaintext_length(9);
  encrypted->mutable_payload_crypto()->set_payload_key_id("payload-key");
  encrypted->mutable_payload_crypto()->set_nonce("payload-nonce");
  encrypted->mutable_payload_crypto()->set_aad_context("payload-context");
  assert(data->has_encrypted_data());
  assert(data->payload_case() == protobuf::Data::PayloadCase::kEncryptedData);
  assert(data->encrypted_data().ciphertext() == "ciphertext");
  assert(data->encrypted_data().plaintext_length() == 9);
  assert(data->encrypted_data().payload_crypto().payload_key_id() == "payload-key");
  assert(data->encrypted_data().payload_crypto().nonce() == "payload-nonce");
  assert(data->encrypted_data().payload_crypto().aad_context() == "payload-context");
}

static void check_attach_protobuf_contract()
{
  protobuf::Message message;
  auto* attach = message.mutable_attach();
  attach->set_session_id("session-1");
  attach->set_participant_id("participant-1");
  attach->set_attach_grant("grant");
  attach->set_requested_role(protobuf::ATTACH_ROLE_SPECTATOR);
  attach->set_transport(protobuf::ATTACH_TRANSPORT_WEBSOCKET);
  attach->add_capabilities(protobuf::ATTACH_CAPABILITY_READ_STREAM);
  attach->add_capabilities(protobuf::ATTACH_CAPABILITY_REQUEST_CONTROL);
  attach->mutable_device_id()->set_value("device-1");
  attach->mutable_browser_id()->set_value("browser-1");

  std::string bytes;
  assert(message.SerializeToString(&bytes));
  protobuf::Message parsed;
  assert(parsed.ParseFromString(bytes));
  assert(parsed.payload_case() == protobuf::Message::PayloadCase::kAttach);
  assert(parsed.attach().session_id() == "session-1");
  assert(parsed.attach().participant_id() == "participant-1");
  assert(parsed.attach().attach_grant() == "grant");
  assert(parsed.attach().requested_role() == protobuf::ATTACH_ROLE_SPECTATOR);
  assert(parsed.attach().transport() == protobuf::ATTACH_TRANSPORT_WEBSOCKET);
  assert(parsed.attach().capabilities_size() == 2);
  assert(parsed.attach().capabilities(0) == protobuf::ATTACH_CAPABILITY_READ_STREAM);
  assert(parsed.attach().capabilities(1) == protobuf::ATTACH_CAPABILITY_REQUEST_CONTROL);
  assert(parsed.attach().device_id().value() == "device-1");
  assert(parsed.attach().browser_id().value() == "browser-1");
}

static void check_error_category_and_messages()
{
  assert(std::string(rstream::webtty::error::rstream_webtty_error_category().name()) == "rstream::webtty::error::category");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::success) == "success");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::client_error) == "client error");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::invalid_state) == "invalid state");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::not_a_tty) == "terminal is not a TTY");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::operation_aborted) == "operation aborted");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::operation_timeout) == "operation timeout");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::protocol_error) == "protocol error");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::server_error) == "server error");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::known_server_required) == "known WebTTY server endpoint identity is required");
  assert(
      rstream::webtty::to_string(rstream::webtty::error::code::server_endpoint_identity_mismatch) == "WebTTY server endpoint identity does not match the configured known server");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::server_proof_invalid) == "WebTTY server proof is invalid");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::client_identity_required) == "WebTTY client identity is required");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::client_proof_required) == "WebTTY client proof is required");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::client_proof_invalid) == "WebTTY client proof is invalid");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::client_unauthorized) == "WebTTY client signing key is not authorized");
  assert(
      rstream::webtty::to_string(rstream::webtty::error::code::managed_attach_unsupported) == "managed WebTTY attach is handled by the rstream engine; direct WebTTY servers accept only new Open sessions");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::unexpected_message) == "unexpected message");
  assert(rstream::webtty::to_string(rstream::webtty::error::code::unknown_undefined_error) == "error is unknown / undefined");
  assert(rstream::webtty::to_string(static_cast<rstream::webtty::error::code>(9999)) == "unknown error");

  auto code = rstream::webtty::error::make_error_code(static_cast<int>(rstream::webtty::error::code::server_error));
  assert(code);
  assert(code.message() == "server error");
  assert(code.category() == rstream::webtty::error::rstream_webtty_error_category());
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_environment_parsing_and_updates();
  check_protocol_type_parsing();
  check_execution_mode_parsing();
  check_identifier_and_username_parsing();
#ifndef _WIN32
  check_user_info_error_paths_do_not_report_success();
  check_user_info_is_reentrant();
#endif
  check_webtty_uri_is_publishable_and_labelled();
  check_managed_webtty_uri_is_publishable_and_labelled();
  check_managed_webtty_admission_label_reaches_tunnel_properties();
  check_private_managed_webtty_uri_omits_token_auth();
  check_e2e_crypto_protobuf_contract();
  check_attach_protobuf_contract();
  check_error_category_and_messages();
  return 0;
}
