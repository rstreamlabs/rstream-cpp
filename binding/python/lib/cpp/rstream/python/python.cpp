// See LICENSE file in the project root for license information.

#include "python.hpp"

#include <boost/filesystem.hpp>

#include <rstream/python/exception.hpp>

namespace rstream {
namespace python {

static boost::python::object import_module_file_internal(const std::string& fullname, const std::string& path);

void prepend_sys_path(const std::string& path)
{
  boost::python::import("sys").attr("path").attr("insert")(0, path.c_str());
}

boost::python::object import_module_file(const std::string& fullname, const std::string& path)
{
  try {
    return import_module_file_internal(fullname, path);
  }
  catch (const boost::python::error_already_set&) {
    throw rstream::python::exception();
  }
}

boost::python::object import_module_file(const std::string& path)
{
  return import_module_file(boost::filesystem::path(path).stem().string(), path);
}

boost::python::object import_module_file_internal(const std::string& fullname, const std::string& path)
{
  return boost::python::import("importlib.machinery").attr("SourceFileLoader")(fullname.c_str(), path.c_str()).attr("load_module")();
}

}  // namespace python
}  // namespace rstream
