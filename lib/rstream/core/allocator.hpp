// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace rstream {
namespace core {

class allocator : public std::enable_shared_from_this<allocator> {
 public:
  using ptr = std::shared_ptr<allocator>;

  virtual ~allocator() = default;

  virtual void* allocate(std::size_t size) = 0;
  virtual void deallocate(void* pointer)   = 0;

  template <typename T>
  struct wrapper {
    using size_type       = std::size_t;
    using value_type      = T;
    using difference_type = std::ptrdiff_t;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;

    wrapper(allocator::ptr allocator) noexcept
        : m_allocator(allocator) {};

    bool operator!=(const wrapper& other) const
    {
      return m_allocator != other.m_allocator;
    }

    template <class U>
    struct rebind {
      typedef wrapper<U> other;
    };

    template <typename U>
    wrapper(const wrapper<U>& other)
        : m_allocator(other.m_allocator)
    {
    }

    allocator::ptr get_allocator() const;

    pointer allocate(std::size_t n)
    {
      if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::bad_array_new_length();
      }
      return static_cast<T*>(wrapper::get_allocator()->allocate(n * sizeof(T)));
    }

    void deallocate(pointer ptr, size_type)
    {
      return wrapper::get_allocator()->deallocate(ptr);
    }

    allocator::ptr m_allocator;
  };

  template <typename T>
  wrapper<T> make_wrapper()
  {
    return wrapper<T>(shared_from_this());
  }
};

allocator::ptr default_allocator();

template <typename T>
allocator::ptr allocator::wrapper<T>::get_allocator() const
{
  return (m_allocator ? m_allocator : default_allocator());
}

}  // namespace core
}  // namespace rstream
