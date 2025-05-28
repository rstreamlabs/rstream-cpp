// See LICENSE file in the project root for license information.

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>

static const rstream::core::logger g_logger({"rstream", "example", "log"});

int main(int argc, char** argv)
{
  rstream::core::log::enable_ansicolor_stdout_mt();
  g_logger->trace("welcome to rstream {}", RSTREAM_VERSION);
  g_logger->debug("welcome to rstream {}", RSTREAM_VERSION);
  g_logger->info("welcome to rstream {}", RSTREAM_VERSION);
  g_logger->warn("welcome to rstream {}", RSTREAM_VERSION);
  g_logger->error("welcome to rstream {}", RSTREAM_VERSION);
  g_logger->critical("welcome to rstream {}", RSTREAM_VERSION);
  rstream::core::default_logger()->info("welcome to rstream {}", RSTREAM_VERSION);
  return 0;
}
