// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace nperf {

namespace error {

const category& rstream_nperf_error_category() noexcept
{
  static const category value;
  return value;
}

const char* category::name() const noexcept
{
  return "rstream::nperf::error::category";
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
    case error::code::protocol_error:
      return "protocol error";
    case error::code::unexpected_close:
      return "connection unexpectedly closed";
    case error::code::invalid_argument:
      return "invalid argument";
    default:
      return "unknown error";
  }
}

}  // namespace nperf
}  // namespace rstream
