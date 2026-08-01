// See LICENSE file in the project root for license information.

#include "random.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <functional>
#include <random>
#include <thread>

namespace rstream {
namespace core {

namespace {

std::mt19937 make_random_engine()
{
  static std::atomic<std::uint64_t> sequence = 0;
  std::random_device device;
  const auto now          = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto thread_id    = std::hash<std::thread::id>{}(std::this_thread::get_id());
  const auto thread_value = static_cast<std::uint64_t>(thread_id);
  const auto instance     = sequence.fetch_add(1, std::memory_order_relaxed);
  std::seed_seq seed{
      device(),
      device(),
      static_cast<std::uint32_t>(now),
      static_cast<std::uint32_t>(static_cast<std::uint64_t>(now) >> 32),
      static_cast<std::uint32_t>(thread_value),
      static_cast<std::uint32_t>(thread_value >> 32),
      static_cast<std::uint32_t>(instance),
      static_cast<std::uint32_t>(instance >> 32)};
  return std::mt19937(seed);
}

std::mt19937& random_engine()
{
  thread_local std::mt19937 engine = make_random_engine();
  return engine;
}

}  // namespace

void random_bytes(void* data, std::size_t size)
{
  std::uniform_int_distribution<unsigned int> distribution(0, UCHAR_MAX);
  for (std::size_t i = 0; i < size; ++i) {
    static_cast<unsigned char*>(data)[i] = static_cast<unsigned char>(distribution(random_engine()));
  }
}

std::string random_str64(std::size_t size)
{
  static char constexpr chars[] = {
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
  std::uniform_int_distribution<> distribution(0, sizeof(chars) - 2);
  auto randchar = [&]() {
    return chars[distribution(random_engine())];
  };
  std::string str(size, 0);
  std::generate(str.begin(), str.end(), randchar);
  return str;
}

}  // namespace core
}  // namespace rstream
