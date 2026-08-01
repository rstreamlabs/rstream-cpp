// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace io {

namespace error {

class category : public boost::system::error_category {
 public:
  category() noexcept;

  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  deserialization_error,
  invalid_buffer_size,
  operation_cancelled,
  operation_timeout,
  unknown_undefined_error,
  invalid_uri,
  unsupported_operation
};

const category& rstream_io_error_category() noexcept;

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_io_error_category()};
}

}  // namespace error

std::string to_string(error::code code);

}  // namespace io
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::io::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
