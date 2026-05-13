// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/url.hpp>
#include <openssl/opensslv.h>

#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io/detail/stream/error.hpp>

class env_guard {
 public:
  explicit env_guard(const char* key)
      : m_key(key)
  {
    const char* value = std::getenv(key);
    if (value) {
      m_present = true;
      m_value   = value;
    }
  }
  ~env_guard()
  {
    if (m_present) {
      setenv(m_key, m_value.c_str(), 1);
    }
    else {
      unsetenv(m_key);
    }
  }
  void set(const std::string& value)
  {
    setenv(m_key, value.c_str(), 1);
  }
  void unset()
  {
    unsetenv(m_key);
  }
 private:
  const char* m_key;
  bool m_present = false;
  std::string m_value;
};

static std::string default_engine_tls_groups_query()
{
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
  return "&ssl.groups=SecP256r1MLKEM768%3ASecP384r1MLKEM1024%3AX25519MLKEM768%3AX25519%3Asecp256r1%3Asecp384r1";
#else
  return "&ssl.groups=X25519%3Asecp256r1%3Asecp384r1";
#endif
}

static boost::urls::url parse_url(const std::string& uri)
{
  auto parsed = boost::urls::parse_uri(uri);
  assert(parsed);
  return boost::urls::url(parsed.value());
}

static void assert_error_code(const boost::system::error_code& actual, int value, const char* category)
{
  if (actual.value() != value || std::string(actual.category().name()) != category) {
    std::cerr << "unexpected error code: value=" << actual.value()
              << " category=" << actual.category().name()
              << " message=" << actual.message() << std::endl;
    assert(false);
  }
}

static void check_parse_settings_rejects_token_conflict()
{
  auto url = parse_url("rstrm://edge?rstream.no_token=true&rstream.token=secret");
  rstream::io_rstrm::settings_socket settings;
  boost::system::error_code error_code;
  rstream::io_rstrm::parse_settings_socket(url, settings, error_code);
  assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration), rstream::io_rstrm::error::rstream_rstream_error_category().name());
}

static void check_parse_settings_socket_token_rules()
{
  rstream::io_rstrm::settings_socket settings;
  boost::system::error_code error_code;
  rstream::io_rstrm::parse_settings_socket(parse_url("rstrm://edge?rstream.token=uri-token"), settings, error_code);
  assert(!error_code);
  assert(settings.m_config.m_token);
  assert(settings.m_config.m_token.value() == "uri-token");
  assert(settings.m_config.m_token_from_uri_param);

  rstream::io_rstrm::settings_socket invalid_settings;
  rstream::io_rstrm::parse_settings_socket(parse_url("rstrm://edge?rstream.token"), invalid_settings, error_code);
  assert_error_code(error_code, static_cast<int>(rstream::io::detail::stream::error::code::invalid_argument), rstream::io::detail::stream::error::rstream_io_detail_stream_error_category().name());
}

static void check_parse_settings_rejects_invalid_boolean()
{
  auto url = parse_url("rstrm://edge?rstream.retry=maybe&rstrm.publish=true");
  rstream::io_rstrm::settings_acceptor settings;
  boost::system::error_code error_code;
  rstream::io_rstrm::parse_settings_acceptor(url, settings, error_code);
  assert_error_code(error_code, static_cast<int>(rstream::io::detail::stream::error::code::invalid_argument), rstream::io::detail::stream::error::rstream_io_detail_stream_error_category().name());
}

