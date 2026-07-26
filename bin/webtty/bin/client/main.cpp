// See LICENSE file in the project root for license information.

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <docopt.h>
#include <webtty_cli.hpp>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/webtty/client.hpp>
#include <rstream/webtty/webtty.hpp>

static const char USAGE[] = R"(
rstream-webtty-client - https://rstream.io/ - Web Remote Terminal client using rstream primitives

this program is distributed with the rstream C++ tools. See https://rstream.io/docs/integrations/cpp-sdk and https://github.com/rstreamlabs/rstream-cpp.

usage:
  rstream-webtty-client [options] [-e=ARG...] [-i|-I] [-t|-T] [--] [<cmd>...]
  rstream-webtty-client (-h|--help)
  rstream-webtty-client --version

options:
  -h --help                  show this screen
  --version                  show version
  -v --verbose               enable verbose mode
  --uri=ARG                  URI [default: 127.0.0.1:6002]
  -i --interactive           enable interactive mode
  -I --no-interactive        disable interactive mode
  -t --tty                   enable TTY allocation
  -T --no-tty                disable TTY allocation
  -e --env=ARG               pass environment variable
  -w --workdir=ARG           set the working directory
  -u --user=ARG              username or UID
  --transport=ARG            WebTTY transport to use [default: websocket]
  --auth-token-file=ARG      read local WebTTY bearer token from file
  --e2e                      require end-to-end encrypted WebTTY terminal content
  --identity=ARG             named local WebTTY client identity
  --identity-file=ARG        local WebTTY client identity file
  --client-credential-file=ARG local WebTTY client credential file
  --known-server=ARG         local known WebTTY server name
  --known-server-key=ARG     known WebTTY server endpoint identity
  --known-servers-file=ARG   JSON file containing known WebTTY server endpoint identities
  -j --jobs=ARG              number of threads to run simultaneously (0 = auto) [default: 0]

valid transports: plain, websocket
)";

const auto version = std::string("rstream-webtty-client ") + RSTREAM_VERSION;

struct known_server_resolution {
  std::vector<rstream::webtty::e2e_recipient> m_recipients;
  std::vector<rstream::webtty::endpoint_identity_public> m_endpoint_identities;
  std::string m_client_identity;
  bool m_configured = false;
};

static void add_known_server_recipient(std::vector<rstream::webtty::e2e_recipient>& recipients, const rstream::webtty::e2e_recipient& next)
{
  auto it = std::find_if(recipients.begin(), recipients.end(), [&next](const rstream::webtty::e2e_recipient& existing) {
    return existing.m_key_id == next.m_key_id;
  });
  if (it == recipients.end()) {
    recipients.push_back(next);
    return;
  }
  if (it->m_public_key != next.m_public_key) {
    throw std::runtime_error("conflicting known WebTTY server keys for the same key id");
  }
}

static void add_known_server_endpoint_identity(std::vector<rstream::webtty::endpoint_identity_public>& identities, const rstream::webtty::endpoint_identity_public& next)
{
  auto it = std::find_if(identities.begin(), identities.end(), [&next](const rstream::webtty::endpoint_identity_public& existing) {
    return existing.m_encryption_key_id == next.m_encryption_key_id || existing.m_signing_key_id == next.m_signing_key_id;
  });
  if (it == identities.end()) {
    identities.push_back(next);
    return;
  }
  if (it->m_encryption_public_key != next.m_encryption_public_key || it->m_signing_public_key != next.m_signing_public_key) {
    throw std::runtime_error("conflicting known WebTTY server endpoint identities");
  }
}

static void set_known_server_client_identity(std::string& value, const std::string& next)
{
  auto candidate = rstream::webtty::cli::trim_copy(next);
  if (candidate.empty()) {
    return;
  }
  if (!value.empty() && value != candidate) {
    throw std::runtime_error("multiple WebTTY client identities match this known server; pass --identity explicitly");
  }
  value = candidate;
}

static void add_unique_target_candidate(std::vector<std::string>& candidates, const std::string& raw)
{
  auto value = rstream::webtty::cli::trim_copy(raw);
  if (value.empty()) {
    return;
  }
  auto exists = std::find_if(candidates.begin(), candidates.end(), [&value](const std::string& existing) {
    return existing == value;
  });
  if (exists == candidates.end()) {
    candidates.push_back(value);
  }
}

