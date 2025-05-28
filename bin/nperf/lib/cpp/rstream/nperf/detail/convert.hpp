// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/nperf/nperf.hpp>
#include <rstream/nperf/protobuf/messages.pb.h>

namespace rstream {
namespace nperf {
namespace detail {

void convert(unsigned int& dst, const rstream::nperf::protobuf::Option& src);
void convert(rstream::nperf::protobuf::Option& dst, unsigned int src);

}  // namespace detail
}  // namespace nperf
}  // namespace rstream
