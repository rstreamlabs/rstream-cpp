// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace io {
namespace detail {
namespace stream {

namespace error {

class category : public boost::system::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  generic_error,
  invalid_argument,
  operation_aborted,
  operation_in_progress,
  ssl_configuration_error,
  uninitialized_object,
};

extern inline const category& rstream_io_detail_stream_error_category()
{
  static class category category;
  return category;
}

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_io_detail_stream_error_category()};
}

}  // namespace error

std::string to_string(error::code code);

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::io::detail::stream::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
