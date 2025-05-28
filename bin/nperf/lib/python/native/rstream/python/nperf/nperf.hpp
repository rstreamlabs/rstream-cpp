// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/python.hpp>
#include <boost/variant.hpp>

#include <rstream/nperf/nperf.hpp>

namespace rstream {
namespace python {
namespace nperf {

template <typename T>
struct variant_wrapper {
  using variant = T;
  struct variant_to_object : boost::static_visitor<PyObject*> {
    static result_type convert(const variant& v)
    {
      return apply_visitor(variant_to_object(), v);
    }
    template <typename V>
    result_type operator()(const V& v) const
    {
      return boost::python::incref(boost::python::object(v).ptr());
    }
  };
  variant_wrapper(const variant& variant)
      : m_variant(variant)
  {
  }
  boost::python::object get()
  {
    return boost::python::object(boost::python::handle<>(variant_to_object::convert(m_variant)));
  }
  static auto accessor()
  {
    return +[](const variant& v) { return variant_wrapper(v).get(); };
  }
  const variant m_variant;
};

std::string to_string(const rstream::nperf::timestamp& timestamp, const std::string& format);

}  // namespace nperf
}  // namespace python
}  // namespace rstream
