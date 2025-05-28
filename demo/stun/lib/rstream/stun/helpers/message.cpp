// See LICENSE file in the project root for license information.

#include "message.hpp"

namespace rstream {
namespace stun {
namespace helpers {

void basic_message::serialize(void* dst) const
{
  std::size_t offset = 0;
  serialize(dst, offset);
}

rstream::core::memory basic_message::serialize_to_memory() const
{
  auto memory = rstream::core::make_memory_allocated(byte_size_long());
  serialize(memory.get_data());
  return memory;
}

rstream::core::buffer basic_message::serialize_to_buffer() const
{
  rstream::core::buffer buffer;
  auto memory = serialize_to_memory();
  buffer.append(memory);
  return buffer;
}

nlohmann::json& operator<<(nlohmann::json& json, const basic_message& message)
{
  message.serialize_json(json);
  return json;
}

template <>
std::size_t byte_size_long_value<std::string>(const std::string& value)
{
  return value.length();
}

template <>
std::size_t byte_size_long_value<rstream::core::memory>(const rstream::core::memory& value)
{
  return value.get_size();
}

template <>
void serialize_value<std::string>(void* dst, const std::string& src, std::size_t& offset)
{
  auto diff = src.length();
  auto data = &((std::uint8_t*)dst)[offset];
  ::memcpy(data, src.data(), diff);
  offset += diff;
}

template <>
void serialize_value<rstream::core::memory>(void* dst, const rstream::core::memory& src, std::size_t& offset)
{
  auto diff = src.get_size();
  auto data = &((std::uint8_t*)dst)[offset];
  ::memcpy(data, src.get_const_data(), diff);
  offset += diff;
}

template <>
void parse_value<std::string>(std::string& dst, const rstream::core::memory memory, std::size_t& offset)
{
  dst = std::string(&((const char*)memory.get_const_data())[offset], std::max((std::size_t)0, memory.get_size() - offset));
  offset += dst.length();
}

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
