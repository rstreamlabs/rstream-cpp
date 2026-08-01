// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace core {

namespace error {

category::category() noexcept
    : boost::system::error_category(0x72737472636f7265ULL)
{
}

const category& rstream_core_error_category() noexcept
{
  static const category value;
  return value;
}

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
    case error::code::plugin_already_registered:
      return "plugin already registered";
    case error::code::plugin_initialization_failed:
      return "plugin initialization failed";
    default:
      return "unknown error";
  }
}

}  // namespace core
}  // namespace rstream
