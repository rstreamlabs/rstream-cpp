// See LICENSE file in the project root for license information.

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/plugin.hpp>

int main()
{
  rstream::core::log::enable_ansicolor_stdout_mt();
  rstream::core::default_logger()->info("welcome to rstream ({})", RSTREAM_VERSION);
  for (std::size_t iteration = 0; iteration < 32; ++iteration) {
    rstream::core::plugin::element::ptr element;
    {
      rstream::core::plugin::factory factory(rstream::core::plugin::factory::default_config());
      boost::system::error_code error_code;
      element = factory.create("io.stream.tcp", error_code);
      if (error_code || !element) {
        rstream::core::default_logger()->error("TCP plugin is unavailable: {}", error_code.message());
        return 1;
      }
    }
    element.reset();
  }
  return 0;
}
