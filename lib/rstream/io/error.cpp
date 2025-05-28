// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace io {

namespace error {

const char* category::name() const noexcept
{
  return "rstream::io::error::category";
}

std::string category::message(int code) const
{
  return to_string(static_cast<enum code>(code));
}

}  // namespace error

std::string to_string(error::code code)
{
  switch (code) {
    case error::code::success:
      return "success";
    case error::code::deserialization_error:
      return "deserialization error";
    case error::code::invalid_buffer_size:
      return "invalid buffer size";
    case error::code::operation_cancelled:
      return "operation has been cancelled";
    case error::code::operation_timeout:
      return "operation timeout";
    case error::code::unknown_undefined_error:
      return "error is unknonw / undefined";
    case error::code::invalid_uri:
      return "invalid URI";
    case error::code::unsupported_operation:
      return "unsupported operation";
    default:
      return "unknown error";
  }
}

}  // namespace io
}  // namespace rstream
