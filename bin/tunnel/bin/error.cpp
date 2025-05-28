// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace error {

const char* category::name() const noexcept
{
  return "error::category";
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
    case error::code::ncurses_terminal:
      return "failed to initialize ncurses terminal";
    default:
      return "unknown error";
  }
}
