// See LICENSE file in the project root for license information.

#pragma once

#include <cstdlib>
#include <string>

namespace rstream {
namespace core {

void random_bytes(void* data, std::size_t size);

std::string random_str64(std::size_t size);

}  // namespace core
}  // namespace rstream
