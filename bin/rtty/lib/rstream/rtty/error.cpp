// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace rtty {

namespace error {

const char* category::name() const noexcept
{
  return "rstream::rtty::error::category";
}

std::string category::message(int code) const
{
  return to_string(static_cast<enum code>(code));
}

std::error_code make_error_code(int code)
{
  return make_error_code((error::code)code);
}

}  // namespace error

std::string to_string(error::code code)
{
  switch (code) {
    case error::code::success:
      return "success";
    case error::code::unknown_undefined_error:
      return "error is unknonw / undefined";
    case error::code::invalid_state:
      return "invalid state";
    case error::code::not_a_tty:
      return "terminal is not a TTY";
    case error::code::unexpected_message:
      return "unexpected message";
    case error::code::protocol_error:
      return "protocol error";
    case error::code::server_error:
      return "server error";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_timeout:
      return "operation timeout";
    case error::code::no_valid_endpoint:
      return "no valid endpoint";
    case error::code::operation_not_permitted:
      return "operation not permitted";
    default:
      return "unknown error";
  }
}

}  // namespace rtty
}  // namespace rstream
