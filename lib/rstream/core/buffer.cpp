// See LICENSE file in the project root for license information.

#include "buffer.hpp"

#include <limits>
#include <list>
#include <vector>

#include <boost/system/system_error.hpp>

#include <rstream/config.hpp>

#include "error.hpp"

namespace rstream {
namespace core {

static void check_size_add(std::size_t base, std::size_t value)
{
  if (value > std::numeric_limits<std::size_t>::max() - base) {
    throw boost::system::system_error(error::code::invalid_size);
  }
}

class RSTREAM_GNUC_INTERNAL buffer::impl {
 public:
  impl(allocator::ptr allocator);
  allocator::ptr m_allocator;
  memory_blocks m_memory_blocks;
};

buffer::buffer(allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), allocator);
}

buffer::buffer(const memory& memory)
    : buffer((allocator::ptr) nullptr)
{
  append(std::forward<decltype(memory)>(memory));
}

buffer::operator bool() const noexcept
{
  return !m_impl->m_memory_blocks.empty();
}

void buffer::prepend(const memory& memory)
{
  insert_memory(0, memory);
}

void buffer::append(const memory& memory)
{
  insert_memory(-1, memory);
}

void buffer::append(const buffer& buffer)
{
  auto memory_blocks = std::vector<memory>(buffer.m_impl->m_memory_blocks.begin(), buffer.m_impl->m_memory_blocks.end());
  for (const auto& memory : memory_blocks) {
    append(memory);
  }
}

void buffer::insert_memory(int index, const memory& memory)
{
  if (!memory) {
    throw boost::system::system_error(error::code::object_null);
  }
  if (!(index == -1 || (index >= 0 && index <= m_impl->m_memory_blocks.size()))) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  memory_blocks::iterator it = m_impl->m_memory_blocks.begin();
  std::advance(it, index != -1 ? index : m_impl->m_memory_blocks.size());
  m_impl->m_memory_blocks.insert(it, memory);
}

std::size_t buffer::extract(void* dst, std::size_t offset, std::size_t size) const
{
  std::size_t left = size;
  std::size_t i    = 0;
  for (auto it = m_impl->m_memory_blocks.begin(); it != m_impl->m_memory_blocks.end(); ++it) {
    if (left == 0) {
      break;
    }
    auto mem      = it;
    auto mem_size = mem->get_size();
    if (mem_size > offset) {
      std::size_t to_copy = std::min(mem_size - offset, left);
      memcpy(&((std::uint8_t*)dst)[i], &((const std::uint8_t*)mem->get_const_data())[offset], to_copy);
      left -= to_copy;
      i += to_copy;
      offset = 0;
    }
    else {
      offset -= mem_size;
    }
  }
  return size - left;
}

std::size_t buffer::fill(const void* src, std::size_t offset, std::size_t size)
{
  std::size_t left = size;
  std::size_t i    = 0;
  for (auto it = m_impl->m_memory_blocks.begin(); it != m_impl->m_memory_blocks.end(); ++it) {
    if (left == 0) {
      break;
    }
    auto mem      = map_memory_block(it, map_mode::write);
    auto mem_size = mem->get_size();
    if (mem_size > offset) {
      std::size_t to_copy = std::min(mem_size - offset, left);
      memcpy(&((std::uint8_t*)mem->get_data())[offset], &((const std::uint8_t*)src)[i], to_copy);
      left -= to_copy;
      i += to_copy;
      offset = 0;
    }
    else {
      offset -= mem_size;
    }
  }
  return size - left;
}

const memory buffer::map(map_mode mode)
{
  return map_range(mode, 0, -1);
}

const memory buffer::map_range(map_mode mode, unsigned int index, int length)
{
  process_args(index, length);
  memory result = nullptr;
  if (length == 1) {
    auto it = m_impl->m_memory_blocks.begin();
    std::advance(it, index);
    result = *map_memory_block(it, mode);
  }
  else if (length != 0) {
    auto size = get_size_range(index, length);
    result    = make_memory_allocated(size, m_impl->m_allocator);
    auto it   = m_impl->m_memory_blocks.begin();
    std::advance(it, index);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < length; ++i) {
      auto mem            = *map_memory_block(it, mode);
      std::size_t to_copy = mem.get_size();
      memcpy(&((std::uint8_t*)result.get_data())[offset], mem.get_const_data(), to_copy);
      offset += to_copy;
      it = m_impl->m_memory_blocks.erase(it);
    }
    m_impl->m_memory_blocks.insert(it, result);
  }
  return result;
}

std::size_t buffer::get_size() const
{
  return get_size_range(0, -1);
}

std::size_t buffer::get_size(std::size_t& offset, std::size_t& maxsize) const
{
  return get_size_range(0, -1, &offset, &maxsize);
}

