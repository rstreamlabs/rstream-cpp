// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <list>
#include <string>

#include <boost/any.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>

#include <rstream/stun/helpers/message.hpp>

#include "message.hpp"

namespace rstream {
namespace stun {

enum class attribute_type {

  // RFC8489 (Session Traversal Utilities for NAT (STUN))

  mapped_address,
  xor_mapped_address,
  username,
  message_integrity,
  fingerprint,
  error_code,
  realm,
  nonce,
  unknown_attributes,
  software,
  alternate_server,

  // RFC8445 (Interactive Connectivity Establishment (ICE))

  priority,
  use_candidate,
  ice_controlled,
  ice_controlling,

  // RFC5780 (NAT Behavior Discovery Using Session Traversal Utilities for NAT (STUN))

  change_request,
  response_port,
  padding,
  cache_timeout,
  response_origin,
  other_address
};

enum class error_code {

  undefined = 0,

  // RFC8489 (Session Traversal Utilities for NAT (STUN))

  try_alternate     = 300,
  bad_request       = 400,
  unauthenticated   = 401,
  unknown_attribute = 420,
  stale_nonce       = 438,
  server_error      = 500,

  // RFC8445 (Interactive Connectivity Establishment (ICE))

  role_conflict = 487,

  // RFC8656 (Traversal Using Relays around NAT (TURN))

  forbidden                      = 403,
  allocation_mismatch            = 437,
  address_family_not_supported   = 440,
  wrong_credentials              = 441,
  unsupported_transport_protocol = 442,
  peer_address_family_mismatch   = 443,
  allocation_quota_reached       = 486,
  insufficient_capacity          = 508
};

std::string to_string(error_code error_code);

template <class T>
std::size_t get_hash_code()
{
  return typeid(T).hash_code();
}

msg_type get_attribute_msg_type(attribute_type type);
attribute_type get_attribute_type(msg_type type);
attribute_type get_attribute_type(std::size_t hash_code);

template <class T>
attribute_type get_attribute_type()
{
  return get_attribute_type(get_hash_code<T>());
}

template <class T>
class attribute_value : public helpers::message_base<T> {
 public:
  attribute_type get_attribute_type() const
  {
    return get_attribute_type<T>();
  }
};

template <class T>
attribute attribute::make(const T& value)
{
  class attribute attribute;
  attribute.m_data_type           = data_type::parsed;
  attribute.m_header.get_type()   = get_attribute_msg_type(get_attribute_type<T>());
  attribute.m_header.get_length() = helpers::byte_size_long_value(value);
  attribute.m_data                = std::make_shared<T>(value);
  return attribute;
}

template <class T>
bool has_attribute(const message& message)
{
  return has_attribute(message, get_attribute_msg_type(get_attribute_type<T>()));
}

template <class T>
T get_attribute(const message& message)
{
  return get_attribute(message, get_attribute_msg_type(get_attribute_type<T>())).template get<T>();
}

template <class T>
void add_attribute(message& message, const T& value)
{
  message.get_attributes().push_back(make_attribute(value));
}

template <class T>
void message_builder::add_attribute(const T& value)
{
  return stun::add_attribute(m_message, value);
}

enum class stun_address_family {
  IPV4,
  IPV6
};

class attribute_value_mapped_address_base {
 public:
  attribute_value_mapped_address_base() = default;
  std::uint8_t& get_padding();
  const std::uint8_t& get_padding() const;
  stun_address_family& get_family();
  const stun_address_family& get_family() const;
  std::uint16_t& get_port();
  const std::uint16_t& get_port() const;
  rstream::core::memory& get_address_memory();
  const rstream::core::memory& get_address_memory() const;
  boost::asio::ip::address get_address() const;
  void set_address(const boost::asio::ip::address& address);

 private:
  std::uint8_t m_padding;
  stun_address_family m_family;
  std::uint16_t m_port;
  rstream::core::memory m_address;
};

class attribute_value_mapped_address : public attribute_value_mapped_address_base, public attribute_value<attribute_value_mapped_address> {
 public:
  attribute_value_mapped_address() = default;
};

class attribute_value_xor_mapped_address : public attribute_value_mapped_address_base, public attribute_value<attribute_value_xor_mapped_address> {
 public:
  using mask_type                      = std::array<std::uint8_t, 16>;
  attribute_value_xor_mapped_address() = default;
  void apply_mask(const mask_type& mask);
  static void read_mask(const void* data, mask_type& mask);
};

class attribute_value_username : public attribute_value<attribute_value_username> {
 public:
  attribute_value_username() = default;
  std::string& get_value();
  const std::string& get_value() const;
  std::pair<std::string, std::string> parse_string(char separator = ':') const;

