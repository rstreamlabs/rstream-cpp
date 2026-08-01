// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace file_server {

namespace error {

class category : public boost::system::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  invalid_state,
  no_valid_endpoint,
  operation_aborted,
  operation_timeout
};

const category& rstream_file_server_error_category() noexcept;

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_file_server_error_category()};
}

boost::system::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

}  // namespace file_server
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::file_server::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
