// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <sstream>

#include <boost/system/system_error.hpp>

#include <nlohmann/json.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/io/error.hpp>

namespace rstream {
namespace stun {
namespace helpers {

template <typename T>
void parse_value(T& dst, const rstream::core::memory memory, std::size_t& offset)
{
  auto diff = sizeof(T);
  auto data = &((const std::uint8_t*)memory.get_const_data())[offset];
  if ((offset + diff) > memory.get_size()) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "data has invalid size");
  }
  auto value = *((const T*)data);
  offset += diff;
  dst = value;
}

template <typename T>
std::size_t byte_size_long_value(const T& value)
{
  (void)value;
  return sizeof(T);
}

template <typename T>
void serialize_value(void* dst, const T& src, std::size_t& offset)
{
  auto diff = sizeof(T);
  auto data = &((std::uint8_t*)dst)[offset];
  memcpy(data, &src, sizeof(T));
  offset += diff;
}

template <typename T>
void serialize_json_value(nlohmann::json& json, const T& src)
{
  (void)json;
  (void)src;
  throw std::runtime_error("unimplemented method");
}

class basic_message {
 public:
  virtual std::size_t byte_size_long() const                   = 0;
  virtual void serialize(void* dst, std::size_t& offset) const = 0;
  virtual void serialize_json(nlohmann::json& json) const      = 0;
  void serialize(void* dst) const;
  rstream::core::memory serialize_to_memory() const;
  rstream::core::buffer serialize_to_buffer() const;
};

nlohmann::json& operator<<(nlohmann::json& json, const basic_message& message);

template <class T>
class message_base : public basic_message {
 public:
  T& parse(const rstream::core::memory memory, std::size_t offset)
  {
    std::size_t tmp = offset;
    parse_value<T>(get(), memory, tmp);
    if (tmp != memory.get_size()) {
      std::stringstream stringstream;
      stringstream << "data has invalid size [expected: " << (memory.get_size() - offset) << ", current: " << (tmp - offset) << "]";
      throw rstream::core::system_error(rstream::io::error::code::deserialization_error, stringstream.str());
    }
    return get();
  }
  T& parse(const rstream::core::memory memory)
  {
    return parse(memory, 0);
  }
  T& parse(rstream::core::buffer buffer)
  {
    return parse(buffer.map(rstream::core::buffer::map_mode::read));
  }
  std::size_t byte_size_long() const override
  {
    return byte_size_long_value<T>(get());
  }
  void serialize(void* dst, std::size_t& offset) const override
  {
    serialize_value<T>(dst, get(), offset);
  }
  void serialize_json(nlohmann::json& json) const override
  {
    serialize_json_value<T>(json, get());
  }

 private:
  T& get()
  {
    return dynamic_cast<T&>(*this);
  }
  const T& get() const
  {
    return dynamic_cast<const T&>(*this);
  }
};

template <>
std::size_t byte_size_long_value<std::string>(const std::string& value);
template <>
std::size_t byte_size_long_value<rstream::core::memory>(const rstream::core::memory& value);
template <>
void serialize_value<std::string>(void* dst, const std::string& src, std::size_t& offset);
template <>
void serialize_value<rstream::core::memory>(void* dst, const rstream::core::memory& src, std::size_t& offset);
template <>
void parse_value<std::string>(std::string& dst, const rstream::core::memory memory, std::size_t& offset);

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