static std::vector<std::string> known_server_target_candidates(const std::string& raw_uri)
{
  std::vector<std::string> candidates;
  auto value = rstream::webtty::cli::trim_copy(raw_uri);
  add_unique_target_candidate(candidates, value);
  auto endpoint   = value;
  auto scheme_pos = endpoint.find("://");
  if (scheme_pos != std::string::npos) {
    endpoint = endpoint.substr(scheme_pos + 3);
  }
  auto path_pos = endpoint.find_first_of("/?#");
  if (path_pos != std::string::npos) {
    endpoint = endpoint.substr(0, path_pos);
  }
  auto userinfo_pos = endpoint.rfind('@');
  if (userinfo_pos != std::string::npos) {
    endpoint = endpoint.substr(userinfo_pos + 1);
  }
  add_unique_target_candidate(candidates, endpoint);
  if (!endpoint.empty() && endpoint.front() == '[') {
    auto bracket = endpoint.find(']');
    if (bracket != std::string::npos) {
      add_unique_target_candidate(candidates, endpoint.substr(1, bracket - 1));
    }
  }
  else {
    auto first_colon = endpoint.find(':');
    auto last_colon  = endpoint.rfind(':');
    if (first_colon != std::string::npos && first_colon == last_colon) {
      add_unique_target_candidate(candidates, endpoint.substr(0, first_colon));
    }
  }
  return candidates;
}

static std::vector<rstream::webtty::cli::known_server_entry> select_known_server_entries(const std::vector<rstream::webtty::cli::known_server_entry>& entries, const std::string& uri, const std::string& raw_name)
{
  auto name = rstream::webtty::cli::trim_copy(raw_name);
  if (!name.empty()) {
    rstream::webtty::cli::validate_local_name(name, "--known-server");
    for (const auto& entry : entries) {
      if (entry.m_name == name) {
        return {entry};
      }
    }
    throw std::runtime_error("known WebTTY server \"" + name + "\" was not found; add it with rstream webtty known-server add " + name + " --key <server-endpoint-identity>");
  }
  auto candidates = known_server_target_candidates(uri);
  std::vector<rstream::webtty::cli::known_server_entry> matches;
  for (const auto& entry : entries) {
    for (const auto& candidate : candidates) {
      if (entry.m_name == candidate) {
        matches.push_back(entry);
        break;
      }
    }
  }
  if (!matches.empty()) {
    return matches;
  }
  if (entries.size() == 1) {
    return entries;
  }
  return {};
}

known_server_resolution read_known_server_resolution(const docopt::value& raw_keys, const std::string& raw_file, const std::string& raw_name, const std::string& uri, bool e2e_requested)
{
  known_server_resolution resolution;
  auto known_server_name = rstream::webtty::cli::trim_copy(raw_name);
  if (!known_server_name.empty()) {
    rstream::webtty::cli::validate_local_name(known_server_name, "--known-server");
  }
  if (raw_keys && raw_keys.isStringList()) {
    for (const auto& raw : raw_keys.asStringList()) {
      add_known_server_recipient(resolution.m_recipients, rstream::webtty::cli::parse_known_server_key(raw));
      if (std::count(raw.begin(), raw.end(), ':') == 3) {
        add_known_server_endpoint_identity(resolution.m_endpoint_identities, rstream::webtty::cli::parse_known_server_endpoint_identity(raw));
      }
    }
  }
  else if (raw_keys && raw_keys.isString()) {
    auto raw = raw_keys.asString();
    add_known_server_recipient(resolution.m_recipients, rstream::webtty::cli::parse_known_server_key(raw));
    if (std::count(raw.begin(), raw.end(), ':') == 3) {
      add_known_server_endpoint_identity(resolution.m_endpoint_identities, rstream::webtty::cli::parse_known_server_endpoint_identity(raw));
    }
  }
  auto env_key = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::known_server_key_env);
  if (!env_key.empty()) {
    add_known_server_recipient(resolution.m_recipients, rstream::webtty::cli::parse_known_server_key(env_key));
    if (std::count(env_key.begin(), env_key.end(), ':') == 3) {
      add_known_server_endpoint_identity(resolution.m_endpoint_identities, rstream::webtty::cli::parse_known_server_endpoint_identity(env_key));
    }
  }
  if (!known_server_name.empty() && !resolution.m_recipients.empty()) {
    throw std::runtime_error("--known-server cannot be combined with --known-server-key or RSTREAM_WEBTTY_KNOWN_SERVER_KEY");
  }
  auto known_servers_file            = rstream::webtty::cli::trim_copy(raw_file);
  bool known_servers_file_configured = !known_servers_file.empty();
  if (known_servers_file.empty()) {
    known_servers_file            = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::known_servers_file_env);
    known_servers_file_configured = !known_servers_file.empty();
  }
  bool known_server_uses_default_file = false;
  if (!known_server_name.empty() && known_servers_file.empty()) {
    known_servers_file             = rstream::webtty::cli::default_known_servers_path();
    known_server_uses_default_file = true;
  }
  const bool keys_configured = !resolution.m_recipients.empty() || !known_servers_file.empty() || !known_server_name.empty();
  if (e2e_requested && resolution.m_recipients.empty() && known_servers_file.empty()) {
    auto default_file = rstream::webtty::cli::default_known_servers_path();
    if (std::filesystem::exists(rstream::webtty::cli::expand_path(default_file))) {
      known_servers_file = default_file;
    }
  }
  if (!resolution.m_recipients.empty() && known_servers_file_configured) {
    throw std::runtime_error("--known-server-key cannot be combined with --known-servers-file");
  }
  if (!known_servers_file.empty()) {
    if (known_server_uses_default_file && !std::filesystem::exists(rstream::webtty::cli::expand_path(known_servers_file))) {
      throw std::runtime_error("known WebTTY server \"" + known_server_name + "\" was not found; add it with rstream webtty known-server add " + known_server_name + " --key <server-endpoint-identity>");
    }
    auto entries  = rstream::webtty::cli::load_known_server_entries_file(known_servers_file);
    auto selected = select_known_server_entries(entries, uri, known_server_name);
    if (selected.empty() && !entries.empty()) {
      throw std::runtime_error("known WebTTY servers file contains multiple entries and none matches this URI; pass --known-server for the target server or provide a dedicated --known-servers-file");
    }
    for (const auto& entry : selected) {
      add_known_server_recipient(resolution.m_recipients, entry.m_recipient);
      if (entry.m_endpoint_identity) {
        add_known_server_endpoint_identity(resolution.m_endpoint_identities, *entry.m_endpoint_identity);
      }
      set_known_server_client_identity(resolution.m_client_identity, entry.m_client_identity);
    }
  }
  resolution.m_configured = !resolution.m_recipients.empty() || !known_servers_file.empty();
  if ((e2e_requested || keys_configured) && resolution.m_recipients.empty()) {
    throw std::runtime_error("E2E client mode requires --known-server-key, --known-servers-file, RSTREAM_WEBTTY_KNOWN_SERVER_KEY, RSTREAM_WEBTTY_KNOWN_SERVERS_FILE, or ~/.rstream/webtty/known_servers.json");
  }
  return resolution;
}

