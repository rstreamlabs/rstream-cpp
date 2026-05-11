// See LICENSE file in the project root for license information.

#include <cassert>
#include <stdexcept>
#include <string>

#include <boost/filesystem.hpp>

#include <rstream/io/geoip.hpp>

static void check_geoip_rejects_missing_database()
{
  auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("rstream-cpp-missing-geoip-%%%%-%%%%.mmdb");
  rstream::io::geoip::config config;
  config.m_database_location = path.string();
  try {
    rstream::io::geoip geoip(config);
    assert(false);
  }
  catch (const std::runtime_error& error) {
    assert(std::string(error.what()).size() > 0);
  }
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_geoip_rejects_missing_database();
  return 0;
}
