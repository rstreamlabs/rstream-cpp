// See LICENSE file in the project root for license information.

#include "ssl.hpp"

#include "url.hpp"

#define PARSE_PARAMS_VIEW_BOOLEAN(src, dst, prefix, error_code, name) \
  {                                                                   \
    if (!error_code) {                                                \
      auto it = src.find(std::string(prefix) + #name);                \
      if (it != src.end()) {                                          \
        parse_url_param_value(dst.m_##name, *it, error_code);         \
      }                                                               \
    }                                                                 \
  }

#define PARSE_PARAMS_VIEW_STRING(src, dst, prefix, error_code, name) \
  {                                                                  \
    if (!error_code) {                                               \
      auto it = src.find(std::string(prefix) + #name);               \
      if (it != src.end()) {                                         \
        parse_url_param_value(dst.m_##name, *it, error_code);        \
      }                                                              \
    }                                                                \
  }

#define PARSE_PARAMS_VIEW_ULONG(src, dst, prefix, error_code, name) \
  {                                                                 \
    if (!error_code) {                                              \
      auto it = src.find(std::string(prefix) + #name);              \
      if (it != src.end()) {                                        \
        parse_url_param_value(dst.m_##name, *it, error_code);       \
      }                                                             \
    }                                                               \
  }

namespace rstream {
namespace io {

namespace detail {
namespace stream {

namespace ssl {

}  // namespace ssl

boost::optional<ssl::config> parse_ssl_config(const boost::urls::url& url, boost::system::error_code& error_code)
{
  const auto& params = url.params();
  boost::optional<ssl::config> res;
  if (!error_code) {
    bool ssl;
    {
      auto it = params.find("ssl");
      if (it != params.end()) {
        parse_url_param_value(ssl, *it, error_code);
      }
      else {
        ssl = false;
      }
    }
    if (!error_code && ssl) {
      ssl::config config         = {};
      config.m_tlsv12            = false;
      config.m_tlsv13            = false;
      config.m_peer_verification = true;
      config.m_client_rpk        = false;
      config.m_server_rpk        = false;
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, tlsv12)
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, tlsv13)
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, peer_verification)
      config.m_request_peer_cert = config.m_peer_verification;
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, request_peer_cert)
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, client_rpk)
      PARSE_PARAMS_VIEW_BOOLEAN(params, config, "ssl.", error_code, server_rpk)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, key)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, key_file)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, key_type)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, passphrase)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, cert)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, cert_file)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, cert_type)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, tmp_dh_params)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, tmp_dh_params_file)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, cacert)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, cacert_file)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, capath)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, ciphers)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, ciphers_tlsv13)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, sni)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, alpn_protos)
      PARSE_PARAMS_VIEW_STRING(params, config, "ssl.", error_code, engine)
      PARSE_PARAMS_VIEW_ULONG(params, config, "ssl.", error_code, max_ongoing_upstream_ops)
      PARSE_PARAMS_VIEW_ULONG(params, config, "ssl.", error_code, async_shutdown_timeout_ms)
      if (!error_code) {
        res = config;
      }
    }
  }
  return res;
}

}  // namespace stream
}  // namespace detail

}  // namespace io
}  // namespace rstream