static void check_parse_settings_acceptor_policy_controls()
{
  auto url = parse_url("rstrm://edge?rstream.retry=false&rstrm.type=bytestream&rstrm.publish=false&rstrm.protocol=http&rstrm.labels=ignored&rstrm.labels=env%3Dprod&rstrm.labels=env%3Dstaging&rstrm.labels=tier%3Dedge&rstrm.geoip=FR,US&rstrm.trusted_ips=203.0.113.0%2F24,198.51.100.12%2F32&rstrm.hostname=api.example&rstrm.host=legacy.example&rstrm.tls_mode=terminated&rstrm.tls_alpns=h2,http%2F1.1&rstrm.tls_min_version=tls1.2&rstrm.tls_ciphers=TLS_AES_128_GCM_SHA256,TLS_AES_256_GCM_SHA384&rstrm.mtls_auth&rstrm.http_version=h2&rstrm.http_use_tls=false&rstrm.token_auth=true&rstrm.rstream_auth=true&rstrm.challenge_mode=true&rstrm.upstream_tls=true");
  rstream::io_rstrm::settings_acceptor settings;
  boost::system::error_code error_code;
  rstream::io_rstrm::parse_settings_acceptor(url, settings, error_code);
  assert(!error_code);
  assert(!settings.m_auto_reconnect);
  assert(settings.m_tunnel_properties.m_type);
  assert(settings.m_tunnel_properties.m_type.value() == "bytestream");
  assert(settings.m_tunnel_properties.m_publish);
  assert(!settings.m_tunnel_properties.m_publish.value());
  assert(settings.m_tunnel_properties.m_protocol);
  assert(settings.m_tunnel_properties.m_protocol.value() == "http");
  assert(settings.m_tunnel_properties.m_labels.count("ignored") == 0);
  assert(settings.m_tunnel_properties.m_labels.at("env") == "staging");
  assert(settings.m_tunnel_properties.m_labels.at("tier") == "edge");
  assert(settings.m_tunnel_properties.m_geoip.size() == 2);
  assert(settings.m_tunnel_properties.m_trusted_ips.size() == 2);
  assert(settings.m_tunnel_properties.m_hostname);
  assert(settings.m_tunnel_properties.m_hostname.value() == "api.example");
  assert(settings.m_tunnel_properties.m_host);
  assert(settings.m_tunnel_properties.m_host.value() == "legacy.example");
  assert(settings.m_tunnel_properties.m_tls_mode);
  assert(settings.m_tunnel_properties.m_tls_mode.value() == "terminated");
  assert(settings.m_tunnel_properties.m_tls_alpns.size() == 2);
  assert(settings.m_tunnel_properties.m_tls_min_version);
  assert(settings.m_tunnel_properties.m_tls_min_version.value() == "tls1.2");
  assert(settings.m_tunnel_properties.m_tls_ciphers.size() == 2);
  assert(settings.m_tunnel_properties.m_mtls_auth);
  assert(settings.m_tunnel_properties.m_mtls_auth.value());
  assert(settings.m_tunnel_properties.m_http_version);
  assert(settings.m_tunnel_properties.m_http_version.value() == "h2");
  assert(settings.m_tunnel_properties.m_http_use_tls);
  assert(!settings.m_tunnel_properties.m_http_use_tls.value());
  assert(settings.m_tunnel_properties.m_token_auth);
  assert(settings.m_tunnel_properties.m_token_auth.value());
  assert(settings.m_tunnel_properties.m_rstream_auth);
  assert(settings.m_tunnel_properties.m_rstream_auth.value());
  assert(settings.m_tunnel_properties.m_challenge_mode);
  assert(settings.m_tunnel_properties.m_challenge_mode.value());
  assert(settings.m_tunnel_properties.m_upstream_tls);
  assert(settings.m_tunnel_properties.m_upstream_tls.value());
}

static boost::filesystem::path write_config_file(const std::string& content)
{
  auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("rstream-cpp-test-%%%%-%%%%.yaml");
  std::ofstream file(path.string());
  file << content;
  file.close();
  return path;
}

static void check_token_resolution_precedence()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  token.unset();
  context.unset();
  api_url.unset();
  auto path = write_config_file(
      "defaults:\n"
      "  context:\n"
      "    name: prod\n"
      "environments:\n"
      "  - apiUrl: https://rstream.io\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: env-token\n"
      "      mtls:\n"
      "        certificateFile: /etc/rstream/env-client.pem\n"
      "        keyFile: /etc/rstream/env-client-key.pem\n"
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://rstream.io\n"
      "    engine: engine.example:443\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: context-token\n"
      "      mtls:\n"
      "        certificate: |\n"
      "          -----BEGIN CERTIFICATE-----\n"
      "          context-cert\n"
      "          -----END CERTIFICATE-----\n"
      "        key: |\n"
      "          -----BEGIN PRIVATE KEY-----\n"
      "          context-key\n"
      "          -----END PRIVATE KEY-----\n");
  config_path.set(path.string());
  rstream::io_rstrm::config cfg;
  auto token_from_config = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_from_config);
  assert(token_from_config.value());
  assert(token_from_config.value().get() == "context-token");
  token.set(" env-token ");
  auto token_from_env = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_from_env);
  assert(token_from_env.value().get() == "env-token");
  cfg.m_token = "explicit-token";
  auto token_from_explicit_config = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_from_explicit_config);
  assert(token_from_explicit_config.value().get() == "explicit-token");
  cfg.m_no_token = true;
  auto skipped_token = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(skipped_token);
  assert(!skipped_token.value());
  boost::filesystem::remove(path);
}