 private:
  std::string m_value;
};

class attribute_value_message_integrity : public attribute_value<attribute_value_message_integrity> {
 public:
  enum class data_type {
    undefined = 0,
    input,
    output
  };
  struct input_data {
    std::string m_password;
  };
  struct output_data {
    using array_type = std::array<std::uint8_t, 20>;
    array_type m_serialized;
  };
  attribute_value_message_integrity();
  data_type get_data_type() const;
  input_data& get_input_data();
  const input_data& get_input_data() const;
  output_data& get_output_data();
  const output_data& get_output_data() const;
  static std::string compute_long_term_credentials(const std::string& username, const std::string& realm, const std::string& password);
  static void hmac_sha1(const void* data, std::size_t size, const std::string& password, void* dst);
  static std::string to_string(const output_data::array_type& data);

 private:
  data_type m_data_type;
  boost::any m_data;
};

class attribute_value_fingerprint : public attribute_value<attribute_value_fingerprint> {
 public:
  enum class state {
    unprocessed,
    processed
  };
  attribute_value_fingerprint() = default;
  state get_state() const;
  std::uint32_t& get_value();
  const std::uint32_t& get_value() const;
  static std::uint32_t crc32(const void* data, std::size_t size);

 private:
  boost::optional<std::uint32_t> m_value;
};

class attribute_value_error_code : public attribute_value<attribute_value_error_code> {
 public:
  attribute_value_error_code() = default;
  std::uint8_t& get_code_class();
  const std::uint8_t& get_code_class() const;
  std::uint8_t& get_code_number();
  const std::uint8_t& get_code_number() const;
  std::string& get_reason();
  const std::string& get_reason() const;
  error_code get_error_code() const;
  void set_error_code(error_code error_code);

 private:
  std::uint8_t m_code_class;
  std::uint8_t m_code_number;
  std::string m_reason;
};

attribute_value_error_code make_error_code(error_code error_code);
attribute_value_error_code make_error_code(error_code error_code, const std::string& reason);

class attribute_value_realm : public attribute_value<attribute_value_realm> {
 public:
  attribute_value_realm() = default;
  std::string& get_value();
  const std::string& get_value() const;

 private:
  std::string m_value;
};

class attribute_value_nonce : public attribute_value<attribute_value_nonce> {
 public:
  attribute_value_nonce() = default;
  std::string& get_value();
  const std::string& get_value() const;

 private:
  std::string m_value;
};

class attribute_value_unknown_attributes : public attribute_value<attribute_value_unknown_attributes> {
 public:
  attribute_value_unknown_attributes() = default;
  std::list<msg_type>& get_value();
  const std::list<msg_type>& get_value() const;

 private:
  std::list<msg_type> m_value;
};

class attribute_value_software : public attribute_value<attribute_value_software> {
 public:
  attribute_value_software() = default;
  std::string& get_value();
  const std::string& get_value() const;

 private:
  std::string m_value;
};

class attribute_value_alternate_server : public attribute_value_mapped_address_base, public attribute_value<attribute_value_alternate_server> {
 public:
  attribute_value_alternate_server() = default;
};

class attribute_value_priority : public attribute_value<attribute_value_priority> {
 public:
  attribute_value_priority() = default;
  std::uint32_t& get_value();
  const std::uint32_t& get_value() const;

 private:
  std::uint32_t m_value;
};

class attribute_value_use_candidate : public attribute_value<attribute_value_use_candidate> {
 public:
  attribute_value_use_candidate() = default;
};

class attribute_value_ice_controlled : public attribute_value<attribute_value_ice_controlled> {
 public:
  attribute_value_ice_controlled() = default;
  std::uint64_t& get_value();
  const std::uint64_t& get_value() const;

 private:
  std::uint64_t m_value;
};

class attribute_value_ice_controlling : public attribute_value<attribute_value_ice_controlling> {
 public:
  attribute_value_ice_controlling() = default;
  std::uint64_t& get_value();
  const std::uint64_t& get_value() const;

 private:
  std::uint64_t m_value;
};

class attribute_value_change_request : public attribute_value<attribute_value_change_request> {
 public:
  enum flags {
    change_ip   = 2,
    change_port = 4
  };
  attribute_value_change_request() = default;
  std::uint32_t& get_value();
  const std::uint32_t& get_value() const;

 private:
  std::uint32_t m_value;
};

class attribute_value_response_port : public attribute_value<attribute_value_response_port> {
 public:
  attribute_value_response_port() = default;
  std::uint16_t& get_value();
  const std::uint16_t& get_value() const;

