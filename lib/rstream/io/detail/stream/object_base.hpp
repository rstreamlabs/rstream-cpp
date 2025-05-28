// See LICENSE file in the project root for license information.

#pragma once

#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class object_base {
 public:
  virtual ~object_base() = default;
};

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