static void check_token_resolution_rejects_non_inline_storage()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  token.unset();
  context.set("prod");
  api_url.unset();
  auto path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: file\n"
      "          value: /tmp/token\n");
  config_path.set(path.string());
  rstream::io_rstrm::config cfg;
  auto token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(!token_result);
  assert(token_result.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(path);
}

static void check_client_details_rejects_token_and_mtls_auth_conflict()
{
  rstream::io_rstrm::config cfg;
  cfg.m_token = "token";
  auto details = rstream::io_rstrm::get_client_details(
      cfg,
      rstream::io::make_address("tcp://engine.example:443?ssl&ssl.cert_file=client.pem&ssl.key_file=client-key.pem"));
  assert(!details);
  assert(details.error().value() == static_cast<int>(rstream::io_rstrm::error::code::authentication_conflict));
  assert(details.error().message() == "token and mTLS authentication cannot be used together");

  cfg.m_no_token = true;
  auto mtls_only = rstream::io_rstrm::get_client_details(
      cfg,
      rstream::io::make_address("tcp://engine.example:443?ssl&ssl.cert_file=client.pem&ssl.key_file=client-key.pem"));
  assert(mtls_only);
  assert(!mtls_only.value().m_token);

  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  token.set("env-token");
  cfg.m_no_token = false;
  cfg.m_token    = boost::none;
  auto env_conflict = rstream::io_rstrm::get_client_details(
      cfg,
      rstream::io::make_address("tcp://engine.example:443?ssl&ssl.cert_file=client.pem&ssl.key_file=client-key.pem"));
  assert(!env_conflict);
  assert(env_conflict.error().value() == static_cast<int>(rstream::io_rstrm::error::code::authentication_conflict));
  assert(env_conflict.error().message() == "token and mTLS authentication cannot be used together");
  token.unset();

  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  context.set("prod");
  auto path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    engine: engine.example:443\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: stored-token\n");
  config_path.set(path.string());
  auto stored_token_suppressed = rstream::io_rstrm::get_client_details(
      cfg,
      rstream::io::make_address("tcp://engine.example:443?ssl&ssl.cert_file=client.pem&ssl.key_file=client-key.pem"));
  assert(stored_token_suppressed);
  assert(!stored_token_suppressed.value().m_token);
  boost::filesystem::remove(path);
}

static void check_token_resolution_uses_environment_auth_when_context_has_no_token()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  token.unset();
  context.set("prod");
  api_url.unset();
  auto path = write_config_file(
      "environments:\n"
      "  - apiUrl: https://rstream.io\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: env-fallback-token\n"
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://rstream.io\n");
  config_path.set(path.string());
  rstream::io_rstrm::config cfg;
  auto token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_result);
  assert(token_result.value());
  assert(token_result.value().get() == "env-fallback-token");
  boost::filesystem::remove(path);
}

static void check_config_file_json_shape_and_invalid_yaml()
{
  auto path = write_config_file(
      "defaults:\n"
      "  context:\n"
      "    name: prod\n"
      "environments:\n"
      "  - apiUrl: https://rstream.io\n"
      "contexts:\n"
      "  - name: prod\n"
      "    engine: engine.example:443\n");
  auto json_result = rstream::io_rstrm::get_rstream_config_file(path.string());
  assert(json_result);
  assert(json_result.value()["defaults"]["context"]["name"] == "prod");
  assert(json_result.value()["contexts"][0]["engine"] == "engine.example:443");
  boost::filesystem::remove(path);

  auto invalid_path = write_config_file("contexts: [");
  auto invalid_json = rstream::io_rstrm::get_rstream_config_file(invalid_path.string());
  assert(!invalid_json);
  assert(invalid_json.error().value() == static_cast<int>(boost::system::errc::io_error));
  boost::filesystem::remove(invalid_path);
}

