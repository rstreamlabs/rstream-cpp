// See LICENSE file in the project root for license information.

#include <boost/python.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

BOOST_PYTHON_MODULE(common)
{
  boost::python::class_<boost::system::error_code>("error_code", boost::python::init<>())
      .def("value", &boost::system::error_code::value)
      .def("message", (std::string(boost::system::error_code::*)() const) & boost::system::error_code::message);

  boost::python::class_<boost::system::system_error>("system_error", boost::python::init<boost::system::error_code>())
      .def("code", &boost::system::system_error::code)
      .def("what", &boost::system::system_error::what);
}
