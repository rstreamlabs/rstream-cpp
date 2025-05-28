// See LICENSE file in the project root for license information.

#pragma once

#include <map>
#include <ostream>
#include <string>

#include <boost/optional.hpp>
#include <boost/system/result.hpp>
#include <boost/url.hpp>

#include "ssl.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class endpoint_base {
 public:
  using protocol_type = boost::system::result<std::string>;
  using url_type      = boost::urls::url;

  virtual ~endpoint_base() = default;

  virtual protocol_type protocol() const = 0;

  virtual std::string to_string() const = 0;

  virtual void set_url(const url_type& url) = 0;

  virtual const url_type& get_url() const = 0;
};

std::ostream& operator<<(std::ostream& ostream, const endpoint_base& endpoint);

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
