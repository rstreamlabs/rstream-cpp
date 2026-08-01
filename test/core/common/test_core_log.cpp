// See LICENSE file in the project root for license information.

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include <rstream/core/log.hpp>

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  std::ostringstream output;
  auto* original_buffer = std::cout.rdbuf(output.rdbuf());
  auto sink             = rstream::core::log::enable_ansicolor_stdout_mt(false);
  rstream::core::log::logger logger("test");
  logger->warn("formatted message");
  std::cout.rdbuf(original_buffer);
  assert(output.str().find("WARN") != std::string::npos);
  assert(output.str().find("formatted message") != std::string::npos);
  return 0;
}
