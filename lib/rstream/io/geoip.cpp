// See LICENSE file in the project root for license information.

#include "geoip.hpp"

#include <boost/asio/ip/tcp.hpp>

#include <maxminddb.h>

#include <rstream/config.hpp>

namespace rstream {
namespace io {

class RSTREAM_GNUC_INTERNAL geoip::impl {
 public:
  impl(const config& config);
  virtual ~impl();
  result lookup(const boost::asio::ip::address& address);

 private:
  MMDB_s m_mmdb;
};

geoip::geoip(const config& config)
{
  m_impl = std::make_shared<impl>(config);
}

geoip::result geoip::lookup(const boost::asio::ip::address& address)
{
  return m_impl->lookup(address);
}

geoip::impl::impl(const config& config)
{
  int mmdb_error = MMDB_open(config.m_database_location.c_str(), 0, &m_mmdb);
  if (mmdb_error != MMDB_SUCCESS) {
    throw std::runtime_error(MMDB_strerror(mmdb_error));
  }
}

geoip::impl::~impl()
{
  MMDB_close(&m_mmdb);
}

geoip::result geoip::impl::lookup(const boost::asio::ip::address& address)
{
  int mmdb_error;
  geoip::result result = {.m_country_iso_code = {}};
  boost::asio::ip::tcp::endpoint endpoint(address, 0);
  MMDB_lookup_result_s mmdb_result = MMDB_lookup_sockaddr(&m_mmdb, endpoint.data(), &mmdb_error);
  auto error                       = false;
  if (mmdb_error == MMDB_SUCCESS) {
    if (!mmdb_result.found_entry) {
      error = true;
    }
    else {
      MMDB_entry_data_s data;
      mmdb_error = MMDB_get_value(&mmdb_result.entry, &data, "country", "iso_code", NULL);
      if (mmdb_error != MMDB_SUCCESS || !data.has_data || data.type != MMDB_DATA_TYPE_UTF8_STRING) {
        error = true;
      }
      else {
        result.m_country_iso_code = std::string(data.utf8_string, data.data_size);
      }
    }
  }
  if (error) {
    throw std::runtime_error(mmdb_error != MMDB_SUCCESS ? MMDB_strerror(mmdb_error) : "unknown error");
  }
  return result;
}

}  // namespace io
}  // namespace rstream
