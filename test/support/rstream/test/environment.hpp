// See LICENSE file in the project root for license information.

#pragma once

#include <cassert>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <rstream/core/system.hpp>

namespace rstream {
namespace test {

class environment_guard {
 public:
  explicit environment_guard(std::string key)
      : m_key(std::move(key)),
        m_value(core::get_environment_variable(m_key))
  {
  }

  ~environment_guard()
  {
    if (m_value) {
      set(*m_value);
    }
    else {
      unset();
    }
  }

  environment_guard(const environment_guard&)            = delete;
  environment_guard& operator=(const environment_guard&) = delete;

  void set(const std::string& value)
  {
#ifdef _WIN32
    assert(::_putenv_s(m_key.c_str(), value.c_str()) == 0);
#else
    assert(::setenv(m_key.c_str(), value.c_str(), 1) == 0);
#endif
  }

  void unset()
  {
#ifdef _WIN32
    assert(::_putenv_s(m_key.c_str(), "") == 0);
#else
    assert(::unsetenv(m_key.c_str()) == 0);
#endif
  }

 private:
  std::string m_key;
  std::optional<std::string> m_value;
};

}  // namespace test
}  // namespace rstream
