// See LICENSE file in the project root for license information.

#include <algorithm>
#include <array>
#include <cassert>
#include <string>

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

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_random_bytes_handles_empty_and_fills_buffer();
  check_random_str64_length_and_alphabet();
  return 0;
}
