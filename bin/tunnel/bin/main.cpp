// See LICENSE file in the project root for license information.

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/optional.hpp>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/metrics.hpp>
#include <rstream/tunnel/proxy.hpp>
#include <rstream/tunnel/tunnel.hpp>

#ifdef RSTREAM_WITH_NCURSES
#include "ncurses.hpp"
#endif

static const char USAGE[] = R"(
rstream-tunnel - https://rstream.io/ - serverless networking

this program is part of rstream (https://rstream.io/download) and was created using rstream C++ SDK

description:
  rstream-tunnel is a command-line tool that allows you to create a secure tunnel from your local machine to the global network.
  It is based on the rstream C++ SDK and provides a simple way to create a secure tunnel to the global network.

usage:
  rstream-tunnel [<target>] [--publish|--no-publish] [--http|--tls] [--label=ARG]... [--no-token|--token=ARG] [--retry|--no-retry] [options]
  rstream-tunnel (-h|--help)
  rstream-tunnel --version

target has the following format: [host]:port

options:
  -h --help                             show this screen
  --version                             show version
  -v --verbose                          enable verbose mode
  -c --config=ARG                       path to rstream configuration file
  -f --format=ARG                       output format

valid output formats: human, human-pretty, json, json-pretty

tunnel options:
  --name=ARG                            tunnel name
  --publish                             require a published tunnel
  --no-publish                          do not publish the tunnel
  --http                                use HTTP protocol
  --tls                                 use TLS protocol
  --label=ARG                           set a label for the tunnel (might be specified multiple times)

security options:
  --geoip=ARG                           specify allowed countries (ISO 3166-1 alpha-2) (comma separated list)
  --trusted-ips=ARG                     specify allowed IPs or CIDR ranges (comma separated)

publishing options:
  --host=ARG                            stable domain for publishing
  --upstream-tls                        proxy the connection to upstream using TLS

tls options:
  --tls-mode=ARG                        specify the TLS mode (terminated, passthrough)
  --tls-alpn=ARG                        specify the ALPN protocols (comma separated list)

publishing (terminated tunnels) options:
  --tls-min-version=ARG                 specify the minimum TLS version (tls1.2, tls1.3)
  --tls-ciphers=ARG                     specify the allowed TLS ciphers (comma separated list)
  --mtls                                enable mTLS Tunnel access

http tunnel options:
  --http-version=ARG                    specify the HTTP version (http/1.1, h2c)
  --http-use-tls                        proxy HTTP upstream using TLS (deprecated; use --upstream-tls)
  --token-auth                          enable token based authentication
  --rstream-auth                        require rstream account authentication
  --challenge-mode                      require an interactive challenge before access

authentication options:
  --no-token                            disable token based authentication
  --token=ARG                           authentication token to use

metrics and monitoring options:
  -m --metrics                          expose prometheus metrics
  --metrics-addr=ARG                    address to expose metrics on [default: 127.0.0.1:9090]

rstream protocol options:
  --engine=URL                          specify the rstream engine address
  --connect-timeout=ARG                 set the connection timeout in milliseconds [default: 10000]
  --retry                               enable automatic reconnection on disconnect
  --no-retry                            disable automatic reconnection on disconnect
  --retry-interval=ARG                  set the retry interval in milliseconds [default: 5000]

other options:
  -j --jobs=ARG                         number of threads to run simultaneously (0 = auto) [default: 0]
  --buffer-size=ARG                     buffer size in bytes expressed as a power of 2 [default: 22]

environment variables:
  RSTREAM_CONFIG                        path to the rstream configuration file
  RSTREAM_API_URL                       Control plane API URL override
  RSTREAM_CONTEXT                       default context name
  RSTREAM_ENGINE                        engine host:port override
  RSTREAM_ENGINE_ADDRESS                full engine address override
  RSTREAM_AUTHENTICATION_TOKEN          authentication token override
  RSTREAM_MTLS_CERT_FILE                client certificate file for mTLS engine authentication
  RSTREAM_MTLS_KEY_FILE                 client private key file for mTLS engine authentication

additional tips:
  - Use the `--verbose` option for troubleshooting and detailed operation logs.
  - Further information can be found in the rstream documentation (https://rstream.io/docs).
)";

const auto version = std::string("rstream-tunnel ") + RSTREAM_VERSION;

static rstream::core::log::str_sink g_str_sink = nullptr;

enum class format {
  human,
#ifdef RSTREAM_WITH_NCURSES
  human_pretty,
#endif
  json,
  json_pretty,
};

static void parse_format(format& dst, const std::string& src);

static std::vector<std::string> split(const std::string& str, char delimiter);

static rstream::io::address make_target_address(const std::map<std::string, docopt::value>& args);

static void on_status(format format, const rstream::tunnel::status_proxy& status);

static void on_new_connection(format format, const rstream::io_rstrm::endpoint& endpoint);

static void log(const std::string& str);

static void log(const nlohmann::json& json, bool pretty);

static void log(const rstream::tunnel::status_proxy& status);

static void log(const rstream::io_rstrm::endpoint& endpoint);

int run(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  boost::optional<std::string> config_path;
  {
    auto it = args.find("--config");
    if (it != args.end() && it->second.operator bool()) {
      config_path = it->second.asString();
    }
  }
  auto verbose = false;
  {
    auto arg = args.at("--verbose");
    if (arg) {
      verbose = arg.asBool();
    }
  }
#ifdef RSTREAM_WITH_NCURSES
#ifdef _WIN32
  auto is_tty = _isatty(_fileno(stdout));
#else
  auto is_tty = isatty(STDOUT_FILENO);
#endif
  auto format = (!verbose && is_tty) ? format::human_pretty : format::human;
#else
  auto format = format::human;
#endif
  {
    auto arg = args.at("--format");
    if (arg) {
      parse_format(format, arg.asString());
    }
  }
#ifdef RSTREAM_WITH_NCURSES
  if (verbose && format == format::human_pretty) {
    throw std::runtime_error("verbose mode cannot be used in conjonction with 'human-pretty' output format");
  }
#endif
  if (verbose) {
    if (format == format::human) {
      g_str_sink = rstream::core::log::enable_ansicolor_stdout_mt();
    }
    else {
      g_str_sink = rstream::core::log::enable_json_stdout_mt(format == format::json_pretty ? true : false);
    }
  }
  rstream::core::default_logger()->info(version);
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  bool metrics = false;
  {
    auto it = args.find("--metrics");
    if (it != args.end() && it->second.asBool()) {
      metrics = true;
    }
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  boost::optional<std::string> name;
  {
    auto it = args.find("--name");
    if (it != args.end() && it->second.operator bool()) {
      name = it->second.asString();
    }
  }
  boost::optional<std::string> engine;
  {
    auto it = args.find("--engine");
    if (it != args.end() && it->second.operator bool()) {
      engine = it->second.asString();
    }
  }
  auto local_endpoint = rstream::io_rstrm::make_endpoint(name, engine, config_path);
  if (!local_endpoint) {
    throw std::runtime_error("failed to create local endpoint");
  }
  rstream::io_rstrm::settings_acceptor settings_acceptor;
  rstream::io_rstrm::config_client config_client;
  if (config_path) {
    config_client.m_config_path = config_path;
  }
  {
    auto it = args.find("--no-token");
    if (it != args.end() && it->second.asBool()) {
      config_client.m_no_token = true;
    }
  }
  {
    auto it = args.find("--token");
    if (it != args.end() && it->second.operator bool()) {
      config_client.m_token = it->second.asString();
    }
  }
  {
    auto it = args.find("--connect-timeout");
    if (it != args.end() && it->second.operator bool()) {
      config_client.m_connection_timeout_ms = static_cast<unsigned int>(std::max((long)0, it->second.asLong()));
    }
  }
  settings_acceptor.m_config = config_client;
  {
    auto it = args.find("--retry");
    if (it != args.end() && it->second.operator bool()) {
      settings_acceptor.m_auto_reconnect = true;
    }
  }
  {
    auto it = args.find("--no-retry");
    if (it != args.end() && it->second.asBool()) {
      settings_acceptor.m_auto_reconnect = false;
    }
  }
  {
    auto it = args.find("--retry-interval");
    if (it != args.end() && it->second.operator bool()) {
      settings_acceptor.m_reconnect_timeout_ms = static_cast<unsigned int>(std::max((long)0, it->second.asLong()));
    }
  }
  rstream::io_rstrm::tunnel_properties tunnel_properties;
  {
    auto it = args.find("--publish");
    if (it != args.end() && it->second.asBool()) {
      tunnel_properties.m_publish = true;
    }
  }
  {
    auto it = args.find("--no-publish");
    if (it != args.end() && it->second.asBool()) {
      tunnel_properties.m_publish = false;
    }
  }
  bool publish = true;
  if (tunnel_properties.m_publish) {
    publish = tunnel_properties.m_publish.value();
  }
  bool flag_http = false;
  {
    auto it = args.find("--http");
    if (it != args.end() && it->second.asBool()) {
      flag_http = true;
    }
  }
  bool flag_tls = false;
  {
    auto it = args.find("--tls");
    if (it != args.end() && it->second.asBool()) {
      flag_tls = true;
    }
  }
  if (publish) {
    if (flag_http) {
      tunnel_properties.m_protocol = rstream::io_rstrm::protocol::http;
    }
    if (flag_tls) {
      tunnel_properties.m_protocol = rstream::io_rstrm::protocol::tls;
    }
  }
  bool is_tls_protocol = tunnel_properties.m_protocol && tunnel_properties.m_protocol.value() == rstream::io_rstrm::protocol::tls;
  {
    auto it = args.find("--label");
    if (it != args.end() && it->second.operator bool()) {
      const auto& labels = it->second.asStringList();
      for (const auto& label : labels) {
        auto pos = label.find('=');
        if (pos != std::string::npos) {
          tunnel_properties.m_labels[label.substr(0, pos)] = label.substr(pos + 1);
        }
        else {
          rstream::core::default_logger()->warn("ignoring invalid label: {}", label);
        }
      }
    }
  }
  {
    auto it = args.find("--geoip");
    if (it != args.end() && it->second.operator bool()) {
      tunnel_properties.m_geoip = split(it->second.asString(), ',');
    }
  }
  {
    auto it = args.find("--trusted-ips");
    if (it != args.end() && it->second.operator bool()) {
      tunnel_properties.m_trusted_ips = split(it->second.asString(), ',');
    }
  }
  if (publish) {
    {
      auto it = args.find("--host");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_hostname = it->second.asString();
      }
    }
    {
      auto it = args.find("--upstream-tls");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_upstream_tls = true;
      }
    }
    {
      auto it = args.find("--mtls");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_mtls_auth = true;
      }
    }
  }
  if (publish && is_tls_protocol) {
    {
      auto it = args.find("--tls-mode");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_tls_mode = it->second.asString();
      }
    }
    {
      auto it = args.find("--tls-alpn");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_tls_alpns = split(it->second.asString(), ',');
      }
    }
    {
      auto it = args.find("--tls-min-version");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_tls_min_version = it->second.asString();
      }
    }
    {
      auto it = args.find("--tls-ciphers");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_tls_ciphers = split(it->second.asString(), ',');
      }
    }
    if (tunnel_properties.m_tls_mode
        && tunnel_properties.m_tls_mode.value() == rstream::io_rstrm::tls_mode::passthrough) {
      if (tunnel_properties.m_upstream_tls && tunnel_properties.m_upstream_tls.value()) {
        throw std::runtime_error("TLS passthrough cannot be combined with --upstream-tls");
      }
      if (!tunnel_properties.m_tls_alpns.empty()) {
        throw std::runtime_error("TLS passthrough cannot be combined with --tls-alpn");
      }
      if (tunnel_properties.m_tls_min_version
          || (tunnel_properties.m_mtls_auth && tunnel_properties.m_mtls_auth.value())) {
        throw std::runtime_error("TLS passthrough cannot be combined with server-side TLS policy or mTLS");
      }
    }
  }
  if (publish) {
    bool is_http_protocol = !tunnel_properties.m_protocol
                            || tunnel_properties.m_protocol.value() == rstream::io_rstrm::protocol::http;
    {
      auto it = args.find("--http-version");
      if (it != args.end() && it->second.operator bool()) {
        tunnel_properties.m_http_version = it->second.asString();
      }
    }
    {
      auto it = args.find("--http-use-tls");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_http_use_tls = true;
      }
    }
    if (is_http_protocol && tunnel_properties.m_upstream_tls && !tunnel_properties.m_http_use_tls) {
      tunnel_properties.m_http_use_tls = tunnel_properties.m_upstream_tls;
    }
    {
      auto it = args.find("--token-auth");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_token_auth = true;
      }
    }
    {
      auto it = args.find("--rstream-auth");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_rstream_auth = true;
      }
    }
    {
      auto it = args.find("--challenge-mode");
      if (it != args.end() && it->second.asBool()) {
        tunnel_properties.m_challenge_mode = true;
      }
    }
  }
  settings_acceptor.m_tunnel_properties       = tunnel_properties;
  rstream::tunnel::proxy::config config_proxy = {
      .m_local_endpoint    = local_endpoint.value(),
      .m_target_address    = make_target_address(args),
      .m_settings_acceptor = settings_acceptor,
  };
  auto buffer_size                               = static_cast<std::uint32_t>(std::max((long)0, (long)std::pow(2, args.at("--buffer-size").asLong())));
  rstream::tunnel::settings_proxy settings_proxy = {
      .m_read_downstream_buffer_size_bytes = buffer_size,
      .m_read_upstream_buffer_size_bytes   = buffer_size,
      .m_timeouts_ms                       = {
                                .m_open = 5000,
      },
  };
  rstream::tunnel::proxy proxy(io_context.get_executor(), config_proxy, settings_proxy);
  rstream::io::metrics::exposer::config exposer_config = {
      .m_address = args.at("--metrics-addr").asString(),
  };
  rstream::io::metrics::settings_exposer settings_exposer = {
      .m_timeouts_start_ms = 5000,
  };
  rstream::io::metrics::exposer exposer(io_context.get_executor(), exposer_config, settings_exposer);
