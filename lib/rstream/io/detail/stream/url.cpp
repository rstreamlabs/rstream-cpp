// See LICENSE file in the project root for license information.

#include "url.hpp"

#include <rstream/core/log.hpp>

#include "error.hpp"

static const rstream::core::logger g_logger({"rstream", "io", "stream", "uri"});

namespace rstream {
namespace io {
namespace detail {
namespace stream {

void parse_url_param_value(bool& dst, const boost::urls::param& src, boost::system::error_code& error_code)
{
  if (src.has_value) {
    if (src.value == "true") {
      dst = true;
    }
    else if (src.value == "false") {
      dst = false;
    }
    else {
#ifdef DEBUG_BUILD
      g_logger->trace("invalid value '{}' for boolean parameter '{}'", src.value, src.key);
#endif
      error_code = error::code::invalid_argument;
    }
  }
  else {
    dst = true;
  }
}

void parse_url_param_value(boost::optional<std::string>& dst, const boost::urls::param& src, boost::system::error_code& error_code)
{
  if (src.has_value) {
    dst = src.value;
  }
  else {
#ifdef DEBUG_BUILD
    g_logger->trace("expecting a string value for parameter '{}'", src.key);
#endif
    error_code = error::code::invalid_argument;
  }
}

void parse_url_param_value(boost::optional<unsigned long>& dst, const boost::urls::param& src, boost::system::error_code& error_code)
{
  if (src.has_value) {
    try {
      dst = std::stoul(src.value);
    }
    catch (...) {
#ifdef DEBUG_BUILD
      g_logger->trace("invalid value '{}' for unsigned integer parameter '{}'", src.value, src.key);
#endif
      error_code = error::code::invalid_argument;
    }
  }
  else {
#ifdef DEBUG_BUILD
    g_logger->trace("expecting an unsigned integer value for parameter '{}'", src.key);
#endif
    error_code = error::code::invalid_argument;
  }
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
