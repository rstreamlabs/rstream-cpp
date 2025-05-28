// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/core/plugin.hpp>
#include <rstream/python/python.hpp>

namespace rstream {
namespace python {
namespace detail {
namespace plugin {

class element;

struct element_cast {
  template <typename T>
  std::shared_ptr<T> operator()(const rstream::core::plugin::element::ptr& ptr);
  template <typename T>
  static std::shared_ptr<T> get(const std::shared_ptr<element>& ptr);
  template <typename T>
  static std::shared_ptr<T> get(const boost::python::object& object);
};

class element : public rstream::core::plugin::element {
  friend struct element_cast;

 public:
  using ptr = std::shared_ptr<element>;
  element(const boost::python::object& object);
  virtual ~element() = default;
  const boost::python::object& get();

 private:
  boost::python::object m_object;
};

template <typename T>
std::shared_ptr<T> element_cast::operator()(const rstream::core::plugin::element::ptr& ptr)
{
  rstream::core::detail::plugin::element_cast base;
  auto element_ptr = base.operator()<class element>(ptr);
  return element_ptr ? element_cast::get<T>(element_ptr) : base.operator()<T>(ptr);
}

template <typename T>
std::shared_ptr<T> element_cast::get(const element::ptr& ptr)
{
  return nullptr;  // this aims to be overloaded
}

}  // namespace plugin
}  // namespace detail
}  // namespace python
}  // namespace rstream
