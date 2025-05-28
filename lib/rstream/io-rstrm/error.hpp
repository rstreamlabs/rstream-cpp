// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/error_code.hpp>

namespace rstream {
namespace io_rstrm {

namespace error {

class category : public std::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  invalid_configuration,
  invalid_endpoint,
  invalid_state,
  no_valid_endpoint,
  operation_aborted,
  operation_in_progress,
  operation_timeout,
  protocol_error,
  server_error,
  stream_not_found,
  tunnel_not_found,
  unauthorized,
};

extern inline const category& rstream_rstream_error_category()
{
  static class category category;
  return category;
}

inline std::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_rstream_error_category()};
}

std::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace io_rstrm
}  // namespace rstream

namespace std {

template <>
struct is_error_code_enum<::rstream::io_rstrm::error::code> : std::true_type {};

}  // namespace std
