// See LICENSE file in the project root for license information.

#include "endpoint.hpp"

#include <rstream/io/detail/stream/url.hpp>

#include "error.hpp"

namespace rstream {
namespace io_rstrm {

std::ostream& operator<<(std::ostream& ostream, const endpoint& endpoint)
{
  ostream << "protocol: rstrm, id_name: "
          << (endpoint.m_id_name ? endpoint.m_id_name.get() : "undefined")
          << "";
  if (endpoint.m_source_ip) {
    ostream << ", source_ip: " << endpoint.m_source_ip.get();
  }
  return ostream;
}

nlohmann::json& operator<<(nlohmann::json& json, const endpoint& endpoint)
{
  json["protocol"] = "rstrm";
  if (endpoint.m_id_name) {
    json["id_name"] = endpoint.m_id_name.get();
  }
  if (endpoint.m_source_ip) {
    json["source_ip"] = endpoint.m_source_ip.get().to_string();
  }
  return json;
}

boost::system::result<endpoint> make_endpoint(const boost::optional<std::string>& id_name, const boost::optional<std::string>& server_address)
{
  return make_endpoint(id_name, server_address, boost::none);
}

boost::system::result<endpoint> make_endpoint(const boost::optional<std::string>& id_name, const boost::optional<std::string>& server_address, const boost::optional<std::string>& config_path)
{
  if (server_address) {
    return endpoint{
        .m_id_name                       = id_name,
        .m_server_address                = io::make_address(server_address.get()),
        .m_server_address_from_uri_param = false,
        .m_secret                        = boost::none,
        .m_source_ip                     = boost::none,
    };
  }
  auto server_result = get_rstream_engine_address(config_path);
  if (server_result) {
    return endpoint{
        .m_id_name                       = id_name,
        .m_server_address                = io::make_address(server_result.value()),
        .m_server_address_from_uri_param = false,
        .m_secret                        = boost::none,
        .m_source_ip                     = boost::none,
    };
  }
  return server_result.error();
}

boost::system::result<endpoint> make_endpoint(const boost::urls::url& url)
{
  boost::system::error_code error_code;
  boost::optional<std::string> server;
  bool server_from_uri_param = false;
  {
    const auto params = rstream::io::detail::stream::url_params(url);
    auto it           = params.find("server");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(server, *it, error_code);
      server_from_uri_param = true;
    }
    if (!error_code && !server) {
      auto server_result = get_rstream_engine_address();
      if (server_result) {
        server = server_result.value();
      }
      else {
        error_code = server_result.error();
      }
    }
  }
  if (!error_code) {
    auto host_type = url.host_type();
    boost::optional<std::string> id_name;
    if (host_type == boost::urls::host_type::name) {
      const auto host = url.host();
      if (!host.empty()) {
        id_name = host;
      }
    }
    return endpoint{
        .m_id_name                       = id_name,
        .m_server_address                = io::make_address(server.get()),
        .m_server_address_from_uri_param = server_from_uri_param,
        .m_secret                        = boost::none,
        .m_source_ip                     = boost::none,
    };
  }
  else {
    return error_code;
  }
}

boost::system::result<io::address> make_redirected_server_address(const io::address& base, const std::string& server_address)
{
  try {
    const auto target = io::make_address(server_address);
    if (target.host().empty() || target.port().empty()) {
      return error::code::invalid_endpoint;
    }
    auto url = base.m_url;
    url.set_encoded_host(target.m_url.encoded_host());
    url.set_port(target.port());
    auto params    = url.params();
    const auto sni = params.find("ssl.sni");
    if (sni != params.end()) {
      params.erase(sni);
    }
    return io::address(url);
  }
  catch (...) {
    return error::code::invalid_endpoint;
  }
}

}  // namespace io_rstrm
}  // namespace rstream
