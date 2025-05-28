// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <cstdlib>

namespace rstream {
namespace core {

std::uint32_t crc32(const void* data, std::size_t size);

}
}  // namespace rstream
