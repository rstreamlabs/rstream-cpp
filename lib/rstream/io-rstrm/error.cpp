// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace io_rstrm {

namespace error {

const char* category::name() const noexcept
{
  return "rstream::io-rstrm::error::category";
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
    case error::code::invalid_endpoint:
      return "invalid endpoint";
    case error::code::no_valid_endpoint:
      return "no valid endpoint";
    case error::code::invalid_configuration:
      return "invalid configuration";
    case error::code::invalid_state:
      return "invalid state";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_in_progress:
      return "another operation is in progress";
    case error::code::operation_timeout:
      return "operation timeout";
    case error::code::protocol_error:
      return "protocol error";
    case error::code::server_error:
      return "server error";
    case error::code::stream_not_found:
      return "stream not found";
    case error::code::authentication_conflict:
      return "token and mTLS authentication cannot be used together";
    case error::code::tunnel_not_found:
      return "tunnel not found";
    case error::code::unauthorized:
      return "unauthorized";
    case error::code::invalid_request:
      return "invalid request";
    case error::code::protocol_version_missing:
      return "protocol version missing";
    case error::code::protocol_version_invalid:
      return "protocol version invalid";
    case error::code::protocol_version_incompatible:
      return "protocol version incompatible";
    case error::code::invalid_stream:
      return "invalid stream";
    case error::code::feature_not_available:
      return "feature not available";
    case error::code::service_unavailable:
      return "service unavailable";
    case error::code::capacity_exhausted:
      return "capacity exhausted";
    case error::code::internal:
      return "internal error";
    default:
      return "unknown error";
  }
}

}  // namespace io_rstrm
}  // namespace rstream
