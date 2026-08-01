// See LICENSE file in the project root for license information.

#include <algorithm>
#include <array>
#include <cassert>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <rstream/core/random.hpp>

static void check_random_bytes_handles_empty_and_fills_buffer()
{
  rstream::core::random_bytes(nullptr, 0);

  std::array<unsigned char, 32> buffer{};
  rstream::core::random_bytes(buffer.data(), buffer.size());
  assert(std::any_of(buffer.begin(), buffer.end(), [](unsigned char value) {
    return value != 0;
  }));
}

static void check_random_str64_length_and_alphabet()
{
  auto value = rstream::core::random_str64(128);
  assert(value.size() == 128);
  const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  assert(std::all_of(value.begin(), value.end(), [&](char ch) {
    return alphabet.find(ch) != std::string::npos;
  }));
  assert(rstream::core::random_str64(0).empty());
}

static void check_random_generation_is_thread_safe()
{
  constexpr std::size_t thread_count = 16;
  std::vector<std::string> values(thread_count);
  std::vector<std::array<unsigned char, 32>> buffers(thread_count);
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t index = 0; index < thread_count; ++index) {
    threads.emplace_back([index, &values, &buffers]() {
      values[index] = rstream::core::random_str64(64);
      rstream::core::random_bytes(buffers[index].data(), buffers[index].size());
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  const std::set<std::string> unique_values(values.begin(), values.end());
  assert(unique_values.size() == thread_count);
  for (std::size_t index = 0; index < thread_count; ++index) {
    assert(values[index].size() == 64);
    assert(std::any_of(buffers[index].begin(), buffers[index].end(), [](unsigned char value) {
      return value != 0;
    }));
  }
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_random_bytes_handles_empty_and_fills_buffer();
  check_random_str64_length_and_alphabet();
  check_random_generation_is_thread_safe();
  return 0;
}
