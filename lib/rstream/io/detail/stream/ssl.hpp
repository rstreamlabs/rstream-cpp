// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url.hpp>

namespace rstream {
namespace io {

#ifdef EMBED_DEFAULT_CA_CERTIFICATES
const std::string& get_default_ca_certificates();
#endif

namespace detail {
namespace stream {

class stream_socket;

namespace ssl {

struct config {
  bool m_tlsv12;
  bool m_tlsv13;
  bool m_peer_verification;
  bool m_request_peer_cert;  // for acceptor only
  bool m_client_rpk;
  bool m_server_rpk;
  boost::optional<std::string> m_key;
  boost::optional<std::string> m_key_file;
  boost::optional<std::string> m_key_type;
  boost::optional<std::string> m_passphrase;
  boost::optional<std::string> m_cert;
  boost::optional<std::string> m_cert_file;
  boost::optional<std::string> m_cert_type;
  boost::optional<std::string> m_tmp_dh_params;
  boost::optional<std::string> m_tmp_dh_params_file;
  boost::optional<std::string> m_cacert;
  boost::optional<std::string> m_cacert_file;
  boost::optional<std::string> m_capath;
  boost::optional<std::string> m_ciphers;
  boost::optional<std::string> m_ciphers_tlsv13;
  boost::optional<std::string> m_groups;
  boost::optional<std::string> m_sni;
  boost::optional<std::string> m_alpn_protos;
  boost::optional<std::string> m_engine;
  boost::optional<std::string> m_pkcs11_module;
  boost::optional<std::string> m_pkcs11_pin_env;
  boost::optional<std::string> m_pkcs11_provider;
  boost::optional<unsigned long> m_max_ongoing_upstream_ops;   // for acceptor only, 0 means no limit
  boost::optional<unsigned long> m_async_shutdown_timeout_ms;  // 0 means no limit
};

}  // namespace ssl

boost::optional<ssl::config> parse_ssl_config(const boost::urls::url& url, boost::system::error_code& error_code);

}  // namespace stream
}  // namespace detail

}  // namespace io
}  // namespace rstream