#ifdef RSTREAM_WITH_NCURSES
  ncurses ncurses(io_context.get_executor());
  auto cancel = [&proxy, &exposer, &ncurses, &signal_set]() {
    proxy.cancel();
    exposer.cancel();
    ncurses.cancel();
    signal_set.cancel();
  };
#else
  auto cancel = [&proxy, &exposer, &signal_set]() {
    proxy.cancel();
    exposer.cancel();
    signal_set.cancel();
  };
#endif
  std::error_code result;
  auto completion_handler = [&result, cancel](const std::error_code& error_code) {
    if (!result && error_code) {
      result = error_code;
    }
    cancel();
  };
  signal_set.async_wait(std::bind(cancel));
  rstream::tunnel::proxy::callbacks callbacks = {
      .m_on_status_cb         = nullptr,
      .m_on_new_connection_cb = nullptr,
  };
#ifdef RSTREAM_WITH_NCURSES
  if (format == format::human_pretty) {
    callbacks.m_on_status_cb         = std::bind(&ncurses::render_status, &ncurses, std::placeholders::_1);
    callbacks.m_on_new_connection_cb = std::bind(&ncurses::render_new_connection, &ncurses, std::placeholders::_1);
  }
  else
#endif
  {
    callbacks.m_on_status_cb         = std::bind(&on_status, format, std::placeholders::_1);
    callbacks.m_on_new_connection_cb = std::bind(&on_new_connection, format, std::placeholders::_1);
  }
  proxy.async_run(callbacks, completion_handler);
  if (metrics) {
    exposer.async_run(completion_handler);
  }
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (decltype(n) i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
#ifdef RSTREAM_WITH_NCURSES
  if (format == format::human_pretty) {
    ncurses.async_run(completion_handler);
  }
#endif
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
#ifdef RSTREAM_WITH_NCURSES
  ncurses.join();
#endif
  if (result) {
    std::cerr << result.message() << std::endl;
  }
  return result ? -1 : 0;
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

void parse_format(format& dst, const std::string& src)
{
  if (src == "human") {
    dst = format::human;
  }
  else if (src == "human-pretty") {
#ifdef RSTREAM_WITH_NCURSES
    dst = format::human_pretty;
#else
    throw std::runtime_error("pretty human output format is not supported");
#endif
  }
  else if (src == "json") {
    dst = format::json;
  }
  else if (src == "json-pretty") {
    dst = format::json_pretty;
  }
  else {
    throw std::runtime_error("invalid output format '" + src + "'");
  }
}

std::vector<std::string> split(const std::string& str, char delimiter)
{
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream token_stream(str);
  while (std::getline(token_stream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

rstream::io::address make_target_address(const std::map<std::string, docopt::value>& args)
{
  auto it = args.find("<target>");
  if (it != args.end() && it->second.operator bool()) {
    return rstream::io::address(it->second.asString());
  }
  else {
    return rstream::io::address("8080");
  }
}

void on_status(format format, const rstream::tunnel::status_proxy& status)
{
  if (format == format::human) {
    log(status);
  }
  else if (format == format::json || format == format::json_pretty) {
    nlohmann::json json;
    json["type"] = "tunnel_status";
    json << status;
    log(json, format == format::json_pretty);
  }
}

void on_new_connection(format format, const rstream::io_rstrm::endpoint& endpoint)
{
  if (format == format::human) {
    log(endpoint);
  }
  else if (format == format::json || format == format::json_pretty) {
    nlohmann::json json;
    json["type"] = "new_connection";
    json << endpoint;
    log(json, format == format::json_pretty);
  }
}

void log(const std::string& str)
{
  if (g_str_sink) {
    g_str_sink(str);
  }
  else {
    std::cout << str << std::endl;
  }
}

void log(const nlohmann::json& json, bool pretty)
{
  std::stringstream str;
  str << json.dump(pretty ? 2 : -1);
  log(str.str());
}

void log(const rstream::tunnel::status_proxy& status)
{
  std::stringstream str;
  str << "tunnel status" << std::endl
      << status;
  log(str.str());
}

void log(const rstream::io_rstrm::endpoint& endpoint)
{
  std::stringstream str;
  str << "new connection" << std::endl
      << "  " << endpoint;
  log(str.str());
}
