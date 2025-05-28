// See LICENSE file in the project root for license information.

#pragma once

#include <exception>
#include <string>
#include <tuple>

#include <Python.h>

namespace rstream {
namespace python {

using error = std::tuple<PyObject*, PyObject*, PyObject*>;

error fetch_error();

std::string python_error_string(const error& error);

class exception : public std::exception {
 public:
  exception(const error& error = fetch_error());

  const char* what() const noexcept override;

 private:
  std::string m_what;
};

}  // namespace python
}  // namespace rstream
