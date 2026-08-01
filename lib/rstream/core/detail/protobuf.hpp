// See LICENSE file in the project root for license information.

#pragma once

#include <limits>
#include <utility>

#include <google/protobuf/message.h>

#include <rstream/core/buffer.hpp>

namespace rstream {
namespace core {
namespace detail {

inline bool is_protobuf_buffer_size_valid(std::size_t size) noexcept
{
  return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename Message>
inline bool parse_protobuf_message(Message& message, const void* data, std::size_t size)
{
  return is_protobuf_buffer_size_valid(size)
         && message.ParseFromArray(data, static_cast<int>(size));
}

inline bool serialize_protobuf_message(const google::protobuf::Message& message, buffer& output, allocator::ptr allocator = nullptr)
{
  if (!message.IsInitialized()) {
    return false;
  }
  const auto size = message.ByteSizeLong();
  if (!is_protobuf_buffer_size_valid(size)) {
    return false;
  }
  auto serialized = make_buffer_allocated(size, allocator);
  if (!message.SerializeToArray(serialized.map().get_data(), static_cast<int>(size))) {
    return false;
  }
  output = std::move(serialized);
  return true;
}

}  // namespace detail
}  // namespace core
}  // namespace rstream
