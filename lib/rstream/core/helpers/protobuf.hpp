
// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <google/protobuf/message.h>

namespace rstream {
namespace core {
namespace helpers {

std::string to_json_string(const google::protobuf::Message& message);

}
}  // namespace core
}  // namespace rstream
