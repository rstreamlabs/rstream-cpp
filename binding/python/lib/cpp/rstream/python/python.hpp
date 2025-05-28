// See LICENSE file in the project root for license information.

#pragma once

#include <boost/python.hpp>

namespace rstream {
namespace python {

void prepend_sys_path(const std::string& path);

boost::python::object import_module_file(const std::string& fullname, const std::string& path);
boost::python::object import_module_file(const std::string& path);

}  // namespace python
}  // namespace rstream