 private:
  std::uint16_t m_value;
};

class attribute_value_padding : public attribute_value<attribute_value_padding> {
 public:
  attribute_value_padding() = default;
  std::string& get_value();
  const std::string& get_value() const;

 private:
  std::string m_value;
};

class attribute_value_cache_timeout : public attribute_value<attribute_value_cache_timeout> {
 public:
  attribute_value_cache_timeout() = default;

 private:
};

class attribute_value_response_origin : public attribute_value_mapped_address_base, public attribute_value<attribute_value_response_origin> {
 public:
  attribute_value_response_origin() = default;
};

class attribute_value_other_address : public attribute_value_mapped_address_base, public attribute_value<attribute_value_other_address> {
 public:
  attribute_value_other_address() = default;
};

}  // namespace stun
}  // namespace rstream

namespace rstream {
namespace stun {
namespace helpers {

template <>
std::size_t byte_size_long_value<stun_address_family>(const stun_address_family& value);
template <>
std::size_t byte_size_long_value<attribute_value_mapped_address_base>(const attribute_value_mapped_address_base& value);
template <>
std::size_t byte_size_long_value<attribute_value_mapped_address>(const attribute_value_mapped_address& value);
template <>
std::size_t byte_size_long_value<attribute_value_xor_mapped_address>(const attribute_value_xor_mapped_address& value);
template <>
std::size_t byte_size_long_value<attribute_value_username>(const attribute_value_username& value);
template <>
std::size_t byte_size_long_value<attribute_value_message_integrity>(const attribute_value_message_integrity& value);
template <>
std::size_t byte_size_long_value<attribute_value_fingerprint>(const attribute_value_fingerprint& value);
template <>
std::size_t byte_size_long_value<attribute_value_error_code>(const attribute_value_error_code& value);
template <>
std::size_t byte_size_long_value<attribute_value_realm>(const attribute_value_realm& value);
template <>
std::size_t byte_size_long_value<attribute_value_nonce>(const attribute_value_nonce& value);
template <>
std::size_t byte_size_long_value<attribute_value_unknown_attributes>(const attribute_value_unknown_attributes& value);
template <>
std::size_t byte_size_long_value<attribute_value_software>(const attribute_value_software& value);
template <>
std::size_t byte_size_long_value<attribute_value_alternate_server>(const attribute_value_alternate_server& value);
template <>
std::size_t byte_size_long_value<attribute_value_priority>(const attribute_value_priority& value);
template <>
std::size_t byte_size_long_value<attribute_value_use_candidate>(const attribute_value_use_candidate& value);
template <>
std::size_t byte_size_long_value<attribute_value_ice_controlled>(const attribute_value_ice_controlled& value);
template <>
std::size_t byte_size_long_value<attribute_value_ice_controlling>(const attribute_value_ice_controlling& value);
template <>
std::size_t byte_size_long_value<attribute_value_change_request>(const attribute_value_change_request& value);
template <>
std::size_t byte_size_long_value<attribute_value_response_port>(const attribute_value_response_port& value);
template <>
std::size_t byte_size_long_value<attribute_value_padding>(const attribute_value_padding& value);
template <>
std::size_t byte_size_long_value<attribute_value_cache_timeout>(const attribute_value_cache_timeout& value);
template <>
std::size_t byte_size_long_value<attribute_value_response_origin>(const attribute_value_response_origin& value);
template <>
std::size_t byte_size_long_value<attribute_value_other_address>(const attribute_value_other_address& value);

template <>
void serialize_value<stun_address_family>(void* dst, const stun_address_family& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_mapped_address_base>(void* dst, const attribute_value_mapped_address_base& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_mapped_address>(void* dst, const attribute_value_mapped_address& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_xor_mapped_address>(void* dst, const attribute_value_xor_mapped_address& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_username>(void* dst, const attribute_value_username& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_message_integrity>(void* dst, const attribute_value_message_integrity& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_message_integrity::input_data>(void* dst, const attribute_value_message_integrity::input_data& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_message_integrity::output_data>(void* dst, const attribute_value_message_integrity::output_data& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_fingerprint>(void* dst, const attribute_value_fingerprint& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_error_code>(void* dst, const attribute_value_error_code& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_realm>(void* dst, const attribute_value_realm& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_nonce>(void* dst, const attribute_value_nonce& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_unknown_attributes>(void* dst, const attribute_value_unknown_attributes& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_software>(void* dst, const attribute_value_software& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_alternate_server>(void* dst, const attribute_value_alternate_server& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_priority>(void* dst, const attribute_value_priority& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_use_candidate>(void* dst, const attribute_value_use_candidate& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_ice_controlled>(void* dst, const attribute_value_ice_controlled& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_ice_controlling>(void* dst, const attribute_value_ice_controlling& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_change_request>(void* dst, const attribute_value_change_request& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_response_port>(void* dst, const attribute_value_response_port& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_padding>(void* dst, const attribute_value_padding& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_cache_timeout>(void* dst, const attribute_value_cache_timeout& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_response_origin>(void* dst, const attribute_value_response_origin& src, std::size_t& offset);
template <>
void serialize_value<attribute_value_other_address>(void* dst, const attribute_value_other_address& src, std::size_t& offset);

template <>
void parse_value<stun_address_family>(stun_address_family& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_mapped_address_base>(attribute_value_mapped_address_base& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_mapped_address>(attribute_value_mapped_address& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_xor_mapped_address>(attribute_value_xor_mapped_address& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_username>(attribute_value_username& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_message_integrity>(attribute_value_message_integrity& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_message_integrity::output_data>(attribute_value_message_integrity::output_data& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_fingerprint>(attribute_value_fingerprint& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_error_code>(attribute_value_error_code& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_realm>(attribute_value_realm& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_nonce>(attribute_value_nonce& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_unknown_attributes>(attribute_value_unknown_attributes& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_software>(attribute_value_software& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_alternate_server>(attribute_value_alternate_server& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_priority>(attribute_value_priority& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_use_candidate>(attribute_value_use_candidate& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_ice_controlled>(attribute_value_ice_controlled& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_ice_controlling>(attribute_value_ice_controlling& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_change_request>(attribute_value_change_request& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_response_port>(attribute_value_response_port& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_padding>(attribute_value_padding& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_cache_timeout>(attribute_value_cache_timeout& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_response_origin>(attribute_value_response_origin& dst, const rstream::core::memory memory, std::size_t& offset);
template <>
void parse_value<attribute_value_other_address>(attribute_value_other_address& dst, const rstream::core::memory memory, std::size_t& offset);

template <>
void serialize_json_value<attribute_value_mapped_address_base>(nlohmann::json& json, const attribute_value_mapped_address_base& value);
template <>
void serialize_json_value<attribute_value_mapped_address>(nlohmann::json& json, const attribute_value_mapped_address& value);
template <>
void serialize_json_value<attribute_value_xor_mapped_address>(nlohmann::json& json, const attribute_value_xor_mapped_address& value);
template <>
void serialize_json_value<attribute_value_username>(nlohmann::json& json, const attribute_value_username& value);
template <>
void serialize_json_value<attribute_value_message_integrity>(nlohmann::json& json, const attribute_value_message_integrity& value);
template <>
void serialize_json_value<attribute_value_fingerprint>(nlohmann::json& json, const attribute_value_fingerprint& value);
template <>
void serialize_json_value<attribute_value_error_code>(nlohmann::json& json, const attribute_value_error_code& value);
template <>
void serialize_json_value<attribute_value_realm>(nlohmann::json& json, const attribute_value_realm& value);
template <>
void serialize_json_value<attribute_value_nonce>(nlohmann::json& json, const attribute_value_nonce& value);
template <>
void serialize_json_value<attribute_value_unknown_attributes>(nlohmann::json& json, const attribute_value_unknown_attributes& value);
template <>
void serialize_json_value<attribute_value_software>(nlohmann::json& json, const attribute_value_software& value);
template <>
void serialize_json_value<attribute_value_alternate_server>(nlohmann::json& json, const attribute_value_alternate_server& value);
template <>
void serialize_json_value<attribute_value_priority>(nlohmann::json& json, const attribute_value_priority& value);
template <>
void serialize_json_value<attribute_value_use_candidate>(nlohmann::json& json, const attribute_value_use_candidate& value);
template <>
void serialize_json_value<attribute_value_ice_controlled>(nlohmann::json& json, const attribute_value_ice_controlled& value);
template <>
void serialize_json_value<attribute_value_ice_controlling>(nlohmann::json& json, const attribute_value_ice_controlling& value);
template <>
void serialize_json_value<attribute_value_change_request>(nlohmann::json& json, const attribute_value_change_request& value);
template <>
void serialize_json_value<attribute_value_response_port>(nlohmann::json& json, const attribute_value_response_port& value);
template <>
void serialize_json_value<attribute_value_padding>(nlohmann::json& json, const attribute_value_padding& value);
template <>
void serialize_json_value<attribute_value_cache_timeout>(nlohmann::json& json, const attribute_value_cache_timeout& value);
template <>
void serialize_json_value<attribute_value_response_origin>(nlohmann::json& json, const attribute_value_response_origin& value);
template <>
void serialize_json_value<attribute_value_other_address>(nlohmann::json& json, const attribute_value_other_address& value);

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
