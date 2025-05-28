// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include "common.hpp"
#include "registry.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <class T>
class builder {
 public:
  builder() = default;
  builder& name(const std::string& name);
  builder& help(const std::string& help);
  builder& labels(const detail::metrics::labels& labels);
  builder& registry(registry::ptr registry);
  template <class... Args>
  T build(Args&... args);

 private:
  std::string m_name;
  std::string m_help;
  detail::metrics::labels m_labels;
  registry::ptr m_registry;
};

template <class T>
builder<T>& builder<T>::name(const std::string& name)
{
  m_name = name;
  return *this;
}

template <class T>
builder<T>& builder<T>::help(const std::string& help)
{
  m_help = help;
  return *this;
}

template <class T>
builder<T>& builder<T>::labels(const detail::metrics::labels& labels)
{
  m_labels = labels;
  return *this;
}

template <class T>
builder<T>& builder<T>::registry(registry::ptr registry)
{
  m_registry = registry;
  return *this;
}

template <class T>
template <class... Args>
T builder<T>::build(Args&... args)
{
  return T(m_name, m_help, m_labels, m_registry, std::forward<Args>(args)...);
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
