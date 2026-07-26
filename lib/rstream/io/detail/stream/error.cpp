// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

namespace error {

category::category() noexcept
    : boost::system::error_category(0x727374727374726dULL)
{
}

const category& rstream_io_detail_stream_error_category() noexcept
{
  static const category value;
  return value;
}

const char* category::name() const noexcept
{
  return "rstream::io::detail::stream::error::category";
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
    case error::code::generic_error:
      return "generic error";
    case error::code::invalid_argument:
      return "invalid argument";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_in_progress:
      return "another operation is in progress";
    case error::code::ssl_configuration_error:
      return "SSL configuration error";
    case error::code::uninitialized_object:
      return "uninitialized object";
    default:
      return "unknown error";
  }
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
