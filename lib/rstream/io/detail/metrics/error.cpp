// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace metrics {

namespace error {

category::category() noexcept
    : boost::system::error_category(0x727374726d747263ULL)
{
}

const category& rstream_io_detail_metrics_error_category() noexcept
{
  static const category value;
  return value;
}

const char* category::name() const noexcept
{
  return "rstream::io::detail::metrics::error::category";
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
    case error::code::invalid_state:
      return "invalid state";
    case error::code::no_valid_endpoint:
      return "no valid endpoint";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_timeout:
      return "operation timeout";
    case error::code::no_data_available:
      return "no data available";
    default:
      return "unknown error";
  }
}

}  // namespace metrics
}  // namespace detail
}  // namespace io
}  // namespace rstream
