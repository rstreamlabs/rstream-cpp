// See LICENSE file in the project root for license information.

#include "memory.hpp"

#include <cstring>

#include <boost/make_shared.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/config.hpp>

#include "error.hpp"

namespace rstream {
namespace core {

class RSTREAM_GNUC_INTERNAL memory::impl {
 public:
  class base;
  impl(std::size_t size, allocator::ptr allocator);
  impl(void* data, std::size_t max_size, std::size_t offset, const destroy_notify_func& destroy_notify_func, allocator::ptr allocator);
  impl(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator);
  impl(const impl& other, allocator::ptr allocator);
  std::size_t m_size;
  std::size_t m_offset;
  std::shared_ptr<const base> m_base;
};

class RSTREAM_GNUC_INTERNAL memory::impl::base {
 public:
  base(allocator::ptr allocator, bool is_mutable);
  bool is_mutable() const;
  virtual void* get_data() const;
  virtual const void* get_const_data() const = 0;
  virtual std::size_t get_size() const       = 0;
  allocator::ptr get_allocator() const;

 private:
  allocator::ptr m_allocator;
  bool m_is_mutable;
};

class RSTREAM_GNUC_INTERNAL memory_allocated : public memory::impl::base {
 public:
  memory_allocated(std::size_t size, allocator::ptr allocator);
  void* get_data() const override;
  const void* get_const_data() const override;
  std::size_t get_size() const override;

 private:
  const boost::shared_ptr<std::uint8_t[]> m_ptr;
  const std::size_t m_size;
};

class RSTREAM_GNUC_INTERNAL memory_wrapped_mutable : public memory::impl::base {
 public:
  memory_wrapped_mutable(void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func, allocator::ptr allocator);
  virtual ~memory_wrapped_mutable();
  void* get_data() const override;
  const void* get_const_data() const override;
  std::size_t get_size() const override;

 private:
  void* m_data;
  std::size_t m_max_size;
  std::size_t m_offset;
  const memory::destroy_notify_func m_destroy_notify_func;
};

class RSTREAM_GNUC_INTERNAL memory_wrapped_const : public memory::impl::base {
 public:
  memory_wrapped_const(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator);
  const void* get_const_data() const override;
  std::size_t get_size() const override;

 private:
  const void* m_data;
  std::size_t m_max_size;
  std::size_t m_offset;
};

memory::memory(std::size_t size, allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), size, allocator);
}

memory::memory(void* data, std::size_t max_size, std::size_t offset, const destroy_notify_func& destroy_notify_func, allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), data, max_size, offset, destroy_notify_func, allocator);
}

memory::memory(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), data, max_size, offset, allocator);
}

memory::memory(const memory& other, allocator::ptr allocator)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), *other.m_impl, allocator);
}

memory::memory(std::nullptr_t)
{
  m_impl = nullptr;
}

memory::memory()
    : memory(nullptr)
{
}

memory::operator bool() const noexcept
{
  return m_impl != nullptr;
}

bool memory::is_mutable() const
{
  return m_impl != nullptr ? m_impl->m_base->is_mutable() : false;
}

void memory::make_mutable()
{
  if (is_mutable()) {
    return;
  }

  *this = copy();
}

void* memory::get_data() const
{
  return m_impl != nullptr ? &((uint8_t*)m_impl->m_base->get_data())[m_impl->m_offset] : nullptr;
}

const void* memory::get_const_data() const
{
  return m_impl != nullptr ? &((uint8_t*)m_impl->m_base->get_const_data())[m_impl->m_offset] : nullptr;
}

std::size_t memory::get_size() const
{
  return m_impl != nullptr ? m_impl->m_size : 0;
}

std::size_t memory::get_size(std::size_t& offset, std::size_t& maxsize) const
{
  if (m_impl == nullptr) {
    return 0;
  }
  offset  = m_impl->m_offset;
  maxsize = m_impl->m_base->get_size();
  return m_impl->m_size;
}

void memory::resize(std::size_t offset)
{
  if (m_impl == nullptr) {
    return;
  }
  if (offset > get_size()) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  m_impl->m_offset += offset;
  m_impl->m_size -= offset;
}

void memory::resize(std::size_t offset, std::size_t size)
{
  if (m_impl == nullptr) {
    return;
  }
  if (m_impl->m_offset + offset + size > m_impl->m_base->get_size()) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  m_impl->m_offset += offset;
  m_impl->m_size = size;
}

void memory::set_size(std::size_t size)
{
  resize(0, size);
}

void memory::reset_size()
{
  std::size_t offset, maxsize;
  get_size(offset, maxsize);
  resize(offset, maxsize);
}

memory memory::copy(std::size_t offset) const
{
  return copy(offset, get_size() - offset);
}

