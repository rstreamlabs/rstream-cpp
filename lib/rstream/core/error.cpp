// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace core {

namespace error {

const char* category::name() const noexcept
{
  return "rstream::core::error::category";
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
    case error::code::invalid_size:
      return "invalid size";
    case error::code::object_null:
      return "object is null";
    case error::code::object_not_writable:
      return "object is not writable";
    case error::code::plugin_not_found:
      return "plugin not found";
    default:
      return "unknown error";
  }
}

}  // namespace core
}  // namespace rstream
