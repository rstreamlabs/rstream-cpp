// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>

#include "common.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

class factory;

class element;

class element_cast {
 public:
  template <typename P>
  std::shared_ptr<P> operator()(const std::shared_ptr<element>& ptr);
};

class plugin;

class element {
  friend factory;

 public:
  using ptr         = std::shared_ptr<element>;
  using name        = std::string;
  using description = std::string;
  struct info {
    name m_name;
    description m_description;
  };
  using create_func = std::function<ptr()>;
  struct handle {
    info m_info;
    create_func m_create_func;
  };
  element();
  virtual ~element() = default;

 private:
  class impl;
  void initialize(const info& info, const std::shared_ptr<plugin>& parent);
  std::shared_ptr<impl> m_impl;
};

template <typename T>
std::shared_ptr<T> element_cast::operator()(const std::shared_ptr<element>& ptr)
{
  return std::dynamic_pointer_cast<T>(ptr);
}

using elements = std::map<element::name, element::handle>;

std::ostream& operator<<(std::ostream& ostream, const element::info& info);

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
