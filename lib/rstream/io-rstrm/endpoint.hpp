// See LICENSE file in the project root for license information.

#pragma once

#include <ostream>
#include <string>

#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>
#include <boost/system/result.hpp>
#include <boost/url.hpp>

#include <nlohmann/json.hpp>

#include <rstream/io/address.hpp>

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

struct endpoint {
  boost::optional<std::string> m_id_name;
  io::address m_server_address;
  boost::optional<std::string> m_secret;
  boost::optional<boost::asio::ip::address> m_source_ip;
};

std::ostream& operator<<(std::ostream& ostream, const endpoint& endpoint);

nlohmann::json& operator<<(nlohmann::json& json, const endpoint& endpoint);

boost::system::result<endpoint> make_endpoint(const boost::optional<std::string>& id_name, const boost::optional<std::string>& server_address);

boost::system::result<endpoint> make_endpoint(const boost::optional<std::string>& id_name, const boost::optional<std::string>& server_address, const boost::optional<std::string>& config_path);

boost::system::result<endpoint> make_endpoint(const boost::urls::url& url);

}  // namespace io_rstrm
}  // namespace rstream
