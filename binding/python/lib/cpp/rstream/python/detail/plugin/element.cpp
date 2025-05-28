// See LICENSE file in the project root for license information.

#include "element.hpp"

#include <rstream/core/log.hpp>

namespace rstream {
namespace python {
namespace detail {
namespace plugin {

element::element(const boost::python::object& object)
    : m_object(object)
{
}

const boost::python::object& element::get()
{
  return m_object;
}

}  // namespace plugin
}  // namespace detail
}  // namespace python
}  // namespace rstream
