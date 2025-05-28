// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/dll.hpp>

#include <nlohmann/json.hpp>

namespace rstream {
namespace core {
namespace detail {
namespace plugin {

using config = nlohmann::json;

using shared_library = std::shared_ptr<boost::dll::shared_library>;

enum class type {
  dynamic,
  static_,
};

template <class T, class V>
struct object_deleter {
  object_deleter(const std::shared_ptr<T> object, const std::shared_ptr<V> parent)
      : m_object(object),
        m_parent(parent)
  {
  }
  void operator()(T* ptr)
  {
    (void)ptr;
    m_object = nullptr;
    m_parent = nullptr;
  }
  std::shared_ptr<T> m_object;
  std::shared_ptr<V> m_parent;
};

}  // namespace plugin
}  // namespace detail
}  // namespace core
}  // namespace rstream
