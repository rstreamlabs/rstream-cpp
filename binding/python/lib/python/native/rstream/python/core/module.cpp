// See LICENSE file in the project root for license information.

#include <boost/python.hpp>

#include "core.hpp"

BOOST_PYTHON_MODULE(core)
{
  boost::python::def("version", &rstream::python::core::version);
}
