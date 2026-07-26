// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace core {

namespace error {

class category : public boost::system::error_category {
 public:
  category() noexcept;

  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success                      = 0,
  invalid_size                 = 1,
  object_null                  = 2,
  object_not_writable          = 3,
  plugin_not_found             = 4,
  plugin_already_registered    = 5,
  plugin_initialization_failed = 6
};

const category& rstream_core_error_category() noexcept;

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_core_error_category()};
}

}  // namespace error

std::string to_string(error::code code);

}  // namespace core
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::core::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
