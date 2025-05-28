// See LICENSE file in the project root for license information.

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>

int main(int argc, char** argv)
{
  rstream::core::log::enable_ansicolor_stdout_mt();
  rstream::core::default_logger()->info("welcome to rstream ({})", RSTREAM_VERSION);
  return 0;
}
