// See LICENSE file in the project root for license information.

#include "protobuf.hpp"

#include <google/protobuf/stubs/common.h>
#include <google/protobuf/util/json_util.h>

namespace rstream {
namespace core {
namespace helpers {

std::string to_json_string(const google::protobuf::Message& message)
{
  std::string str;
  #if GOOGLE_PROTOBUF_VERSION < 3030000
  google::protobuf::util::JsonOptions options;
#else
  google::protobuf::util::JsonPrintOptions options;
#endif
  options.add_whitespace = true;
#if GOOGLE_PROTOBUF_VERSION < 3026000
  options.always_print_primitive_fields = true;
#else
  options.always_print_fields_with_no_presence = true;
#endif
  auto status = google::protobuf::util::MessageToJsonString(message, &str, options);
  if (!status.ok()) {
    std::string message;
#if GOOGLE_PROTOBUF_VERSION < 3016000
    message = status.error_message();
#else
    message = status.message().data();
#endif
    str = std::string("cannot serialize protobuf message: " + message);
  }
  if (str.length() > 0) {
    std::string::iterator it = str.end() - 1;
    if (*it == '\n') {
      str.erase(it);
    }
  }
  return str;
}

}  // namespace helpers
}  // namespace core
}  // namespace rstream
