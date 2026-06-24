// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/error_code.hpp>

namespace rstream {
namespace webtty {

namespace error {

class category : public std::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  client_error,
  invalid_state,
  crypto_error,
  not_a_tty,
  operation_aborted,
  operation_timeout,
  protocol_error,
  server_error,
  e2e_session_key_grant_required,
  known_server_required,
  server_endpoint_identity_mismatch,
  server_proof_invalid,
  client_identity_required,
  client_proof_required,
  client_proof_invalid,
  client_unauthorized,
  managed_attach_unsupported,
  unsupported_execution_mode,
  login_user_required,
  client_user_disabled,
  unexpected_message,
  unknown_undefined_error,
};

extern inline const category& rstream_webtty_error_category()
{
  static class category category;
  return category;
}

inline std::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_webtty_error_category()};
}

std::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace webtty
}  // namespace rstream

namespace std {

template <>
struct is_error_code_enum<rstream::webtty::error::code> : std::true_type {};

}  // namespace std
