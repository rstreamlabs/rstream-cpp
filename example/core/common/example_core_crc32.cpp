// See LICENSE file in the project root for license information.

#include <iostream>

#include <rstream/core/crc32.hpp>

int main()
{
  std::string str("this is a string");
  std::cout << std::hex << std::uppercase << rstream::core::crc32(str.c_str(), str.length()) << std::endl;
  return 0;
}
