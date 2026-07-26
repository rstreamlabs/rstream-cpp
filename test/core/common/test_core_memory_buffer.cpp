// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include <boost/system/system_error.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/error.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/core/memory.hpp>

class tracking_allocator : public rstream::core::allocator {
 public:
  void* allocate(std::size_t size) override
  {
    ++m_allocations;
    return ::operator new(size);
  }

  void deallocate(void* pointer) override
  {
    ::operator delete(pointer);
  }

  std::size_t m_allocations = 0;
};

template <typename callback_type>
static void expect_core_error(callback_type&& callback, rstream::core::error::code expected)
{
  bool thrown = false;
  try {
    callback();
  }
  catch (const boost::system::system_error& error) {
    thrown = true;
    assert(error.code().category() == rstream::core::error::rstream_core_error_category());
    assert(error.code().value() == static_cast<int>(expected));
  }
  assert(thrown);
}

static std::string to_string(const rstream::core::memory& memory)
{
  return std::string(static_cast<const char*>(memory.get_const_data()), memory.get_size());
}

static std::string to_string(rstream::core::buffer& buffer)
{
  std::string out(buffer.get_size(), '\0');
  buffer.extract(out.data(), 0, out.size());
  return out;
}

static void check_wrapped_memory_honors_offset()
{
  char mutable_raw[] = "abcdef";
  auto wrapped       = rstream::core::make_memory_wrapped(mutable_raw, 6, 2);
  assert(wrapped.get_size() == 4);
  assert(to_string(wrapped) == "cdef");
  static_cast<char*>(wrapped.get_data())[0] = 'C';
  assert(std::string(mutable_raw, 6) == "abCdef");

  const char const_raw[] = "uvwxyz";
  auto const_wrapped     = rstream::core::make_memory_wrapped(const_raw, 6, 3);
  assert(!const_wrapped.is_mutable());
  assert(to_string(const_wrapped) == "xyz");
  expect_core_error([&const_wrapped]() { (void)const_wrapped.get_data(); },
                    rstream::core::error::code::object_not_writable);

  auto detached = const_wrapped;
  detached.make_mutable();
  static_cast<char*>(detached.get_data())[0] = 'X';
  assert(to_string(detached) == "Xyz");
  assert(std::string(const_raw, 6) == "uvwxyz");
}

