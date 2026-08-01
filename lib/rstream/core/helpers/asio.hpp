// See LICENSE file in the project root for license information.

#pragma once

#include <string_view>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/memory.hpp>

namespace rstream {
namespace core {
namespace helpers {

template <typename ActualErrorCode, typename ExpectedErrorCode>
bool matches_error(const ActualErrorCode& actual, const ExpectedErrorCode& expected) noexcept
{
  if (actual.value() != expected.value()) {
    return false;
  }
  return std::string_view(actual.category().name()) == expected.category().name();
}

template <typename ErrorCode>
bool is_eof_error(const ErrorCode& error_code) noexcept
{
  if (!error_code) {
    return false;
  }
  if (matches_error(error_code, boost::system::error_code(boost::asio::error::eof))) {
    return true;
  }
#ifdef _WIN32
  if (matches_error(error_code, boost::system::error_code(boost::asio::error::broken_pipe))) {
    return true;
  }
#endif
  return false;
}

class memory_sequence {
 public:
  class memory_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    bool operator==(const memory_iterator& rhs) const;
    bool operator!=(const memory_iterator& rhs) const;

   protected:
    memory_iterator(buffer::memory_blocks::const_iterator it);
    buffer::memory_blocks::const_iterator& iterator();
    const buffer::memory_blocks::const_iterator& iterator() const;

   private:
    buffer::memory_blocks::const_iterator m_it;
  };

 protected:
  memory_sequence(const buffer::memory_blocks& memory_blocks);
  memory_sequence(const buffer& buffer);
  const buffer::memory_blocks& get_memory_blocks() const;

 private:
  const buffer::memory_blocks* m_memory_blocks;
};

class const_memory_sequence : public memory_sequence {
 public:
  class const_memory_iterator : public memory_iterator {
    friend class const_memory_sequence;

   public:
    using value_type      = boost::asio::const_buffer;
    using reference       = value_type&;
    using pointer         = value_type*;
    using const_reference = const value_type&;
    using const_pointer   = const value_type*;
    const_memory_iterator();
    const_memory_iterator& operator++();
    const_memory_iterator operator++(int);
    const_memory_iterator& operator--();
    const_memory_iterator operator--(int);
    const_reference operator*() const;
    const_pointer operator->() const;

   private:
    void update_value() const;
    const_memory_iterator(buffer::memory_blocks::const_iterator it);
    mutable value_type m_value;
  };
  const_memory_sequence(const buffer::memory_blocks& memory_blocks);
  const_memory_sequence(const buffer& buffer);
  const_memory_iterator begin() const;
  const_memory_iterator end() const;
};

class mutable_memory_sequence : public memory_sequence {
 public:
  class mutable_memory_iterator : public memory_iterator {
    friend class mutable_memory_sequence;

   public:
    using value_type      = boost::asio::mutable_buffer;
    using reference       = value_type&;
    using pointer         = value_type*;
    using const_reference = const value_type&;
    using const_pointer   = const value_type*;
    mutable_memory_iterator();
    mutable_memory_iterator& operator++();
    mutable_memory_iterator operator++(int);
    mutable_memory_iterator& operator--();
    mutable_memory_iterator operator--(int);
    const_reference operator*() const;
    const_pointer operator->() const;

   private:
    void update_value() const;
    mutable_memory_iterator(buffer::memory_blocks::const_iterator it);
    mutable value_type m_value;
  };
  mutable_memory_sequence(const buffer::memory_blocks& memory_blocks);
  mutable_memory_sequence(const buffer& buffer);
  mutable_memory_iterator begin() const;
  mutable_memory_iterator end() const;
};

}  // namespace helpers
}  // namespace core
}  // namespace rstream
