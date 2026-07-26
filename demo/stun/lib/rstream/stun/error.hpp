// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace rstream {
namespace stun {

namespace error {

class category : public boost::system::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  invalid_integrity,
  invalid_state,
  invalid_stun_attribute,
  no_valid_endpoint,
  unknown_stun_attribute,
  unknown_stun_class,
  unknown_stun_method
};

const category& rstream_io_stun_error_category() noexcept;

inline boost::system::error_code make_error_code(code code)
{
  return {static_cast<int>(code), rstream_io_stun_error_category()};
}

}  // namespace error

std::string to_string(error::code code);

}  // namespace stun
}  // namespace rstream

namespace boost {
namespace system {

template <>
struct is_error_code_enum<rstream::stun::error::code> : std::true_type {};

}  // namespace system
}  // namespace boost
