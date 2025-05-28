// See LICENSE file in the project root for license information.

#include "exception.hpp"

#include <boost/python.hpp>

namespace rstream {
namespace python {

error fetch_error()
{
  // Fetch the exception information. If there was no error ptype will be set
  // to null. The other two values might set to null anyway.
  PyObject* ptype      = nullptr;
  PyObject* pvalue     = nullptr;
  PyObject* ptraceback = nullptr;
  PyErr_Fetch(&ptype, &pvalue, &ptraceback);
  if (ptype == nullptr) {
    throw std::runtime_error("a Python error was detected but PyErr_Fetch() it returned null indicating that there was no error");
  }
  PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);
  if (ptraceback != nullptr) {
    PyException_SetTraceback(pvalue, ptraceback);
  }
  return std::make_tuple(ptype, pvalue, ptraceback);
}

std::string python_error_string(const error& error)
{
  // Get Boost handles to the Python objects so we get an easier API
  boost::python::handle<> htype(std::get<0>(error));
  boost::python::handle<> hvalue(boost::python::allow_null(std::get<1>(error)));
  boost::python::handle<> htraceback(boost::python::allow_null(std::get<2>(error)));
  // Import the `traceback` module and use it to format the exception
  boost::python::object traceback        = boost::python::import("traceback");
  boost::python::object format_exception = traceback.attr("format_exception");
  boost::python::object formatted_list   = format_exception(htype, hvalue, htraceback);
  boost::python::object formatted        = boost::python::str("").join(formatted_list);
  return boost::python::extract<std::string>(formatted);
}

exception::exception(const error& error)
    : m_what(python_error_string(error))
{
  if (!m_what.empty() && m_what.back() == '\n') {
    m_what.pop_back();
  }
}

const char* exception::what() const noexcept
{
  return m_what.c_str();
}

}  // namespace python
}  // namespace rstream
