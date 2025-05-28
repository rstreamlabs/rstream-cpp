// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <boost/asio/ip/address.hpp>

namespace rstream {
namespace io {

class geoip {
 public:
  using ptr = std::shared_ptr<geoip>;
  struct config {
    std::string m_database_location;
  };
  struct result {
    std::string m_country_iso_code;
  };
  geoip(const config& config);
  result lookup(const boost::asio::ip::address& address);

 private:
  class impl;
  std::shared_ptr<impl> m_impl;
};

}  // namespace io
}  // namespace rstream