std::size_t buffer::get_size_range(unsigned int index, int length, std::size_t* p_offset, std::size_t* p_maxsize) const
{
  process_args(index, length);
  std::size_t size, offset, extra;
  size = offset = extra = 0;
  auto it               = m_impl->m_memory_blocks.begin();
  std::advance(it, index);
  for (std::size_t i = 0; i < length; ++i) {
    std::size_t mem_size, mem_offset, mem_maxsize;
    mem_size = mem_offset = mem_maxsize = 0;
    mem_size                         = it->get_size(mem_offset, mem_maxsize);
    if (mem_offset > mem_maxsize || mem_size > mem_maxsize - mem_offset) {
      throw boost::system::system_error(error::code::invalid_size);
    }
    if (mem_size != 0) {
      if (size == 0) {
        offset = extra + mem_offset;
      }
      check_size_add(size, mem_size);
      size += mem_size;
      extra = mem_maxsize - (mem_offset + mem_size);
    }
    else {
      check_size_add(extra, mem_maxsize);
      extra += mem_maxsize;
    }
    ++it;
  }
  if (p_offset) {
    *p_offset = offset;
  }
  if (p_maxsize) {
    check_size_add(offset, size);
    check_size_add(offset + size, extra);
    *p_maxsize = offset + size + extra;
  }
  return size;
}

void buffer::resize(std::size_t offset)
{
  resize_range_checked(0, -1, offset, false, 0);
}

void buffer::resize(std::size_t offset, std::size_t size)
{
  resize_range_checked(0, -1, offset, true, size);
}

void buffer::resize_range(unsigned int index, int length, std::size_t offset, int size)
{
  if (size < -1) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  resize_range_checked(index, length, offset, size != -1, size != -1 ? static_cast<std::size_t>(size) : 0);
}

void buffer::resize_range_checked(unsigned int index, int length, std::size_t offset, bool has_size, std::size_t size)
{
  process_args(index, length);
  std::size_t buf_size, buf_offset, buf_maxsize;
  buf_size = get_size_range(index, length, &buf_offset, &buf_maxsize);
  if (buf_offset > buf_maxsize || offset > buf_maxsize - buf_offset) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  if (!has_size) {
    if (!(buf_size >= offset)) {
      throw boost::system::system_error(error::code::invalid_size);
    }
    size = buf_size - offset;
  }
  if (size > buf_maxsize - buf_offset - offset) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  if (offset == 0 && size == buf_size) {
    return;
  }
  auto it = m_impl->m_memory_blocks.begin();
  std::advance(it, index);
  for (std::size_t i = 0; i < length; ++i) {
    std::size_t left, noffs, mem_size;
    noffs    = 0;
    mem_size = it->get_size();
    if (i + 1 == length) {
      left = size;
    }
    else if (mem_size <= offset) {
      left   = 0;
      noffs  = offset - mem_size;
      offset = 0;
    }
    else {
      left = std::min(mem_size - offset, (std::size_t)size);
    }
    if (offset != 0 || left != mem_size) {
      it->resize(offset, left);
    }
    offset = noffs;
    size -= left;
    ++it;
  }
}

void buffer::set_size(std::size_t size)
{
  resize(0, size);
}

void buffer::reset_size()
{
  for (auto& memory : m_impl->m_memory_blocks) {
    memory.reset_size();
  }
}

buffer buffer::copy(std::size_t offset) const
{
  return copy(offset, get_size() - offset);
}

buffer buffer::copy(std::size_t offset, std::size_t size) const
{
  if (offset > get_size()) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  if (size > get_size() - offset) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  buffer buffer = make_buffer_allocated(size);
  extract(buffer.map(map_mode::write).get_data(), offset, size);
  return buffer;
}

const buffer::memory_blocks& buffer::get_memory_blocks() const
{
  return m_impl->m_memory_blocks;
}

buffer::memory_blocks::iterator buffer::map_memory_block(const memory_blocks::iterator& it, map_mode mode)
{
  if (mode == map_mode::write && !it->is_mutable()) {
    it->make_mutable();
  }
  return it;
}

void buffer::process_args(unsigned int index, int& length) const
{
  auto max_length = m_impl->m_memory_blocks.size();
  if (length < -1 || index > max_length) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  if (length == -1) {
    if (max_length - index > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw boost::system::system_error(error::code::invalid_size);
    }
    length = max_length - index;
  }
  else if (static_cast<std::size_t>(length) > max_length - index) {
    throw boost::system::system_error(error::code::invalid_size);
  }
}

buffer make_buffer_allocated(std::size_t size, allocator::ptr allocator)
{
  buffer buffer(allocator);
  auto memory = make_memory_allocated(size, allocator);
  buffer.append(memory);
  return buffer;
}

buffer make_buffer_wrapped(void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func, allocator::ptr allocator)
{
  buffer buffer(allocator);
  auto memory = make_memory_wrapped(data, max_size, offset, destroy_notify_func, allocator);
  buffer.append(memory);
  return buffer;
}

buffer make_buffer_wrapped(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator)
{
  buffer buffer(allocator);
  auto memory = make_memory_wrapped(data, max_size, offset, allocator);
  buffer.append(memory);
  return buffer;
}

buffer make_buffer_wrapped(allocator::ptr allocator, void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func)
{
  return make_buffer_wrapped(data, max_size, offset, destroy_notify_func, allocator);
}

buffer make_buffer_wrapped(allocator::ptr allocator, const void* data, std::size_t max_size, std::size_t offset)
{
  return make_buffer_wrapped(data, max_size, offset, allocator);
}

buffer::impl::impl(allocator::ptr allocator)
    : m_allocator(allocator),
      m_memory_blocks(allocator::wrapper<memory>(allocator))
{
}

}  // namespace core
}  // namespace rstream