static void check_config_file_json_includes_auth_storage_and_env_path()
{
  env_guard config_path("RSTREAM_CONFIG");
  auto path = write_config_file(
      "defaults:\n"
      "  context:\n"
      "    name: prod\n"
      "environments:\n"
      "  - apiUrl: https://rstream.io\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: env-token\n"
      "      mtls:\n"
      "        certificateFile: /etc/rstream/env-client.pem\n"
      "        keyFile: /etc/rstream/env-client-key.pem\n"
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://rstream.io\n"
      "    engine: engine.example:443\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: context-token\n"
      "      mtls:\n"
      "        certificate: |\n"
      "          -----BEGIN CERTIFICATE-----\n"
      "          context-cert\n"
      "          -----END CERTIFICATE-----\n"
      "        key: |\n"
      "          -----BEGIN PRIVATE KEY-----\n"
      "          context-key\n"
      "          -----END PRIVATE KEY-----\n");
  config_path.set(path.string());

  auto config_dir = rstream::io_rstrm::get_rstream_config_path();
  assert(config_dir);
  assert(config_dir.value() == path.parent_path());
  auto config_file_path = rstream::io_rstrm::get_rstream_config_file_path();
  assert(config_file_path);
  assert(config_file_path.value() == path);

  auto json_result = rstream::io_rstrm::get_rstream_config_file();
  assert(json_result);
  const auto& json = json_result.value();
  assert(json["environments"][0]["auth"]["token"]["storage"]["kind"] == "inline");
  assert(json["environments"][0]["auth"]["token"]["storage"]["value"] == "env-token");
  assert(json["environments"][0]["auth"]["mtls"]["certificateFile"] == "/etc/rstream/env-client.pem");
  assert(json["environments"][0]["auth"]["mtls"]["keyFile"] == "/etc/rstream/env-client-key.pem");
  assert(json["contexts"][0]["auth"]["token"]["storage"]["kind"] == "inline");
  assert(json["contexts"][0]["auth"]["token"]["storage"]["value"] == "context-token");
  assert(json["contexts"][0]["auth"]["mtls"]["certificate"].get<std::string>().find("context-cert") != std::string::npos);
  assert(json["contexts"][0]["auth"]["mtls"]["key"].get<std::string>().find("context-key") != std::string::npos);
  boost::filesystem::remove(path);
}

static void check_config_rejects_ambiguous_context()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  token.unset();
  context.set("prod");
  api_url.unset();
  auto path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://one.example\n"
      "  - name: prod\n"
      "    apiUrl: https://two.example\n");
  config_path.set(path.string());
  rstream::io_rstrm::config cfg;
  auto token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(!token_result);
  assert(token_result.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(path);
}

static void check_config_context_api_url_selection_edges()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  token.unset();
  context.set("prod");
  api_url.set("https://custom.example");

  auto fallback_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: unlinked-token\n"
      "environments:\n"
      "  - apiUrl: https://custom.example\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: env-token\n");
  config_path.set(fallback_path.string());
  rstream::io_rstrm::config cfg;
  auto token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_result);
  assert(token_result.value());
  assert(token_result.value().get() == "unlinked-token");
  boost::filesystem::remove(fallback_path);

  auto duplicate_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://custom.example\n"
      "  - name: prod\n"
      "    apiUrl: https://custom.example\n");
  config_path.set(duplicate_path.string());
  auto duplicate_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(!duplicate_result);
  assert(duplicate_result.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(duplicate_path);

  auto missing_path = write_config_file(
      "contexts:\n"
      "  - name: dev\n"
      "    apiUrl: https://custom.example\n");
  config_path.set(missing_path.string());
  auto missing_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(!missing_result);
  assert(missing_result.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(missing_path);
}

static void check_config_default_paths_empty_files_and_exact_context_match()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  env_guard home("HOME");
  token.unset();
  config_path.unset();
  context.unset();
  api_url.unset();

  auto home_dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("rstream-cpp-home-%%%%-%%%%");
  boost::filesystem::create_directories(home_dir);
  home.set(home_dir.string());
  auto home_result = rstream::io_rstrm::get_home_path();
  assert(home_result);
  assert(home_result.value() == home_dir);
  auto default_config_dir = rstream::io_rstrm::get_rstream_config_path();
  assert(default_config_dir);
  assert(default_config_dir.value() == home_dir / ".rstream");
  auto default_config_file = rstream::io_rstrm::get_rstream_config_file_path();
  assert(default_config_file);
  assert(default_config_file.value() == home_dir / ".rstream" / "config.yaml");
  auto missing_config_json = rstream::io_rstrm::get_rstream_config_file();
  assert(missing_config_json);
  assert(missing_config_json.value().empty());
  boost::filesystem::remove_all(home_dir);

  auto empty_path = write_config_file("  \n\t\n");
  auto empty_json = rstream::io_rstrm::get_rstream_config_file(empty_path.string());
  assert(empty_json);
  assert(empty_json.value().empty());
  boost::filesystem::remove(empty_path);

  auto sequence_path = write_config_file("- not-a-map\n");
  auto sequence_json = rstream::io_rstrm::get_rstream_config_file(sequence_path.string());
  assert(sequence_json);
  assert(sequence_json.value().empty());
  boost::filesystem::remove(sequence_path);

  context.set("prod");
  api_url.set("https://custom.example");
  auto exact_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://other.example\n"
      "  - name: prod\n"
      "    apiUrl: https://custom.example\n"
      "    auth:\n"
      "      token:\n"
      "        storage:\n"
      "          kind: inline\n"
      "          value: exact-token\n");
  config_path.set(exact_path.string());
  rstream::io_rstrm::config cfg;
  auto token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(token_result);
  assert(token_result.value());
  assert(token_result.value().get() == "exact-token");
  boost::filesystem::remove(exact_path);

  auto no_token_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://custom.example\n");
  config_path.set(no_token_path.string());
  auto no_token_result = rstream::io_rstrm::get_rstream_token(cfg, rstream::io::make_address("engine.example:443"));
  assert(no_token_result);
  assert(!no_token_result.value());
  boost::filesystem::remove(no_token_path);
}

