// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/error_code.hpp>

namespace rstream {
namespace rtty {

namespace error {

class category : public std::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  client_error,
  invalid_state,
  not_a_tty,
  operation_aborted,
  operation_timeout,
  protocol_error,
  server_error,
  unexpected_message,
  unknown_undefined_error,
};

extern inline const category& rstream_rtty_error_category()
{
  static class category category;
  return category;
}

inline std::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_rtty_error_category()};
}

std::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace rtty
}  // namespace rstream

namespace std {

template <>
struct is_error_code_enum<rstream::rtty::error::code> : std::true_type {};

}  // namespace std
