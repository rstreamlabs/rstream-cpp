// See LICENSE file in the project root for license information.

#include <iostream>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/geoip.hpp>

static const char USAGE[] = R"(
rstream-example-io-geoip

usage:
    rstream-example-io-geoip <database> <ip> [-v]
    rstream-example-io-geoip (-h|--help)
    rstream-example-io-geoip --version

example:
    rstream-example-io-geoip /var/lib/GeoIP/GeoLite2-Country.mmdb 8.8.8.8

options:
    -h --help       show this screen
    --version       show version
    -v --verbose    enable verbose mode
)";

const auto version = std::string("rstream-example-io-geoip ") + RSTREAM_VERSION;

int main(int argc, char** argv)
{
  auto args    = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  bool verbose = false;
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      verbose = true;
    }
  }
  if (verbose) {
    rstream::core::log::enable_ansicolor_stdout_mt();
  }
  rstream::io::geoip::config config = {.m_database_location = args.at("<database>").asString()};
  rstream::io::geoip geoip(config);
  std::cout << geoip.lookup(boost::asio::ip::make_address(args.at("<ip>").asString())).m_country_iso_code << std::endl;
  return 0;
}