static void check_config_ignores_non_map_yaml_entries_and_malformed_auth_nodes()
{
  auto path = write_config_file(
      "environments:\n"
      "  - not-a-map\n"
      "  - apiUrl: https://scalar-auth.example\n"
      "    auth: invalid\n"
      "  - apiUrl: https://scalar-token.example\n"
      "    auth:\n"
      "      token: invalid\n"
      "  - apiUrl: https://scalar-storage.example\n"
      "    auth:\n"
      "      token:\n"
      "        storage: invalid\n"
      "  - apiUrl: https://empty-storage.example\n"
      "    auth:\n"
      "      token:\n"
      "        storage: {}\n"
      "contexts:\n"
      "  - not-a-map\n"
      "  - name: scalar-auth\n"
      "    auth: invalid\n"
      "  - name: scalar-token\n"
      "    auth:\n"
      "      token: invalid\n"
      "  - name: scalar-storage\n"
      "    auth:\n"
      "      token:\n"
      "        storage: invalid\n"
      "  - name: empty-storage\n"
      "    auth:\n"
      "      token:\n"
      "        storage: {}\n");
  auto json_result = rstream::io_rstrm::get_rstream_config_file(path.string());
  assert(json_result);
  const auto& json = json_result.value();
  assert(json["environments"].size() == 4);
  assert(json["contexts"].size() == 4);
  assert(!json["environments"][0].contains("auth"));
  assert(!json["environments"][1].contains("auth"));
  assert(!json["environments"][2].contains("auth"));
  assert(json["environments"][3]["auth"]["token"]["storage"]["kind"] == "");
  assert(json["environments"][3]["auth"]["token"]["storage"]["value"] == "");
  assert(!json["contexts"][0].contains("auth"));
  assert(!json["contexts"][1].contains("auth"));
  assert(!json["contexts"][2].contains("auth"));
  assert(json["contexts"][3]["auth"]["token"]["storage"]["kind"] == "");
  assert(json["contexts"][3]["auth"]["token"]["storage"]["value"] == "");
  boost::filesystem::remove(path);
}

