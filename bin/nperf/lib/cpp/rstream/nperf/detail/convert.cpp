// See LICENSE file in the project root for license information.

#include "convert.hpp"

namespace rstream {
namespace nperf {
namespace detail {

void convert(unsigned int& dst, const rstream::nperf::protobuf::Option& src)
{
  if (src == rstream::nperf::protobuf::Option::OPTION_PING) {
    dst |= option::ping;
  }
  else if (src == rstream::nperf::protobuf::Option::OPTION_DOWNLOAD) {
    dst |= option::download;
  }
  else if (src == rstream::nperf::protobuf::Option::OPTION_UPLOAD) {
    dst |= option::upload;
  }
}

void convert(rstream::nperf::protobuf::Option& dst, unsigned int src)
{
  if (src & option::ping) {
    dst = rstream::nperf::protobuf::Option::OPTION_PING;
  }
  else if ((src & option::download)) {
    dst = rstream::nperf::protobuf::Option::OPTION_DOWNLOAD;
  }
  else if ((src & option::upload)) {
    dst = rstream::nperf::protobuf::Option::OPTION_UPLOAD;
  }
}

}  // namespace detail
}  // namespace nperf
}  // namespace rstream
