// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>

#include "allocator.hpp"

namespace rstream {
namespace core {

class memory {
 public:
  class impl;
  using destroy_notify_func = std::function<void(void*)>;
  memory(std::size_t size, allocator::ptr allocator);
  memory(void* data, std::size_t max_size, std::size_t offset, const destroy_notify_func& destroy_notify_func, allocator::ptr allocator);
  memory(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator);
  memory(std::nullptr_t);
  memory();
  //    memory& operator=(const memory&) = delete;
  //    memory& operator=(memory&) = default;
  operator bool() const noexcept;
  bool is_mutable() const;
  void make_mutable();
  void* get_data() const;
  const void* get_const_data() const;
  std::size_t get_size() const;
  std::size_t get_size(std::size_t& offset, std::size_t& maxsize) const;
  void resize(std::size_t offset);
  void resize(std::size_t offset, std::size_t size);
  void set_size(std::size_t size);
  void reset_size();
  memory copy(std::size_t offset = 0) const;
  memory copy(std::size_t offset, std::size_t size) const;
  memory share(std::size_t offset, std::size_t size);
  const memory share(std::size_t offset, std::size_t size) const;

 private:
  friend memory make_memory_shared(allocator::ptr allocator, memory& other, std::size_t offset, std::size_t size);
  memory(const memory& other, allocator::ptr allocator);
  std::shared_ptr<impl> m_impl;
};

memory make_memory_allocated(std::size_t size, allocator::ptr allocator = nullptr);
memory make_memory_wrapped(void* data, std::size_t max_size, std::size_t offset = 0, const memory::destroy_notify_func& destroy_notify_func = nullptr, allocator::ptr allocator = nullptr);
memory make_memory_wrapped(const void* data, std::size_t max_size, std::size_t offset = 0, allocator::ptr allocator = nullptr);
memory make_memory_wrapped(allocator::ptr allocator, void* data, std::size_t max_size, std::size_t offset = 0, const memory::destroy_notify_func& destroy_notify_func = nullptr);
memory make_memory_wrapped(allocator::ptr allocator, const void* data, std::size_t max_size, std::size_t offset = 0);
memory make_memory_shared(allocator::ptr allocator, memory& other, std::size_t offset, std::size_t size);

}  // namespace core
}  // namespace rstream
