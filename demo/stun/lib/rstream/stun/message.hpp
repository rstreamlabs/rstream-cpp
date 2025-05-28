// See LICENSE file in the project root for license information.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <sstream>
#include <string>

#include <boost/any.hpp>
#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <boost/optional.hpp>
#include <boost/system/system_error.hpp>

#include <nlohmann/json.hpp>
#include <string.h>

#include <rstream/core/buffer.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/io/error.hpp>
#include <rstream/stun/helpers/message.hpp>

#include "error.hpp"

#define STUN_TRANSACTION_ID_SIZE 12

namespace rstream {
namespace stun {

using msg_type           = std::uint16_t;
using msg_magic          = std::uint32_t;
using msg_transaction_id = std::array<std::uint8_t, STUN_TRANSACTION_ID_SIZE>;

std::string to_string(msg_transaction_id msg_transaction_id);

msg_transaction_id random_msg_transaction_id();

enum class stun_class {
  request,
  indication,
  response_success,
  response_error
};

stun_class parse_stun_class(msg_type msg_type);
std::string to_string(stun_class stun_class);

enum class stun_method {
  binding
};

stun_method parse_stun_method(msg_type msg_type);
std::string to_string(stun_method stun_method);

msg_type encode_msg_type(stun_class stun_class, stun_method stun_method);

class header : public helpers::message_base<header> {
 public:
  header() = default;
  msg_type& get_type();
  const msg_type& get_type() const;
  std::uint16_t& get_payload_length();
  const std::uint16_t& get_payload_length() const;
  msg_magic& get_magic();
  const msg_magic& get_magic() const;
  msg_transaction_id& get_transaction_id();
  const msg_transaction_id& get_transaction_id() const;
  stun_class get_stun_class() const;
  stun_method get_stun_method() const;
  bool is_response() const;

 private:
  msg_type m_type;
  std::uint16_t m_payload_length;
  msg_magic m_magic;
  msg_transaction_id m_transaction_id;
};

class attribute_header : public helpers::message_base<attribute_header> {
 public:
  attribute_header() = default;
  msg_type& get_type();
  const msg_type& get_type() const;
  std::uint16_t& get_length();
  const std::uint16_t& get_length() const;

 private:
  msg_type m_type;
  std::uint16_t m_length;
};

class attribute : public helpers::message_base<attribute> {
 public:
  enum class data_type {
    undefined,
    serialized,
    parsed
  };
  struct data_serialized {
    const rstream::core::memory get() const;
    rstream::core::memory m_data;
    std::size_t m_offset;
    std::size_t m_size;
  };
  using data_parsed = helpers::basic_message;
  attribute();
  data_type get_data_type() const;
  attribute_header& get_header();
  const attribute_header& get_header() const;
  const data_serialized& get_data_serialized() const;
  const data_parsed& get_data_parsed() const;
  static attribute make(const rstream::core::memory& data, std::size_t& offset);
  template <class T>
  static attribute make(const T& value);
  template <class T>
  T get() const
  {
    if (m_data_type == data_type::undefined) {
      throw std::runtime_error("attribute is undefined");
    }
    else if (m_data_type == data_type::serialized) {
      return parse<T>(get_data_serialized());
    }
    else {
      return *std::static_pointer_cast<T>(m_data);
    }
  }
  template <class T>
  static T parse(const data_serialized& data)
  {
    auto offset = data.m_offset;
    auto memory = data.m_data;
    memory.resize(0, offset + data.m_size);
    return T().parse(memory, offset);
  }

 private:
  data_type m_data_type;
  attribute_header m_header;
  std::shared_ptr<void> m_data;
};

attribute make_attribute(const rstream::core::memory& data, std::size_t& offset);

using attribute_serialize_json_func = std::function<void(nlohmann::json&, const attribute::data_serialized&)>;

template <class T>
attribute_serialize_json_func make_attribute_serialize_json_func()
{
  return [](nlohmann::json& json, const attribute::data_serialized& data) { attribute::parse<T>(data).serialize_json(json); };
}

struct attribute_property {
  msg_type m_msg_type;
  std::size_t m_hash_code;
  std::string m_name;
  attribute_serialize_json_func m_attribute_serialize_json_func;
};

template <class T>
static attribute make_attribute(const T& value)
{
  return attribute::make(value);
}

template <class T>
using container  = std::list<T>;
using attributes = container<attribute>;

class message : public helpers::message_base<message> {
 public:
  message() = default;
  header& get_header();
  const header& get_header() const;
  attributes& get_attributes();
  const attributes& get_attributes() const;
  void check_fingerprint() const;
  void check_message_integrity(const std::string& password) const;
  bool has_integrity() const;
  std::uint32_t get_priority() const;
  std::string to_string() const;

 private:
  header m_header;
  attributes m_attributes;
};

attributes::const_iterator find_attribute(const message& message, msg_type attribute_msg_type);
attribute get_attribute(const message& message, msg_type attribute_msg_type);

void is_stun_datagram(const rstream::core::memory memory, std::exception_ptr& error);
bool is_stun_datagram(const rstream::core::memory memory);
void is_stun_datagram(rstream::core::buffer buffer, std::exception_ptr& error);
bool is_stun_datagram(rstream::core::buffer buffer);

bool has_attribute(const message& message, msg_type attribute_msg_type);

class message_builder {
 public:
  message_builder(msg_type msg_type, const msg_transaction_id& transaction_id);
  message_builder(stun_class stun_class, stun_method stun_method, const msg_transaction_id& transaction_id);
  message_builder(msg_type msg_type);
  message_builder(stun_class stun_class, stun_method stun_method);
  attributes& get_attributes();
  const attributes& get_attributes() const;
  const message& build();
  template <class T>
  void add_attribute(const T& value);
  void add_software();
  void add_message_integrity(const std::string& password);
  void add_fingerprint();

 public:
  message m_message;
};

}  // namespace stun
}  // namespace rstream

namespace rstream {
namespace stun {
namespace helpers {

template <>
std::size_t byte_size_long_value<header>(const header& value);
template <>
std::size_t byte_size_long_value<attribute_header>(const attribute_header& value);
template <>
std::size_t byte_size_long_value<attribute>(const attribute& value);
template <>
std::size_t byte_size_long_value<attributes>(const attributes& value);
template <>
std::size_t byte_size_long_value<message>(const message& value);

template <>
void serialize_value<header>(void* dst, const header& src, std::size_t& offset);
template <>
void serialize_value<attribute_header>(void* dst, const attribute_header& src, std::size_t& offset);
template <>
void serialize_value<attribute>(void* dst, const attribute& src, std::size_t& offset);
template <>
void serialize_value<attributes>(void* dst, const attributes& src, std::size_t& offset);
template <>
void serialize_value<message>(void* dst, const message& src, std::size_t& offset);

template <>
void parse_value<header>(header& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_header>(attribute_header& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute>(attribute& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attributes>(attributes& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<message>(message& dst, const rstream::core::memory memory, std::size_t& offset);

template <>
void serialize_json_value<header>(nlohmann::json& json, const header& value);
template <>
void serialize_json_value<attribute_header>(nlohmann::json& json, const attribute_header& value);
template <>
void serialize_json_value<attribute>(nlohmann::json& json, const attribute& value);
template <>
void serialize_json_value<attributes>(nlohmann::json& json, const attributes& value);
template <>
void serialize_json_value<message>(nlohmann::json& json, const message& value);

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
