// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace tunnel {

namespace error {

class category : public std::error_category {
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
};

const category& rstream_tunnel_error_category() noexcept;

inline std::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_tunnel_error_category()};
}

std::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace tunnel
}  // namespace rstream

namespace std {

template <>
struct is_error_code_enum<rstream::tunnel::error::code> : std::true_type {};

}  // namespace std
