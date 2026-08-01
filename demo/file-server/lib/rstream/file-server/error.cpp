// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace file_server {

namespace error {

const category& rstream_file_server_error_category() noexcept
{
  static const category value;
  return value;
}

const char* category::name() const noexcept
{
  return "rstream::file-server::error::category";
}

std::string category::message(int code) const
{
  return to_string(static_cast<enum code>(code));
}

boost::system::error_code make_error_code(int code)
{
  return make_error_code((error::code)code);
}

}  // namespace error

std::string to_string(error::code code)
{
  switch (code) {
    case error::code::success:
      return "success";
    case error::code::invalid_state:
      return "invalid state";
    case error::code::no_valid_endpoint:
      return "no valid endpoint";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_timeout:
      return "operation timeout";
    default:
      return "unknown error";
  }
}

}  // namespace file_server
}  // namespace rstream
