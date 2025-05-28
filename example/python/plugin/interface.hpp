// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/python/plugin.hpp>

class interface {
 public:
  virtual long run() = 0;
};

template <>
std::shared_ptr<interface> rstream::python::detail::plugin::element_cast::get<interface>(const rstream::python::plugin::element::ptr& ptr);
