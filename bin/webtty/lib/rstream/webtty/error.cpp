// See LICENSE file in the project root for license information.

#include "error.hpp"

namespace rstream {
namespace webtty {

namespace error {

const char* category::name() const noexcept
{
  return "rstream::webtty::error::category";
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
    case error::code::client_error:
      return "client error";
    case error::code::invalid_state:
      return "invalid state";
    case error::code::crypto_error:
      return "crypto error";
    case error::code::not_a_tty:
      return "terminal is not a TTY";
    case error::code::operation_aborted:
      return "operation aborted";
    case error::code::operation_timeout:
      return "operation timeout";
    case error::code::protocol_error:
      return "protocol error";
    case error::code::server_error:
      return "server error";
    case error::code::e2e_session_key_grant_required:
      return "E2E session key grant is required";
    case error::code::known_server_required:
      return "known WebTTY server endpoint identity is required";
    case error::code::server_endpoint_identity_mismatch:
      return "WebTTY server endpoint identity does not match the configured known server";
    case error::code::server_proof_invalid:
      return "WebTTY server proof is invalid";
    case error::code::client_identity_required:
      return "WebTTY client identity is required";
    case error::code::client_proof_required:
      return "WebTTY client proof is required";
    case error::code::client_proof_invalid:
      return "WebTTY client proof is invalid";
    case error::code::client_unauthorized:
      return "WebTTY client signing key is not authorized";
    case error::code::managed_attach_unsupported:
      return "managed WebTTY attach is handled by the rstream engine; direct WebTTY servers accept only new Open sessions";
    case error::code::unsupported_execution_mode:
      return "unsupported execution mode";
    case error::code::login_user_required:
      return "login execution mode requires a default user or explicitly allowed client user";
    case error::code::client_user_disabled:
      return "client-selected OS users are disabled in login execution mode";
    case error::code::unexpected_message:
      return "unexpected message";
    case error::code::unknown_undefined_error:
      return "error is unknown / undefined";
    default:
      return "unknown error";
  }
}

}  // namespace webtty
}  // namespace rstream