static void check_wrapped_memory_rejects_invalid_bounds()
{
  char raw[] = "abc";
  expect_core_error([&raw]() { (void)rstream::core::make_memory_wrapped(raw, 3, 4); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&raw]() { (void)rstream::core::make_buffer_wrapped(raw, 3, 4); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([]() { (void)rstream::core::make_memory_wrapped(static_cast<void*>(nullptr), 1, 0); },
                    rstream::core::error::code::object_null);
  expect_core_error([]() { (void)rstream::core::make_memory_wrapped(static_cast<const void*>(nullptr), 1, 0); },
                    rstream::core::error::code::object_null);

  auto empty = rstream::core::make_memory_wrapped(static_cast<void*>(nullptr), 0, 0);
  assert(empty.get_size() == 0);
}

static void check_memory_copy_share_and_bounds()
{
  auto memory       = rstream::core::make_memory_allocated(8);
  const char data[] = "abcdefgh";
  std::memcpy(memory.get_data(), data, 8);

  auto shared = memory.share(2, 4);
  assert(to_string(shared) == "cdef");
  static_cast<char*>(shared.get_data())[0] = 'C';
  assert(to_string(memory) == "abCdefgh");

  auto copied                              = memory.copy(2, 3);
  static_cast<char*>(copied.get_data())[0] = 'x';
  assert(to_string(copied) == "xde");
  assert(to_string(memory) == "abCdefgh");

  expect_core_error([&memory]() { (void)memory.copy(9); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&memory]() { memory.resize(9); },
                    rstream::core::error::code::invalid_size);

  rstream::core::memory empty;
  std::size_t empty_offset  = 42;
  std::size_t empty_maxsize = 24;
  assert(empty.get_size(empty_offset, empty_maxsize) == 0);
  assert(empty_offset == 0);
  assert(empty_maxsize == 0);
  empty.make_mutable();
  assert(!empty);
  assert(!empty.copy(0, 0));
  assert(!empty.share(0, 0));
  expect_core_error([&empty]() { (void)empty.copy(0, 1); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&empty]() { (void)empty.share(0, 1); },
                    rstream::core::error::code::invalid_size);
}

static void check_memory_shared_honors_allocator_and_null_state()
{
  auto memory       = rstream::core::make_memory_allocated(6);
  const char data[] = "abcdef";
  std::memcpy(memory.get_data(), data, 6);

  auto allocator = std::make_shared<tracking_allocator>();
  auto shared    = rstream::core::make_memory_shared(allocator, memory, 1, 4);
  assert(allocator->m_allocations > 0);
  assert(to_string(shared) == "bcde");
  static_cast<char*>(shared.get_data())[0] = 'B';
  assert(to_string(memory) == "aBcdef");

  rstream::core::memory empty;
  auto empty_shared = rstream::core::make_memory_shared(allocator, empty, 0, 0);
  assert(!empty_shared);
  expect_core_error([&allocator, &empty]() { (void)rstream::core::make_memory_shared(allocator, empty, 0, 1); },
                    rstream::core::error::code::invalid_size);
}

static void check_mutable_destroy_callback_is_preserved()
{
  bool destroyed = false;
  {
    char raw[]    = "secret";
    void* raw_ptr = raw;
    auto memory   = rstream::core::make_memory_wrapped(raw, 6, 1, [&destroyed, raw_ptr](void* data) {
      assert(data == raw_ptr);
      destroyed = true;
    });
    assert(to_string(memory) == "ecret");
  }
  assert(destroyed);
}

static void check_mutable_destroy_callback_cannot_escape_destruction()
{
  bool destroyed = false;
  {
    char raw[]  = "secret";
    auto memory = rstream::core::make_memory_wrapped(raw, 6, 0, [&destroyed](void*) {
      destroyed = true;
      throw std::runtime_error("destroy callback failure");
    });
    assert(memory.get_size() == 6);
  }
  assert(destroyed);
}

static void check_buffer_extract_fill_map_and_bounds()
{
  auto first  = rstream::core::make_memory_wrapped("abc", 3);
  auto second = rstream::core::make_memory_wrapped("defg", 4);
  rstream::core::buffer buffer;
  assert(!buffer);
  buffer.append(first);
  buffer.append(second);
  assert(buffer);
  assert(buffer.get_size() == 7);

  char extracted[5] = {};
  assert(buffer.extract(extracted, 2, 4) == 4);
  assert(std::string(extracted, 4) == "cdef");

  const char patch[] = "XYZ";
  assert(buffer.fill(patch, 1, 3) == 3);
  assert(to_string(buffer) == "aXYZefg");

  auto copy = buffer.copy(2, 3);
  assert(to_string(copy) == "YZe");

  auto mapped = buffer.map_range(rstream::core::buffer::map_mode::write, 0, 2);
  assert(mapped.is_mutable());
  static_cast<char*>(mapped.get_data())[0] = 'A';
  assert(to_string(buffer) == "AXYZefg");

  buffer.resize(1, 5);
  assert(to_string(buffer) == "XYZef");
  buffer.reset_size();
  assert(buffer.get_size() == 7);

  expect_core_error([&buffer]() { buffer.insert_memory(3, rstream::core::make_memory_allocated(1)); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&buffer]() { (void)buffer.copy(8); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&buffer]() { buffer.append(rstream::core::memory(nullptr)); },
                    rstream::core::error::code::object_null);
  expect_core_error([&buffer]() { (void)buffer.map_range(rstream::core::buffer::map_mode::read, std::numeric_limits<unsigned int>::max(), 1); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&buffer]() { buffer.resize_range(0, 1, 0, -2); },
                    rstream::core::error::code::invalid_size);
  expect_core_error([&buffer]() { buffer.set_size(std::numeric_limits<std::size_t>::max()); },
                    rstream::core::error::code::invalid_size);
}

static void check_buffer_self_append_uses_original_snapshot()
{
  auto memory = rstream::core::make_memory_wrapped("ab", 2);
  rstream::core::buffer buffer;
  buffer.append(memory);
  buffer.append(buffer);
  assert(buffer.get_size() == 4);
  assert(to_string(buffer) == "abab");
  buffer.append(buffer);
  assert(buffer.get_size() == 8);
  assert(to_string(buffer) == "abababab");
}

static void check_allocator_wrapper_rejects_overflowing_counts()
{
  auto allocator = rstream::core::default_allocator()->make_wrapper<std::uint64_t>();
  bool thrown    = false;
  try {
    (void)allocator.allocate(std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t) + 1);
  }
  catch (const std::bad_array_new_length&) {
    thrown = true;
  }
  catch (const std::bad_alloc&) {
    thrown = true;
  }
  assert(thrown);
}

static void check_asio_memory_sequences_iterate_and_mutate_blocks()
{
  auto first  = rstream::core::make_memory_allocated(3);
  auto second = rstream::core::make_memory_allocated(2);
  std::memcpy(first.get_data(), "abc", 3);
  std::memcpy(second.get_data(), "de", 2);

  rstream::core::buffer buffer;
  buffer.append(first);
  buffer.append(second);

  rstream::core::helpers::const_memory_sequence const_sequence(buffer);
  auto const_it = const_sequence.begin();
  assert(const_it != const_sequence.end());
  assert(boost::asio::buffer_size(*const_it) == 3);
  assert(boost::asio::buffer_size(*const_it.operator->()) == 3);
  auto const_post_increment = const_it++;
  assert(boost::asio::buffer_size(*const_post_increment) == 3);
  assert(boost::asio::buffer_size(*const_it) == 2);
  auto const_post_decrement = const_it--;
  assert(boost::asio::buffer_size(*const_post_decrement) == 2);
  assert(boost::asio::buffer_size(*const_it) == 3);
  ++const_it;
  --const_it;
  assert(boost::asio::buffer_size(*const_it) == 3);

  std::string joined;
  for (auto it = const_sequence.begin(); it != const_sequence.end(); ++it) {
    joined.append(static_cast<const char*>(it->data()), boost::asio::buffer_size(*it));
  }
  assert(joined == "abcde");

  rstream::core::helpers::mutable_memory_sequence mutable_sequence(buffer);
  auto mutable_it = mutable_sequence.begin();
  assert(mutable_it != mutable_sequence.end());
  assert(boost::asio::buffer_size(*mutable_it) == 3);
  static_cast<char*>(mutable_it->data())[0] = 'A';
  auto mutable_post_increment               = mutable_it++;
  assert(boost::asio::buffer_size(*mutable_post_increment) == 3);
  assert(boost::asio::buffer_size(*mutable_it) == 2);
  static_cast<char*>(mutable_it->data())[1] = 'E';
  auto mutable_post_decrement               = mutable_it--;
  assert(boost::asio::buffer_size(*mutable_post_decrement) == 2);
  assert(boost::asio::buffer_size(*mutable_it) == 3);
  ++mutable_it;
  --mutable_it;
  assert(boost::asio::buffer_size(*mutable_it) == 3);
  assert(to_string(buffer) == "AbcdE");
}

static void check_core_error_messages()
{
  auto code = rstream::core::error::make_error_code(rstream::core::error::code::invalid_size);
  assert(code.category() == rstream::core::error::rstream_core_error_category());
  assert(code.message() == "invalid size");
  assert(rstream::core::to_string(static_cast<rstream::core::error::code>(999)) == "unknown error");
}

static void run_check(const char* name, void (*callback)())
{
  try {
    callback();
  }
  catch (...) {
    std::cerr << "failed check: " << name << std::endl;
    throw;
  }
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  run_check("wrapped_memory_honors_offset", check_wrapped_memory_honors_offset);
  run_check("wrapped_memory_rejects_invalid_bounds", check_wrapped_memory_rejects_invalid_bounds);
  run_check("memory_copy_share_and_bounds", check_memory_copy_share_and_bounds);
  run_check("memory_shared_honors_allocator_and_null_state", check_memory_shared_honors_allocator_and_null_state);
  run_check("mutable_destroy_callback_is_preserved", check_mutable_destroy_callback_is_preserved);
  run_check("mutable_destroy_callback_cannot_escape_destruction", check_mutable_destroy_callback_cannot_escape_destruction);
  run_check("buffer_extract_fill_map_and_bounds", check_buffer_extract_fill_map_and_bounds);
  run_check("buffer_self_append_uses_original_snapshot", check_buffer_self_append_uses_original_snapshot);
  run_check("allocator_wrapper_rejects_overflowing_counts", check_allocator_wrapper_rejects_overflowing_counts);
  run_check("asio_memory_sequences_iterate_and_mutate_blocks", check_asio_memory_sequences_iterate_and_mutate_blocks);
  run_check("core_error_messages", check_core_error_messages);
  return 0;
}
