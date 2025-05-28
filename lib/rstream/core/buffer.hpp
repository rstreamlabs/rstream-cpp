// See LICENSE file in the project root for license information.

#pragma once

#include <list>
#include <memory>

#include "allocator.hpp"
#include "memory.hpp"

namespace rstream {
namespace core {

namespace helpers {
class memory_sequence;
}

class buffer {
  friend class helpers::memory_sequence;

 public:
  class impl;
  template <class T>
  using container     = std::list<T, allocator::wrapper<T>>;
  using memory_blocks = container<memory>;
  enum class map_mode {
    read  = 0,
    write = 1,
  };
  buffer(allocator::ptr allocator = nullptr);
  buffer(const memory& memory);
  operator bool() const noexcept;
  void prepend(const memory& memory);
  void append(const memory& memory);
  void append(const buffer& buffer);
  void insert_memory(int index, const memory& memory);
  std::size_t extract(void* dst, std::size_t offset, std::size_t size) const;
  std::size_t fill(const void* src, std::size_t offset, std::size_t size);
  const memory map(map_mode mode = map_mode::write);
  const memory map_range(map_mode mode, unsigned int index, int length = -1);
  std::size_t get_size() const;
  std::size_t get_size(std::size_t& offset, std::size_t& maxsize) const;
  std::size_t get_size_range(unsigned int index, int length = -1, std::size_t* p_offset = nullptr, std::size_t* p_maxsize = nullptr) const;
  void resize(std::size_t offset);
  void resize(std::size_t offset, std::size_t size);
  void resize_range(unsigned int index, int length, std::size_t offset, int size = -1);
  void set_size(std::size_t size);
  void reset_size();
  buffer copy(std::size_t offset = 0) const;
  buffer copy(std::size_t offset, std::size_t size) const;

 private:
  const memory_blocks& get_memory_blocks() const;
  memory_blocks::iterator map_memory_block(const memory_blocks::iterator& it, map_mode mode);
  void process_args(unsigned int index, int& length) const;
  std::shared_ptr<impl> m_impl;
};

buffer make_buffer_allocated(std::size_t size, allocator::ptr allocator = nullptr);
buffer make_buffer_wrapped(void* data, std::size_t max_size, std::size_t offset = 0, const memory::destroy_notify_func& destroy_notify_func = nullptr, allocator::ptr allocator = nullptr);
buffer make_buffer_wrapped(const void* data, std::size_t max_size, std::size_t offset = 0, allocator::ptr allocator = nullptr);
buffer make_buffer_wrapped(allocator::ptr allocator, void* data, std::size_t max_size, std::size_t offset = 0, const memory::destroy_notify_func& destroy_notify_func = nullptr);
buffer make_buffer_wrapped(allocator::ptr allocator, const void* data, std::size_t max_size, std::size_t offset = 0);

}  // namespace core
}  // namespace rstream
