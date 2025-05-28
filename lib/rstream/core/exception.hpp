// See LICENSE file in the project root for license information.

#pragma once

#include <exception>
#include <string>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

namespace rstream {
namespace core {

class system_error : public std::runtime_error {
 public:
  system_error(const boost::system::error_code& error_code);
  system_error(const boost::system::error_code& error_code, const std::string& what);
  virtual ~system_error() = default;

  boost::system::error_code code() const noexcept;
  const char* what() const noexcept override;

 private:
  boost::system::error_code m_error_code;
  mutable std::string m_what;
};

class throwable : public std::exception {
 public:
  throwable(const std::exception_ptr error);
  throwable(const boost::system::system_error& error);
  throwable(const boost::system::error_code& error_code);
  throwable(const system_error& error);
  throwable(std::nullptr_t);
  virtual ~throwable() = default;

  operator bool() const noexcept;

  std::exception_ptr get() const noexcept;

  std::string get_name() const;
  std::string get_message() const;

  const char* what() const noexcept override;

  static std::string name(const std::exception_ptr exception);
  static std::string message(const std::exception_ptr exception);
  static std::string to_string(const std::exception_ptr exception);

 private:
  struct info {
    std::string m_name;
    std::string m_message;
  };

  std::exception_ptr m_ptr;
  info m_info;
  std::string m_what;
};

class nested_error : public std::exception {
 public:
  nested_error(const throwable& error, const throwable& cause);
  nested_error(const std::string& message, const throwable& cause);
  nested_error(const std::string& message);
  virtual ~nested_error() = default;

  void set_error(const throwable& error);
  throwable get_error() const;

  throwable get_cause() const;
  void set_cause(const throwable& cause);

  const char* what() const noexcept override;

 private:
  throwable m_error;
  throwable m_cause;
  std::string m_what;
};

}  // namespace core
}  // namespace rstream
