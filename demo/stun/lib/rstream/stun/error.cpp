// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace stun {

namespace error {

const category& rstream_io_stun_error_category() noexcept
{
  static const category value;
  return value;
}

const char* category::name() const noexcept
{
  return "rstream::stun::error::category";
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
    case error::code::invalid_integrity:
      return "invalid integrity";
    case error::code::invalid_state:
      return "invalid state";
    case error::code::invalid_stun_attribute:
      return "invalid stun attribute";
    case error::code::no_valid_endpoint:
      return "no valid endpoint";
    case error::code::unknown_stun_attribute:
      return "unknown stun attribute";
    case error::code::unknown_stun_class:
      return "unknown stun class";
    case error::code::unknown_stun_method:
      return "unknown stun method";
    default:
      return "unknown error";
  }
}

}  // namespace stun
}  // namespace rstream
