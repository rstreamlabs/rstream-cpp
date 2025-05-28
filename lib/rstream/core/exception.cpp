// See LICENSE file in the project root for license information.

#include "exception.hpp"

#ifndef _WIN32
#include <cxxabi.h>
#endif

#include <memory>
#include <sstream>
#include <typeinfo>

#include "error.hpp"

namespace rstream {
namespace core {

system_error::system_error(const boost::system::error_code& error_code)
    : std::runtime_error(""),
      m_error_code(error_code)
{
}

system_error::system_error(const boost::system::error_code& error_code, const std::string& what)
    : std::runtime_error(what),
      m_error_code(error_code)
{
}

boost::system::error_code system_error::code() const noexcept
{
  return m_error_code;
}

const char* system_error::what() const noexcept
{
  if (m_what.empty()) {
    try {
      m_what += m_error_code.message();
      std::string tmp = std::runtime_error::what();
      if (!tmp.empty()) {
        m_what += " (" + tmp + ")";
      }
    }
    catch (...) {
      return std::runtime_error::what();
    }
  }
  return m_what.c_str();
}

namespace abi {

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
static std::string demangle(const std::type_info* ti)
{
  return (ti ? ti->name() : "unknown type");
}

static std::type_info* current_exception_type()
{
  return nullptr;
}
#else
static std::string demangle(const std::type_info* ti)
{
  std::unique_ptr<char, void (*)(void*)> own(::abi::__cxa_demangle(ti->name(), nullptr, nullptr, nullptr), std::free);
  return own != nullptr ? own.get() : ti->name();
}

static std::type_info* current_exception_type()
{
  return ::abi::__cxa_current_exception_type();
}
#endif

static std::string what(const throwable& error, const throwable& cause)
{
  std::stringstream result;
  result << error.get_message();
  if (cause) {
    result << "\ncaused by: " << cause.what();
  }
  return result.str();
}

}  // namespace abi

throwable::throwable(const std::exception_ptr exception)
    : m_ptr(exception)
{
  if (exception) {
    try {
      std::rethrow_exception(exception);
    }
    catch (const std::exception& exception) {
      m_info = {
          .m_name    = abi::demangle(abi::current_exception_type()),
          .m_message = exception.what(),
      };
    }
    catch (...) {
      m_info = {
          .m_name    = abi::demangle(abi::current_exception_type()),
          .m_message = "unknown error message: exception has invalid type",
      };
    }
    m_what = m_info.m_name + ": " + m_info.m_message;
  }
  else {
    m_what = "undefined exception";
  }
}

throwable::throwable(const boost::system::system_error& error)
    : throwable(std::make_exception_ptr(error))
{
}

throwable::throwable(const boost::system::error_code& error_code)
    : throwable(boost::system::system_error(error_code))
{
}

throwable::throwable(const system_error& error)
    : throwable(boost::system::system_error(error.code(), error.what()))
{
}

throwable::throwable(std::nullptr_t)
    : throwable(std::exception_ptr())
{
}

std::exception_ptr throwable::get() const noexcept { return m_ptr; }

throwable::operator bool() const noexcept { return m_ptr.operator bool(); }

std::string throwable::get_name() const
{
  if (operator bool()) {
    return m_info.m_name;
  }
  else {
    throw boost::system::system_error(error::code::object_null);
  }
}

std::string throwable::get_message() const
{
  if (operator bool()) {
    return m_info.m_message;
  }
  else {
    throw boost::system::system_error(error::code::object_null);
  }
}

const char* throwable::what() const noexcept { return m_what.c_str(); }

std::string throwable::name(const std::exception_ptr exception)
{
  return throwable(exception).get_name();
}

std::string throwable::message(const std::exception_ptr exception)
{
  return throwable(exception).get_message();
}

std::string throwable::to_string(const std::exception_ptr exception)
{
  return throwable(exception).what();
}

nested_error::nested_error(const throwable& error, const throwable& cause)
    : m_error(error),
      m_cause(cause),
      m_what(abi::what(error, cause))
{
}

nested_error::nested_error(const std::string& message, const throwable& cause)
    : nested_error(std::make_exception_ptr(std::runtime_error(message)), cause)
{
}

nested_error::nested_error(const std::string& message)
    : nested_error(message, nullptr)
{
}

throwable nested_error::get_error() const { return m_error; }

void nested_error::set_error(const throwable& error)
{
  if (!m_error) {
    m_error = error;
    m_what  = abi::what(m_error, m_cause);
  }
}

throwable nested_error::get_cause() const { return m_cause; }

void nested_error::set_cause(const throwable& cause)
{
  if (!m_cause) {
    m_cause = cause;
    m_what  = abi::what(m_error, m_cause);
  }
}

const char* nested_error::what() const noexcept { return m_what.c_str(); }

}  // namespace core
}  // namespace rstream
