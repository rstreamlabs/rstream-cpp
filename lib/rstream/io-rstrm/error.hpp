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
  // Local/client-side errors
  invalid_configuration = 1,
  invalid_endpoint      = 2,
  invalid_state         = 3,
  no_valid_endpoint     = 4,
  operation_aborted     = 5,
  operation_in_progress = 6,
  operation_timeout     = 7,
  protocol_error        = 8,
  server_error          = 9,
  stream_not_found      = 10,

  // Server-mapped error codes
  unauthorized                  = 1000,
  invalid_request               = 2000,
  protocol_version_missing      = 2010,
  protocol_version_invalid      = 2020,
  protocol_version_incompatible = 2030,
  tunnel_not_found              = 3000,
  invalid_stream                = 4000,
  feature_not_available         = 5000,
  service_unavailable           = 6000,
  internal                      = 9000,
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