int run(int argc, char** argv)
{
  auto args    = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  bool verbose = false;
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      verbose = true;
    }
  }
  if (verbose) {
    rstream::core::log::enable_ansicolor_stdout_mt();
  }
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::webtty::protocol::type protocol_type;
  rstream::webtty::protocol::parse_type(protocol_type, args.at("--transport").asString());
  auto auth_token_file = args.at("--auth-token-file") ? args.at("--auth-token-file").asString() : "";
  auto auth_token      = rstream::webtty::cli::read_auth_token(auth_token_file);
  boost::optional<std::string> auth_token_option;
  if (auth_token) {
    auth_token_option = *auth_token;
  }
  if (protocol_type == rstream::webtty::protocol::type::plain && auth_token) {
    throw std::runtime_error("plain WebTTY transport does not support HTTP bearer tokens");
  }
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(args.at("--uri").asString()),
      .m_websocket_target = protocol_type == rstream::webtty::protocol::type::websocket ? boost::optional<std::string>("/") : boost::none,
      .m_auth_token       = auth_token_option,
      .m_protocol_config  = {
           .m_protocol_type = protocol_type,
           .m_options       = {},
           .m_env_vars      = {},
           .m_cmd_args      = {},
           .m_workdir       = {},
           .m_username      = {},
      },
  };
  {
    const auto& env_vars = args.at("--env").asStringList();
    rstream::webtty::protocol::parse_environment(config.m_protocol_config.m_env_vars, env_vars);
  }
  {
    const auto& cmd = args.at("<cmd>").asStringList();
    for (const auto& arg : cmd) {
      config.m_protocol_config.m_cmd_args.push_back(arg);
    }
  }
  {
    auto interactive     = args.at("--interactive").asBool();
    auto non_interactive = args.at("--no-interactive").asBool();

    if (!interactive && !non_interactive) {
      if (config.m_protocol_config.m_cmd_args.empty()) {
        interactive = true;
      }
    }
    config.m_protocol_config.m_options.m_interactive = interactive;
  }
  {
    auto tty     = args.at("--tty").asBool();
    auto non_tty = args.at("--no-tty").asBool();
    if (!tty && !non_tty) {
      if (config.m_protocol_config.m_cmd_args.empty()) {
        tty = true;
      }
    }
    config.m_protocol_config.m_options.m_allocate_tty = tty;
  }
  config.m_protocol_config.m_options.m_send_heartbeat = true;
  {
    auto workdir = args.at("--workdir");
    if (workdir) {
      config.m_protocol_config.m_workdir = workdir.asString();
    }
  }
  {
    auto username = args.at("--user");
    if (username) {
      rstream::webtty::protocol::parse_username(config.m_protocol_config.m_username, username.asString());
    }
  }
  const bool e2e_requested                  = args.at("--e2e").asBool();
  auto identity_name                        = args.at("--identity") ? args.at("--identity").asString() : "";
  auto identity_file                        = args.at("--identity-file") ? args.at("--identity-file").asString() : "";
  auto client_credential_file               = args.at("--client-credential-file") ? args.at("--client-credential-file").asString() : "";
  auto known_server_name                    = args.at("--known-server") ? args.at("--known-server").asString() : "";
  auto known_servers_file                   = args.at("--known-servers-file") ? args.at("--known-servers-file").asString() : "";
  auto known_server_resolution              = read_known_server_resolution(args.at("--known-server-key"), known_servers_file, known_server_name, args.at("--uri").asString(), e2e_requested);
  rstream::webtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 5000,
          },
      },
      .m_std_in_buffer_size = 800 * 1024,
  };
  if (!known_server_resolution.m_recipients.empty()) {
    rstream::webtty::e2e_payload_crypto_config crypto_config;
    crypto_config.m_recipients = known_server_resolution.m_recipients;
    std::error_code error_code;
    settings.m_payload_crypto = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
    if (error_code || !settings.m_payload_crypto) {
      throw std::runtime_error("failed to configure WebTTY E2E payload crypto");
    }
  }
  identity_name = rstream::webtty::cli::trim_copy(identity_name);
  identity_file = rstream::webtty::cli::trim_copy(identity_file);
  if (!identity_name.empty() && !identity_file.empty()) {
    throw std::runtime_error("--identity cannot be combined with --identity-file");
  }
  auto env_identity = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::identity_env);
  if (!env_identity.empty() && (!identity_name.empty() || !identity_file.empty())) {
    throw std::runtime_error(std::string(rstream::webtty::cli::identity_env) + " cannot be combined with --identity or --identity-file");
  }
  if (!identity_name.empty()) {
    identity_file = rstream::webtty::cli::default_identity_path(identity_name);
  }
  if (identity_file.empty()) {
    identity_file = rstream::webtty::cli::getenv_trimmed(rstream::webtty::cli::identity_file_env);
  }
  if (identity_file.empty() && !known_server_resolution.m_client_identity.empty()) {
    identity_file = rstream::webtty::cli::default_identity_path(known_server_resolution.m_client_identity);
  }
  if (!env_identity.empty()) {
    settings.m_endpoint_identity = rstream::webtty::cli::load_identity_json(env_identity, rstream::webtty::cli::identity_env);
  }
  else if (!identity_file.empty()) {
    settings.m_endpoint_identity = rstream::webtty::cli::load_identity_file(identity_file);
  }
  settings.m_client_credential = rstream::webtty::cli::read_client_credential(client_credential_file);
  if (!settings.m_client_credential.empty() && !settings.m_endpoint_identity) {
    throw std::runtime_error("WebTTY client credential requires --identity, --identity-file, RSTREAM_WEBTTY_IDENTITY, or RSTREAM_WEBTTY_IDENTITY_FILE");
  }
  if (known_server_resolution.m_endpoint_identities.size() > 1) {
    throw std::runtime_error("C++ WebTTY client requires exactly one endpoint identity for authenticated E2E; use --known-server-key for the target server");
  }
  if (!known_server_resolution.m_endpoint_identities.empty()) {
    settings.m_expected_server_identity = known_server_resolution.m_endpoint_identities.front();
  }
  if (settings.m_expected_server_identity && !settings.m_endpoint_identity) {
    throw std::runtime_error("WebTTY server requires authenticated E2E; pass --identity, --identity-file, RSTREAM_WEBTTY_IDENTITY, RSTREAM_WEBTTY_IDENTITY_FILE, or set client_identity in the known server entry");
  }
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  signal_set.async_wait([&client](const std::error_code&, int) { client.cancel(); });
  std::pair<std::error_code, int> result;
  client.async_run([&signal_set, &result](const std::error_code& error_code, int code) {
    result = std::make_pair(error_code, code);
    signal_set.cancel();
  });
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (decltype(n) i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  if (result.first) {
    std::cerr << result.first.message() << std::endl;
  }
  return result.first ? -1 : result.second;
}

int main(int argc, char** argv)
{
  std::exception_ptr error;
  try {
    return run(argc, argv);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::cerr << "a fatal error occurred: " << rstream::core::throwable::message(error) << std::endl;
  }
  return error ? -1 : 0;
}
