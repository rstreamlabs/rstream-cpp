// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/system/system_error.hpp>

namespace error {

class category : public std::error_category {
 public:
  virtual const char* name() const noexcept override;
  virtual std::string message(int code) const override;
};

enum class code {
  success = 0,
  ncurses_terminal,
};

extern inline const category& error_category()
{
  static class category category;
  return category;
}

inline std::error_code make_error_code(code code)
{
  return {static_cast<int>(code), error_category()};
}

std::error_code make_error_code(int code);

}  // namespace error

std::string to_string(error::code code);

namespace std {

template <>
struct is_error_code_enum<error::code> : std::true_type {};

}  // namespace std
