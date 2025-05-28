// See LICENSE file in the project root for license information.

#include "asio.hpp"

namespace rstream {
namespace core {
namespace helpers {

bool memory_sequence::memory_iterator::operator==(const memory_iterator& rhs) const { return m_it == rhs.m_it; }
bool memory_sequence::memory_iterator::operator!=(const memory_iterator& rhs) const { return m_it != rhs.m_it; }
memory_sequence::memory_iterator::memory_iterator(buffer::memory_blocks::const_iterator it)
    : m_it(it)
{
}

buffer::memory_blocks::const_iterator& memory_sequence::memory_iterator::iterator() { return m_it; }

const buffer::memory_blocks::const_iterator& memory_sequence::memory_iterator::iterator() const { return m_it; }

memory_sequence::memory_sequence(const buffer::memory_blocks& memory_blocks)
    : m_memory_blocks(&memory_blocks)
{
}

memory_sequence::memory_sequence(const buffer& buffer)
    : memory_sequence(buffer.get_memory_blocks())
{
}

const buffer::memory_blocks& memory_sequence::get_memory_blocks() const { return *m_memory_blocks; }

const_memory_sequence::const_memory_iterator::const_memory_iterator(buffer::memory_blocks::const_iterator it)
    : memory_sequence::memory_iterator(it)
{
}

const_memory_sequence::const_memory_iterator::const_memory_iterator()
    : const_memory_iterator(buffer::memory_blocks::const_iterator())
{
}

const_memory_sequence::const_memory_iterator& const_memory_sequence::const_memory_iterator::operator++()
{
  iterator()++;
  return *this;
}

const_memory_sequence::const_memory_iterator const_memory_sequence::const_memory_iterator::operator++(int)
{
  const_memory_iterator result(iterator());
  iterator()++;
  return result;
}

const_memory_sequence::const_memory_iterator& const_memory_sequence::const_memory_iterator::operator--()
{
  iterator()--;
  return *this;
}

const_memory_sequence::const_memory_iterator const_memory_sequence::const_memory_iterator::operator--(int)
{
  const_memory_iterator result(iterator());
  iterator()--;
  return result;
}

const_memory_sequence::const_memory_iterator::const_reference const_memory_sequence::const_memory_iterator::operator*() const
{
  update_value();
  return m_value;
}

const_memory_sequence::const_memory_iterator::const_pointer const_memory_sequence::const_memory_iterator::operator->() const
{
  update_value();
  return &m_value;
}

void const_memory_sequence::const_memory_iterator::update_value() const { m_value = boost::asio::const_buffer(iterator()->get_const_data(), iterator()->get_size()); }

const_memory_sequence::const_memory_sequence(const buffer::memory_blocks& memory_blocks)
    : memory_sequence(memory_blocks)
{
}

const_memory_sequence::const_memory_sequence(const buffer& buffer)
    : memory_sequence(buffer)
{
}

const_memory_sequence::const_memory_iterator const_memory_sequence::begin() const { return const_memory_iterator(get_memory_blocks().begin()); }

const_memory_sequence::const_memory_iterator const_memory_sequence::end() const { return const_memory_iterator(get_memory_blocks().end()); }

mutable_memory_sequence::mutable_memory_iterator::mutable_memory_iterator(buffer::memory_blocks::const_iterator it)
    : memory_sequence::memory_iterator(it)
{
}

mutable_memory_sequence::mutable_memory_iterator::mutable_memory_iterator()
    : mutable_memory_iterator(buffer::memory_blocks::const_iterator())
{
}

mutable_memory_sequence::mutable_memory_iterator& mutable_memory_sequence::mutable_memory_iterator::operator++()
{
  iterator()++;
  return *this;
}

mutable_memory_sequence::mutable_memory_iterator mutable_memory_sequence::mutable_memory_iterator::operator++(int)
{
  mutable_memory_iterator result(iterator());
  iterator()++;
  return result;
}

mutable_memory_sequence::mutable_memory_iterator& mutable_memory_sequence::mutable_memory_iterator::operator--()
{
  iterator()--;
  return *this;
}

mutable_memory_sequence::mutable_memory_iterator mutable_memory_sequence::mutable_memory_iterator::operator--(int)
{
  mutable_memory_iterator result(iterator());
  iterator()--;
  return result;
}

mutable_memory_sequence::mutable_memory_iterator::const_reference mutable_memory_sequence::mutable_memory_iterator::operator*() const
{
  update_value();
  return m_value;
}

mutable_memory_sequence::mutable_memory_iterator::const_pointer mutable_memory_sequence::mutable_memory_iterator::operator->() const
{
  update_value();
  return &m_value;
}

void mutable_memory_sequence::mutable_memory_iterator::update_value() const { m_value = boost::asio::mutable_buffer(iterator()->get_data(), iterator()->get_size()); }

mutable_memory_sequence::mutable_memory_sequence(const buffer::memory_blocks& memory_blocks)
    : memory_sequence(memory_blocks)
{
}

mutable_memory_sequence::mutable_memory_sequence(const buffer& buffer)
    : memory_sequence(buffer)
{
}

mutable_memory_sequence::mutable_memory_iterator mutable_memory_sequence::begin() const { return mutable_memory_iterator(get_memory_blocks().begin()); }

mutable_memory_sequence::mutable_memory_iterator mutable_memory_sequence::end() const { return mutable_memory_iterator(get_memory_blocks().end()); }

}  // namespace helpers
}  // namespace core
}  // namespace rstream
