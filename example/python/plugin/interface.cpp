// See LICENSE file in the project root for license information.

#include "interface.hpp"

#include <rstream/python/exception.hpp>

template <>
std::shared_ptr<interface> rstream::python::detail::plugin::element_cast::get<interface>(const rstream::python::plugin::element::ptr& ptr)
{
  // load python module
  auto module = rstream::python::import_module_file((boost::filesystem::path(CMAKE_CURRENT_SOURCE_DIR) / "interface.py").string());
  if (!module) {
    return nullptr;
  }

  class Bind : public interface {
   public:
    Bind(const rstream::python::plugin::element::ptr& ptr)
        : m_ptr(ptr)
    {
    }
    long run() override
    {
      try {
        return boost::python::extract<long>(m_ptr->get().attr("run")());
      }
      catch (const boost::python::error_already_set&) {
        throw rstream::python::exception();
      }
    }

   private:
    rstream::python::plugin::element::ptr m_ptr;
  };

  return std::make_shared<Bind>(ptr);
}