static void check_engine_resolution_from_env_and_config()
{
  env_guard engine_address("RSTREAM_ENGINE_ADDRESS");
  env_guard engine("RSTREAM_ENGINE");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  engine_address.set("tcp://direct.example:443?ssl");
  engine.set("ignored.example:443");
  auto direct = rstream::io_rstrm::get_rstream_engine_address();
  assert(direct);
  assert(direct.value() == "tcp://direct.example:443?ssl");
  engine_address.unset();
  engine.set("engine.example:443");
  auto from_engine_env = rstream::io_rstrm::get_rstream_engine_address();
  assert(from_engine_env);
  assert(from_engine_env.value() == "tcp://engine.example:443?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1" + default_engine_tls_groups_query());
  engine.unset();
  context.set("prod");
  api_url.unset();
  auto path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    engine: configured.example:443\n"
      "    auth:\n"
      "      mtls:\n"
      "        certificateFile: /etc/rstream/client.pem\n"
      "        keyFile: /etc/rstream/client-key.pem\n");
  config_path.set(path.string());
  auto from_config = rstream::io_rstrm::get_rstream_engine_address();
  assert(from_config);
  assert(from_config.value() == "tcp://configured.example:443?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1" + default_engine_tls_groups_query() + "&ssl.cert_file=%2Fetc%2Frstream%2Fclient.pem&ssl.key_file=%2Fetc%2Frstream%2Fclient-key.pem");
  boost::filesystem::remove(path);

  engine.set("tcp://raw.example:443?ssl");
  auto raw_engine = rstream::io_rstrm::get_rstream_engine_address();
  assert(raw_engine);
  assert(raw_engine.value() == "tcp://raw.example:443?ssl");
  engine.unset();

  auto missing_engine_path = write_config_file(
      "defaults:\n"
      "  context:\n"
      "    name: prod\n"
      "contexts:\n"
      "  - name: prod\n"
      "    apiUrl: https://rstream.io\n");
  config_path.set(missing_engine_path.string());
  auto missing_engine = rstream::io_rstrm::get_rstream_engine_address();
  assert(!missing_engine);
  assert(missing_engine.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(missing_engine_path);

  auto unknown_context_path = write_config_file(
      "contexts:\n"
      "  - name: dev\n"
      "    engine: dev.example:443\n");
  config_path.set(unknown_context_path.string());
  context.set("prod");
  auto unknown_context_engine = rstream::io_rstrm::get_rstream_engine_address();
  assert(!unknown_context_engine);
  assert(unknown_context_engine.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(unknown_context_path);
}

static void check_engine_resolution_rejects_invalid_mtls_auth_config()
{
  env_guard engine_address("RSTREAM_ENGINE_ADDRESS");
  env_guard engine("RSTREAM_ENGINE");
  env_guard cert_file("RSTREAM_MTLS_CERT_FILE");
  env_guard key_file("RSTREAM_MTLS_KEY_FILE");
  env_guard config_path("RSTREAM_CONFIG");
  env_guard context("RSTREAM_CONTEXT");
  env_guard api_url("RSTREAM_API_URL");
  engine_address.unset();
  engine.unset();
  cert_file.unset();
  key_file.unset();
  api_url.unset();
  context.set("prod");

  auto missing_key_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    engine: configured.example:443\n"
      "    auth:\n"
      "      mtls:\n"
      "        certificateFile: /etc/rstream/client.pem\n");
  config_path.set(missing_key_path.string());
  auto missing_key = rstream::io_rstrm::get_rstream_engine_address();
  assert(!missing_key);
  assert(missing_key.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(missing_key_path);

  auto mixed_source_path = write_config_file(
      "contexts:\n"
      "  - name: prod\n"
      "    engine: configured.example:443\n"
      "    auth:\n"
      "      mtls:\n"
      "        certificate: inline-cert\n"
      "        keyFile: /etc/rstream/client-key.pem\n");
  config_path.set(mixed_source_path.string());
  auto mixed_source = rstream::io_rstrm::get_rstream_engine_address();
  assert(!mixed_source);
  assert(mixed_source.error().value() == static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration));
  boost::filesystem::remove(mixed_source_path);

  engine.set("engine.example:443");
  cert_file.set("/etc/rstream/env-client.pem");
  key_file.set("/etc/rstream/env-client-key.pem");
  auto from_env = rstream::io_rstrm::get_rstream_engine_address();
  assert(from_env);
  assert(from_env.value() == "tcp://engine.example:443?ssl&ssl.tlsv13&ssl.alpn_protos=rstrm%2F1" + default_engine_tls_groups_query() + "&ssl.cert_file=%2Fetc%2Frstream%2Fenv-client.pem&ssl.key_file=%2Fetc%2Frstream%2Fenv-client-key.pem");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_parse_settings_rejects_token_conflict();
  check_parse_settings_socket_token_rules();
  check_parse_settings_rejects_invalid_boolean();
  check_parse_settings_acceptor_policy_controls();
  check_token_resolution_precedence();
  check_token_resolution_rejects_non_inline_storage();
  check_client_details_rejects_token_and_mtls_auth_conflict();
  check_token_resolution_uses_environment_auth_when_context_has_no_token();
  check_config_file_json_shape_and_invalid_yaml();
  check_config_file_json_includes_auth_storage_and_env_path();
  check_config_rejects_ambiguous_context();
  check_config_context_api_url_selection_edges();
  check_config_default_paths_empty_files_and_exact_context_match();
  check_config_ignores_non_map_yaml_entries_and_malformed_auth_nodes();
  check_engine_resolution_from_env_and_config();
  check_engine_resolution_rejects_invalid_mtls_auth_config();
  return 0;
}
