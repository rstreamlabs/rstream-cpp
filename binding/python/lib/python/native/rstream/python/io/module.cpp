// See LICENSE file in the project root for license information.

#include <boost/python.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/io/address.hpp>

#include "io.hpp"

BOOST_PYTHON_MODULE(io)
{
  boost::python::class_<rstream::io::address>("address", boost::python::init<const std::string&>());
}