memory memory::copy(std::size_t offset, std::size_t size) const
{
  if (offset > get_size()) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  if (size > get_size() - offset) {
    throw boost::system::system_error(error::code::invalid_size);
  }
  memory memory(size, m_impl->m_base->get_allocator());
  memcpy(memory.get_data(), &((const uint8_t*)get_const_data())[offset], size);
  return memory;
}

memory memory::share(std::size_t offset, std::size_t size)
{
  memory memory(*this, m_impl->m_base->get_allocator());
  memory.resize(offset, size);
  return memory;
}

const memory memory::share(std::size_t offset, std::size_t size) const
{
  memory memory(*this, m_impl->m_base->get_allocator());
  memory.resize(offset, size);
  return memory;
}

memory::impl::impl(std::size_t size, allocator::ptr allocator)
    : m_size(size),
      m_offset(0)
{
  m_base = std::allocate_shared<memory_allocated>(core::allocator::wrapper<memory_allocated>(allocator), size, allocator);
}

memory::impl::impl(void* data, std::size_t max_size, std::size_t offset, const destroy_notify_func& destroy_notify_func, allocator::ptr allocator)
    : m_size(max_size - offset),
      m_offset(0)
{
  m_base = std::allocate_shared<memory_wrapped_mutable>(core::allocator::wrapper<memory_wrapped_mutable>(allocator), data, max_size, offset, destroy_notify_func, allocator);
}

memory::impl::impl(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator)
    : m_size(max_size - offset),
      m_offset(0)
{
  m_base = std::allocate_shared<memory_wrapped_const>(core::allocator::wrapper<memory_wrapped_const>(allocator), data, max_size, offset, allocator);
}

memory::impl::impl(const impl& other, allocator::ptr allocator)
    : m_size(other.m_size),
      m_offset(other.m_offset),
      m_base(other.m_base)
{
}

memory::impl::base::base(allocator::ptr allocator, bool is_mutable)
    : m_allocator(allocator),
      m_is_mutable(is_mutable)
{
}

bool memory::impl::base::is_mutable() const { return m_is_mutable; }

void* memory::impl::base::get_data() const { throw boost::system::system_error(error::code::object_not_writable); }

allocator::ptr memory::impl::base::get_allocator() const
{
  return m_allocator;
}

memory_allocated::memory_allocated(std::size_t size, allocator::ptr allocator)
    : memory::impl::base(allocator, true),
      m_ptr(boost::allocate_shared<std::uint8_t[]>(allocator::wrapper<std::uint8_t>(allocator), size)),
      m_size(size)
{
}

void* memory_allocated::get_data() const { return m_ptr.get(); }

const void* memory_allocated::get_const_data() const { return m_ptr.get(); }

std::size_t memory_allocated::get_size() const { return m_size; }

memory_wrapped_mutable::memory_wrapped_mutable(void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func, allocator::ptr allocator)
    : memory::impl::base(allocator, true),
      m_data(data),
      m_max_size(max_size),
      m_offset(offset),
      m_destroy_notify_func(destroy_notify_func)
{
}

memory_wrapped_mutable::~memory_wrapped_mutable()
{
  if (m_destroy_notify_func) {
    m_destroy_notify_func(m_data);
  }
}

void* memory_wrapped_mutable::get_data() const { return m_data; }

const void* memory_wrapped_mutable::get_const_data() const { return m_data; }

std::size_t memory_wrapped_mutable::get_size() const { return (m_max_size - m_offset); }

memory_wrapped_const::memory_wrapped_const(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator)
    : memory::impl::base(allocator, false),
      m_data(data),
      m_max_size(max_size),
      m_offset(offset)
{
}

const void* memory_wrapped_const::get_const_data() const { return m_data; }

std::size_t memory_wrapped_const::get_size() const { return (m_max_size - m_offset); }

memory make_memory_allocated(std::size_t size, allocator::ptr allocator)
{
  return memory(size, allocator);
}

memory make_memory_wrapped(void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func, allocator::ptr allocator)
{
  return memory(data, max_size, offset, destroy_notify_func, allocator);
}

memory make_memory_wrapped(const void* data, std::size_t max_size, std::size_t offset, allocator::ptr allocator)
{
  return memory(data, max_size, offset, allocator);
}

memory make_memory_wrapped(allocator::ptr allocator, void* data, std::size_t max_size, std::size_t offset, const memory::destroy_notify_func& destroy_notify_func)
{
  return make_memory_wrapped(data, max_size, offset, destroy_notify_func, allocator);
}

memory make_memory_wrapped(allocator::ptr allocator, const void* data, std::size_t max_size, std::size_t offset)
{
  return make_memory_wrapped(data, max_size, offset, allocator);
}

memory make_memory_shared(allocator::ptr allocator, memory& other, std::size_t offset, std::size_t size)
{
  return other.share(offset, size);
}

}  // namespace core
}  // namespace rstream
