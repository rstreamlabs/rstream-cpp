// See LICENSE file in the project root for license information.

#pragma once

#include <ostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/url.hpp>

namespace rstream {
namespace io {

struct address {
  address() = default;
  address(const boost::urls::url& url);
  address(const std::string& str);
  std::string host() const;
  std::string port() const;
  boost::urls::url m_url;
  boost::optional<std::string> m_str;
};

std::ostream& operator<<(std::ostream& ostream, const address& address);

address make_address(const std::string& str);

}  // namespace io
}  // namespace rstream
