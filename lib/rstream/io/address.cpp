// See LICENSE file in the project root for license information.

#include "address.hpp"

namespace rstream {
namespace io {

static std::string normalize_str(const std::string& str);

address::address(const boost::urls::url& url)
    : m_url(url)
{
}

address::address(const std::string& str)
    : m_url(normalize_str(str))
{
  m_str = str;
}

std::string address::host() const
{
  return m_url.host();
}

std::string address::port() const
{
  return m_url.port();
}

std::ostream& operator<<(std::ostream& ostream, const address& address)
{
  ostream << address.m_url;
  return ostream;
}

address make_address(const std::string& str)
{
  return address(str);
}

std::string normalize_str(const std::string& str)
{
  // Check if the str already has a scheme
  if (str.find("://") != std::string::npos) {
    return str;  // str already contains a scheme, return it unmodified
  }
  std::string res = str;
  // If the str is just a number (port), prepend `localhost:`
  if (std::all_of(str.begin(), str.end(), ::isdigit)) {
    res = "localhost:" + str;
  }
  // Wrap with `tcp://` to make it parseable as a URL
  res = "tcp://" + res;
  return res;
}

}  // namespace io
}  // namespace rstream
