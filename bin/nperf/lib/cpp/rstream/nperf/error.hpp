// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace nperf {

namespace error {

class category : public boost::system::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  invalid_state,
  no_valid_endpoint,
  operation_aborted,
  operation_timeout,
  protocol_error,
  unexpected_close,
  invalid_argument
};

extern inline const category& rstream_nperf_error_category()
{
  static class category category;
  return category;
}

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_nperf_error_category()};
}

boost::system::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace nperf
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::nperf::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
