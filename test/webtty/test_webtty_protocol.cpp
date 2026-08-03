// See LICENSE file in the project root for license information.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <boost/url.hpp>

#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/test/environment.hpp>
#include <rstream/webtty/error.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/webtty.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#endif

namespace protocol = rstream::webtty::protocol;
namespace protobuf = rstream::webtty::protobuf;

using env_guard = rstream::test::environment_guard;

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

static const protocol::environment* find_env(const protocol::env_vars& env_vars, const std::string& key)
{
  for (const auto& item : env_vars) {
#ifdef _WIN32
    if (_stricmp(item.m_key.c_str(), key.c_str()) == 0) {
#else
    if (item.m_key == key) {
#endif
      return &item;
    }
  }
  return nullptr;
}

static void assert_env(const protocol::env_vars& env_vars, const std::string& key, const std::string& value)
{
  const auto* item = find_env(env_vars, key);
  assert(item != nullptr);
  assert(item->m_value == value);
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

static void check_execution_environment_is_administratively_complete_and_secret_free()
{
  env_guard path("PATH");
  env_guard lang("LANG");
  env_guard timezone("TZ");
  env_guard ssh_agent("SSH_AUTH_SOCK");
  env_guard aws_secret("AWS_SECRET_ACCESS_KEY");
  env_guard rstream_token("RSTREAM_AUTHENTICATION_TOKEN");
  path.set("/rstream/test/bin");
  lang.set("en_US.UTF-8");
  timezone.set("UTC");
  ssh_agent.set("/secret/agent.sock");
  aws_secret.set("secret");
  rstream_token.set("secret");

  protocol::user_info user_info = {
      .m_name  = "operator",
      .m_shell = "/rstream/test/shell",
      .m_home  = "/rstream/test/home",
#ifndef _WIN32
      .m_uid    = static_cast<std::uint32_t>(::getuid()),
      .m_gid    = static_cast<std::uint32_t>(::getgid()),
      .m_groups = {},
#endif
  };
  protocol::env_vars env_vars;
  protocol::add_execution_environment(env_vars, rstream::webtty::execution_mode::spawn, user_info);

  assert_env(env_vars, "PATH", "/rstream/test/bin");
  assert_env(env_vars, "LANG", "en_US.UTF-8");
  assert_env(env_vars, "TZ", "UTC");
  assert(find_env(env_vars, "SSH_AUTH_SOCK") == nullptr);
  assert(find_env(env_vars, "AWS_SECRET_ACCESS_KEY") == nullptr);
  assert(find_env(env_vars, "RSTREAM_AUTHENTICATION_TOKEN") == nullptr);
}

static void check_execution_environment_identity_policy()
{
#ifdef _WIN32
  env_guard appdata("APPDATA");
  env_guard localappdata("LOCALAPPDATA");
  env_guard powershell_modules("PSMODULEPATH");
  env_guard ssh_agent("SSH_AUTH_SOCK");
  appdata.set("C:\\Users\\operator\\AppData\\Roaming");
  localappdata.set("C:\\Users\\operator\\AppData\\Local");
  powershell_modules.set("C:\\Modules");
  ssh_agent.set("\\\\.\\pipe\\secret-agent");
#elif defined(__APPLE__)
  env_guard temporary_directory("TMPDIR");
  env_guard core_foundation_encoding("__CF_USER_TEXT_ENCODING");
  temporary_directory.set("/rstream/test/tmp");
  core_foundation_encoding.set("0x1F5:0:0");
#endif
  protocol::user_info user_info = {
      .m_name  = "resolved-user",
      .m_shell = "/resolved/shell",
      .m_home  = "/resolved/home",
#ifndef _WIN32
      .m_uid    = static_cast<std::uint32_t>(::getuid()),
      .m_gid    = static_cast<std::uint32_t>(::getgid()),
      .m_groups = {},
#endif
  };
#ifdef _WIN32
  protocol::env_vars spawn_env = {
      {.m_key = "userprofile", .m_value = "C:\\client\\home"},
  };
  protocol::add_execution_environment(spawn_env, rstream::webtty::execution_mode::spawn, user_info);
  assert_env(spawn_env, "USERPROFILE", "C:\\client\\home");

  protocol::env_vars login_env = {
      {.m_key = "userprofile", .m_value = "C:\\client\\home"},
      {.m_key = "USERNAME", .m_value = "client-user"},
  };
  protocol::add_execution_environment(login_env, rstream::webtty::execution_mode::login, user_info);
  assert_env(login_env, "USERNAME", "resolved-user");
  assert_env(login_env, "USERPROFILE", "/resolved/home");
  assert_env(login_env, "HOME", "/resolved/home");
  assert_env(login_env, "COMSPEC", "/resolved/shell");
  assert_env(login_env, "APPDATA", "C:\\Users\\operator\\AppData\\Roaming");
  assert_env(login_env, "LOCALAPPDATA", "C:\\Users\\operator\\AppData\\Local");
  assert_env(login_env, "PSMODULEPATH", "C:\\Modules");
  assert(find_env(login_env, "SSH_AUTH_SOCK") == nullptr);
#else
  protocol::env_vars spawn_env = {
      {.m_key = "HOME", .m_value = "/client/home"},
  };
  protocol::add_execution_environment(spawn_env, rstream::webtty::execution_mode::spawn, user_info);
  assert_env(spawn_env, "HOME", "/client/home");
  assert_env(spawn_env, "LOGNAME", "resolved-user");

  protocol::env_vars login_env = {
      {.m_key = "USER", .m_value = "client-user"},
      {.m_key = "LOGNAME", .m_value = "client-user"},
      {.m_key = "HOME", .m_value = "/client/home"},
      {.m_key = "SHELL", .m_value = "/client/shell"},
  };
  protocol::add_execution_environment(login_env, rstream::webtty::execution_mode::login, user_info);
  assert_env(login_env, "USER", "resolved-user");
  assert_env(login_env, "LOGNAME", "resolved-user");
  assert_env(login_env, "HOME", "/resolved/home");
  assert_env(login_env, "SHELL", "/resolved/shell");
#ifdef __APPLE__
  assert_env(login_env, "TMPDIR", "/rstream/test/tmp");
  assert_env(login_env, "__CF_USER_TEXT_ENCODING", "0x1F5:0:0");
#endif
#endif
}

#ifdef __linux__
static void check_linux_login_environment_validates_user_runtime_and_bus()
{
  char runtime_template[] = "/tmp/rstream-cpp-webtty-runtime-XXXXXX";
  const auto* runtime_dir = ::mkdtemp(runtime_template);
  assert(runtime_dir != nullptr);
  assert(::chmod(runtime_dir, 0700) == 0);
  const auto bus_path = std::string(runtime_dir) + "/bus";
  const auto bus       = ::socket(AF_UNIX, SOCK_STREAM, 0);
  assert(bus >= 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  assert(bus_path.size() < sizeof(address.sun_path));
  std::copy(bus_path.begin(), bus_path.end(), address.sun_path);
  address.sun_path[bus_path.size()] = '\0';
  assert(::bind(bus, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
  env_guard runtime_guard("XDG_RUNTIME_DIR");
  env_guard dbus_guard("DBUS_SESSION_BUS_ADDRESS");
  runtime_guard.set(runtime_dir);
  dbus_guard.set("unix:path=/untrusted/bus");

  protocol::user_info user_info = {
      .m_name   = "resolved-user",
      .m_shell  = "/resolved/shell",
      .m_home   = "/resolved/home",
      .m_uid    = static_cast<std::uint32_t>(::getuid()),
      .m_gid    = static_cast<std::uint32_t>(::getgid()),
      .m_groups = {},
  };
  protocol::env_vars env_vars = {
      {.m_key = "XDG_RUNTIME_DIR", .m_value = "/client/runtime"},
      {.m_key = "DBUS_SESSION_BUS_ADDRESS", .m_value = "unix:path=/client/bus"},
  };
  protocol::add_execution_environment(env_vars, rstream::webtty::execution_mode::login, user_info);
  assert_env(env_vars, "XDG_RUNTIME_DIR", runtime_dir);
  assert_env(env_vars, "DBUS_SESSION_BUS_ADDRESS", "unix:path=" + bus_path);

  assert(::close(bus) == 0);
  assert(::unlink(bus_path.c_str()) == 0);
  assert(::rmdir(runtime_dir) == 0);
}

static void check_linux_login_environment_rejects_public_runtime()
{
  char runtime_template[] = "/tmp/rstream-cpp-webtty-public-runtime-XXXXXX";
  const auto* runtime_dir = ::mkdtemp(runtime_template);
  assert(runtime_dir != nullptr);
  assert(::chmod(runtime_dir, 0755) == 0);
  env_guard runtime_guard("XDG_RUNTIME_DIR");
  runtime_guard.set(runtime_dir);

  protocol::user_info user_info = {
      .m_name   = "resolved-user",
      .m_shell  = "/resolved/shell",
      .m_home   = "/resolved/home",
      .m_uid    = static_cast<std::uint32_t>(::getuid()),
      .m_gid    = static_cast<std::uint32_t>(::getgid()),
      .m_groups = {},
  };
  protocol::env_vars env_vars;
  protocol::add_execution_environment(env_vars, rstream::webtty::execution_mode::login, user_info);
  const auto* selected_runtime = find_env(env_vars, "XDG_RUNTIME_DIR");
  assert(selected_runtime == nullptr || selected_runtime->m_value != runtime_dir);
  if (selected_runtime != nullptr) {
    const auto systemd_runtime = std::string("/run/user/") + std::to_string(user_info.m_uid);
    assert(selected_runtime->m_value == systemd_runtime);
    struct stat value{};
    assert(::stat(systemd_runtime.c_str(), &value) == 0);
    assert(S_ISDIR(value.st_mode));
    assert(value.st_uid == user_info.m_uid);
    assert((value.st_mode & 0077) == 0);
  }
  const auto* selected_bus = find_env(env_vars, "DBUS_SESSION_BUS_ADDRESS");
  assert(selected_bus == nullptr ||
         (selected_runtime != nullptr && selected_bus->m_value == "unix:path=" + selected_runtime->m_value + "/bus"));
  assert(::rmdir(runtime_dir) == 0);
}
#endif

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
#else
static void check_windows_login_user_is_restricted_to_server_account()
{
  protocol::user_info expected;
  std::error_code error_code;
  protocol::get_user_info(expected, error_code);
  assert(!error_code);

  auto same_name = expected.m_name;
  std::transform(same_name.begin(), same_name.end(), same_name.begin(), [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
  protocol::username same_user = protocol::identifier(std::string("DOMAIN\\") + same_name);
  protocol::user_info actual;
  protocol::get_user_info(actual, same_user, error_code);
  assert(!error_code);
  assert(actual.m_name == expected.m_name);

  protocol::username different_user = protocol::identifier(std::string("rstream-cpp-different-user"));
  protocol::get_user_info(actual, different_user, error_code);
  assert(error_code == std::make_error_code(std::errc::operation_not_permitted));

  protocol::username numeric_user = protocol::identifier(std::uint32_t{42});
  protocol::get_user_info(actual, numeric_user, error_code);
  assert(error_code == std::make_error_code(std::errc::not_supported));
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
  check_execution_environment_is_administratively_complete_and_secret_free();
  check_execution_environment_identity_policy();
#ifdef __linux__
  check_linux_login_environment_validates_user_runtime_and_bus();
  check_linux_login_environment_rejects_public_runtime();
#endif
  check_protocol_type_parsing();
  check_execution_mode_parsing();
  check_identifier_and_username_parsing();
#ifndef _WIN32
  check_user_info_error_paths_do_not_report_success();
  check_user_info_is_reentrant();
#else
  check_windows_login_user_is_restricted_to_server_account();
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
