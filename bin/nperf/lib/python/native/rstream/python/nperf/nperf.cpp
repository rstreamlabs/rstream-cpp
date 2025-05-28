// See LICENSE file in the project root for license information.

#include "nperf.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace rstream {
namespace python {
namespace nperf {

std::string to_string(const rstream::nperf::timestamp& timestamp, const std::string& format)
{
  std::time_t tt = std::chrono::system_clock::to_time_t(timestamp);
  std::tm tm     = *std::gmtime(&tt);
  std::stringstream ss;
  ss << std::put_time(&tm, format.c_str());
  return ss.str();
}

}  // namespace nperf
}  // namespace python
}  // namespace rstream
