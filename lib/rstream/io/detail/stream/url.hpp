// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url.hpp>

namespace rstream {
namespace io {
namespace detail {
namespace stream {

void parse_url_param_value(bool& dst, const boost::urls::param& src, boost::system::error_code& error_code);

void parse_url_param_value(boost::optional<std::string>& dst, const boost::urls::param& src, boost::system::error_code& error_code);

void parse_url_param_value(boost::optional<unsigned long>& dst, const boost::urls::param& src, boost::system::error_code& error_code);

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
