// See LICENSE file in the project root for license information.

#include "random.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <functional>
#include <random>

namespace rstream {
namespace core {

void random_bytes(void* data, std::size_t size)
{
  using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned int>;
  auto seed                 = std::chrono::system_clock::now().time_since_epoch().count();
  random_bytes_engine engine(seed);
  for (std::size_t i = 0; i < size; ++i) {
    ((unsigned char*)data)[i] = static_cast<unsigned char>(engine());
  }
}

std::string random_str64(std::size_t size)
{
  static char constexpr chars[] = {
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
  std::random_device device;
  std::uniform_int_distribution<> distribution(0, sizeof(chars) - 2);
  auto randchar = [&]() {
    return chars[distribution(device)];
  };
  std::string str(size, 0);
  std::generate(str.begin(), str.end(), randchar);
  return str;
}

}  // namespace core
}  // namespace rstream
