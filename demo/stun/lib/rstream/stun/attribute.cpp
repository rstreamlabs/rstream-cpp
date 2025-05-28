// See LICENSE file in the project root for license information.

#include "attribute.hpp"

#include <map>
#include <sstream>

#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <boost/format.hpp>
#include <boost/system/system_error.hpp>

#include <string.h>

#include <rstream/core/crc32.hpp>
#include <rstream/io/error.hpp>
#include <rstream/stun/hmac.hpp>
#include <rstream/stun/md5.hpp>

#define STUN_FINGERPRINT_XOR 0x5354554E  // "STUN"

#define STUN_MAKE_ATTRIBUTE_PROPERTY(arg1, arg2)                                                                                                 \
  {                                                                                                                                              \
    attribute_type::arg1, { arg2, get_hash_code<attribute_value_##arg1>(), #arg1, make_attribute_serialize_json_func<attribute_value_##arg1>() } \
  }

namespace rstream {
namespace stun {

static const std::map<attribute_type, attribute_property> m_attribute_properties = {
    STUN_MAKE_ATTRIBUTE_PROPERTY(mapped_address, 0x0001),
    STUN_MAKE_ATTRIBUTE_PROPERTY(xor_mapped_address, 0x0020),
    STUN_MAKE_ATTRIBUTE_PROPERTY(username, 0x0006),
    STUN_MAKE_ATTRIBUTE_PROPERTY(message_integrity, 0x0008),
    STUN_MAKE_ATTRIBUTE_PROPERTY(fingerprint, 0x8028),
    STUN_MAKE_ATTRIBUTE_PROPERTY(error_code, 0x0009),
    STUN_MAKE_ATTRIBUTE_PROPERTY(realm, 0x0014),
    STUN_MAKE_ATTRIBUTE_PROPERTY(nonce, 0x0015),
    STUN_MAKE_ATTRIBUTE_PROPERTY(unknown_attributes, 0x000A),
    STUN_MAKE_ATTRIBUTE_PROPERTY(software, 0x8022),
    STUN_MAKE_ATTRIBUTE_PROPERTY(alternate_server, 0x8023),
    STUN_MAKE_ATTRIBUTE_PROPERTY(priority, 0x0024),
    STUN_MAKE_ATTRIBUTE_PROPERTY(use_candidate, 0x0025),
    STUN_MAKE_ATTRIBUTE_PROPERTY(ice_controlled, 0x8029),
    STUN_MAKE_ATTRIBUTE_PROPERTY(ice_controlling, 0x802A),
    STUN_MAKE_ATTRIBUTE_PROPERTY(change_request, 0x0003),
    STUN_MAKE_ATTRIBUTE_PROPERTY(response_port, 0x0027),
    STUN_MAKE_ATTRIBUTE_PROPERTY(padding, 0x0026),
    STUN_MAKE_ATTRIBUTE_PROPERTY(cache_timeout, 0x8027),
    STUN_MAKE_ATTRIBUTE_PROPERTY(response_origin, 0x802b),
    STUN_MAKE_ATTRIBUTE_PROPERTY(other_address, 0x802c)};

std::string to_string(error_code error_code)
{
  switch (error_code) {
    case error_code::try_alternate:
      return "try alternate";
    case error_code::bad_request:
      return "bad request";
    case error_code::unauthenticated:
      return "unauthenticated";
    case error_code::unknown_attribute:
      return "unknown attribute";
    case error_code::stale_nonce:
      return "stale nonce";
    case error_code::server_error:
      return "server error";
    case error_code::role_conflict:
      return "role conflict";
    case error_code::forbidden:
      return "forbidden";
    case error_code::allocation_mismatch:
      return "allocation_mismatch";
    case error_code::address_family_not_supported:
      return "address family not supported";
    case error_code::wrong_credentials:
      return "wrong credentials";
    case error_code::unsupported_transport_protocol:
      return "unsupported transport protocol";
    case error_code::peer_address_family_mismatch:
      return "peer address family mismatch";
    case error_code::allocation_quota_reached:
      return "allocation quota reached";
    case error_code::insufficient_capacity:
      return "insufficient capacity";
    default:
      return "undefined / unknown error";
  }
}

msg_type get_attribute_msg_type(attribute_type type)
{
  auto iterator = m_attribute_properties.find(type);
  if (iterator != m_attribute_properties.end()) {
    return iterator->second.m_msg_type;
  }
  throw boost::system::system_error(error::code::unknown_stun_attribute);
}

attribute_type get_attribute_type(msg_type type)
{
  for (auto it = m_attribute_properties.cbegin(); it != m_attribute_properties.cend(); ++it) {
    if (it->second.m_msg_type == type) {
      return it->first;
    }
  }
  throw boost::system::system_error(error::code::unknown_stun_attribute);
}

attribute_type get_attribute_type(std::size_t hash_code)
{
  for (auto it = m_attribute_properties.cbegin(); it != m_attribute_properties.cend(); ++it) {
    if (it->second.m_hash_code == hash_code) {
      return it->first;
    }
  }
  throw boost::system::system_error(error::code::unknown_stun_attribute);
}

std::uint8_t& attribute_value_mapped_address_base::get_padding()
{
  return m_padding;
}

const std::uint8_t& attribute_value_mapped_address_base::get_padding() const
{
  return m_padding;
}

stun_address_family& attribute_value_mapped_address_base::get_family()
{
  return m_family;
}

const stun_address_family& attribute_value_mapped_address_base::get_family() const
{
  return m_family;
}

std::uint16_t& attribute_value_mapped_address_base::get_port()
{
  return m_port;
}

const std::uint16_t& attribute_value_mapped_address_base::get_port() const
{
  return m_port;
}

rstream::core::memory& attribute_value_mapped_address_base::get_address_memory()
{
  return m_address;
}

const rstream::core::memory& attribute_value_mapped_address_base::get_address_memory() const
{
  return m_address;
}

boost::asio::ip::address attribute_value_mapped_address_base::get_address() const
{
  if (get_family() == stun_address_family::IPV4) {
    return boost::asio::ip::address(boost::asio::ip::address_v4(ntohl(*(const boost::asio::ip::address_v4::uint_type*)get_address_memory().get_const_data())));
  }
  else {
    boost::asio::ip::address_v6::bytes_type array;
    ::memcpy(array.data(), get_address_memory().get_const_data(), array.size());
    return boost::asio::ip::address(boost::asio::ip::address_v6(array));
  }
}

void attribute_value_mapped_address_base::set_address(const boost::asio::ip::address& address)
{
  if (address.is_v4()) {
    get_family()                                                                = stun_address_family::IPV4;
    get_address_memory()                                                        = rstream::core::make_memory_allocated(address.to_v4().to_bytes().size());
    *((boost::asio::ip::address_v4::uint_type*)get_address_memory().get_data()) = htonl(address.to_v4().to_uint());
  }
  else {
    get_family()         = stun_address_family::IPV6;
    const auto bytes     = address.to_v6().to_bytes();
    get_address_memory() = rstream::core::make_memory_allocated(bytes.size());
    ::memcpy(get_address_memory().get_data(), bytes.data(), bytes.size());
  }
}

void attribute_value_xor_mapped_address::apply_mask(const mask_type& mask)
{
  get_port()                 = ntohs(htons(get_port()) ^ *((const std::uint16_t*)mask.data()));
  auto bytes                 = (std::uint8_t*)get_address_memory().get_data();
  std::size_t address_length = get_family() == stun_address_family::IPV4 ? 4 : 16;
  for (std::size_t i = 0; i < address_length; ++i) {
    bytes[i] = bytes[i] ^ mask[i];
  }
}

void attribute_value_xor_mapped_address::read_mask(const void* memory, mask_type& mask)
{
  *((std::uint32_t*)mask.data()) = *(const std::uint32_t*)&(((const std::uint8_t*)memory)[4]);
  ::memcpy(&mask.data()[sizeof(std::uint32_t)], &(((const std::uint8_t*)memory)[8]), 12);
}

std::string& attribute_value_username::get_value()
{
  return m_value;
}

const std::string& attribute_value_username::get_value() const
{
  return m_value;
}

std::pair<std::string, std::string> attribute_value_username::parse_string(char separator) const
{
  auto pos = get_value().find(separator);
  if (pos == std::string::npos) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "missing separator");
  }
  return std::make_pair(get_value().substr(0, pos), get_value().substr(pos + 1, get_value().length() - (pos + 1)));
}

attribute_value_message_integrity::attribute_value_message_integrity()
    : m_data_type(data_type::undefined)
{
}

attribute_value_message_integrity::data_type attribute_value_message_integrity::get_data_type() const
{
  return m_data_type;
}

attribute_value_message_integrity::input_data& attribute_value_message_integrity::get_input_data()
{
  if (m_data_type != data_type::input) {
    m_data_type           = data_type::input;
    input_data input_data = {
        .m_password = "",
    };
    m_data = input_data;
  }
  return boost::any_cast<input_data&>(m_data);
}

const attribute_value_message_integrity::input_data& attribute_value_message_integrity::get_input_data() const
{
  if (m_data_type != data_type::input) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "expecting input data");
  }
  return boost::any_cast<const input_data&>(m_data);
}

attribute_value_message_integrity::output_data& attribute_value_message_integrity::get_output_data()
{
  if (m_data_type != data_type::output) {
    m_data_type             = data_type::output;
    output_data output_data = {
        .m_serialized = output_data::array_type(),
    };
    m_data = output_data;
  }
  return boost::any_cast<output_data&>(m_data);
}

const attribute_value_message_integrity::output_data& attribute_value_message_integrity::get_output_data() const
{
  if (m_data_type != data_type::output) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "expecting output data");
  }
  return boost::any_cast<const output_data&>(m_data);
}

std::string attribute_value_message_integrity::compute_long_term_credentials(const std::string& username, const std::string& realm, const std::string& password)
{
  std::string output;
  output.resize(16);
  std::string input(username + ":" + realm + ":" + password);
  ::rstream::stun::md5_sum(input.data(), input.size(), &output[0]);
  return output;
}

void attribute_value_message_integrity::hmac_sha1(const void* data, std::size_t size, const std::string& password, void* dst)
{
  ::rstream::stun::hmac_sha1(data, size, password.data(), password.size(), dst);
}

std::string attribute_value_message_integrity::to_string(const attribute_value_message_integrity::output_data::array_type& data)
{
  std::stringstream str;
  for (auto i = 0; i != data.size(); ++i) {
    str << (boost::format("%02x") % (int)data.data()[i]);
  }
  return str.str();
}

attribute_value_fingerprint::state attribute_value_fingerprint::get_state() const
{
  return m_value.operator bool() ? state::processed : state::unprocessed;
}

std::uint32_t& attribute_value_fingerprint::get_value()
{
  if (get_state() == state::unprocessed) {
    m_value = 0;
  }
  return m_value.get();
}

const std::uint32_t& attribute_value_fingerprint::get_value() const
{
  if (get_state() == state::unprocessed) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "attribute not processed");
  }
  return m_value.get();
}

std::uint32_t attribute_value_fingerprint::crc32(const void* data, std::size_t size)
{
  return rstream::core::crc32(data, size) ^ STUN_FINGERPRINT_XOR;
}

std::uint8_t& attribute_value_error_code::get_code_class()
{
  return m_code_class;
}

const std::uint8_t& attribute_value_error_code::get_code_class() const
{
  return m_code_class;
}

std::uint8_t& attribute_value_error_code::get_code_number()
{
  return m_code_number;
}

const std::uint8_t& attribute_value_error_code::get_code_number() const
{
  return m_code_number;
}

std::string& attribute_value_error_code::get_reason()
{
  return m_reason;
}

const std::string& attribute_value_error_code::get_reason() const
{
  return m_reason;
}

error_code attribute_value_error_code::get_error_code() const
{
  return (error_code)(int)((get_code_class() & 0x07) * 100 + get_code_number());
}

void attribute_value_error_code::set_error_code(error_code error_code)
{
  get_code_class()  = (static_cast<int>(error_code) / 100) & 0x07;
  get_code_number() = static_cast<int>(error_code) % 100;
  get_reason()      = to_string(error_code);
}

attribute_value_error_code make_error_code(error_code error_code)
{
  attribute_value_error_code attribute_value_error_code;
  attribute_value_error_code.set_error_code(error_code);
  return attribute_value_error_code;
}

attribute_value_error_code make_error_code(error_code error_code, const std::string& reason)
{
  auto attribute_value_error_code         = make_error_code(error_code);
  attribute_value_error_code.get_reason() = reason;
  return attribute_value_error_code;
}

std::string& attribute_value_realm::get_value()
{
  return m_value;
}

const std::string& attribute_value_realm::get_value() const
{
  return m_value;
}

std::string& attribute_value_nonce::get_value()
{
  return m_value;
}

const std::string& attribute_value_nonce::get_value() const
{
  return m_value;
}

std::list<msg_type>& attribute_value_unknown_attributes::get_value()
{
  return m_value;
}

const std::list<msg_type>& attribute_value_unknown_attributes::get_value() const
{
  return m_value;
}

std::string& attribute_value_software::get_value()
{
  return m_value;
}

const std::string& attribute_value_software::get_value() const
{
  return m_value;
}

std::uint32_t& attribute_value_priority::get_value()
{
  return m_value;
}

const std::uint32_t& attribute_value_priority::get_value() const
{
  return m_value;
}

std::uint64_t& attribute_value_ice_controlled::get_value()
{
  return m_value;
}

const std::uint64_t& attribute_value_ice_controlled::get_value() const
{
  return m_value;
}

std::uint64_t& attribute_value_ice_controlling::get_value()
{
  return m_value;
}

const std::uint64_t& attribute_value_ice_controlling::get_value() const
{
  return m_value;
}

std::uint32_t& attribute_value_change_request::get_value()
{
  return m_value;
}

const std::uint32_t& attribute_value_change_request::get_value() const
{
  return m_value;
}

std::uint16_t& attribute_value_response_port::get_value()
{
  return m_value;
}

const std::uint16_t& attribute_value_response_port::get_value() const
{
  return m_value;
}

std::string& attribute_value_padding::get_value()
{
  return m_value;
}

const std::string& attribute_value_padding::get_value() const
{
  return m_value;
}

}  // namespace stun
}  // namespace rstream

namespace rstream {
namespace stun {
namespace helpers {

template <>
std::size_t byte_size_long_value<stun_address_family>(const stun_address_family& value)
{
  std::size_t size = 0;
  size += sizeof(std::uint8_t);
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_mapped_address_base>(const attribute_value_mapped_address_base& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_padding());
  size += byte_size_long_value(value.get_family());
  size += byte_size_long_value(value.get_port());
  size += byte_size_long_value(value.get_address_memory());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_mapped_address>(const attribute_value_mapped_address& value)
{
  return byte_size_long_value<attribute_value_mapped_address_base>(value);
}

template <>
std::size_t byte_size_long_value<attribute_value_xor_mapped_address>(const attribute_value_xor_mapped_address& value)
{
  return byte_size_long_value<attribute_value_mapped_address_base>(value);
}

template <>
std::size_t byte_size_long_value<attribute_value_username>(const attribute_value_username& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_message_integrity>(const attribute_value_message_integrity& value)
{
  std::size_t size = 0;
  size += sizeof(attribute_value_message_integrity::output_data);
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_fingerprint>(const attribute_value_fingerprint& value)
{
  std::size_t size = 0;
  size += sizeof(std::uint32_t);
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_error_code>(const attribute_value_error_code& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_code_class());
  size += byte_size_long_value(value.get_code_number());
  size += byte_size_long_value(value.get_reason());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_realm>(const attribute_value_realm& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_nonce>(const attribute_value_nonce& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_unknown_attributes>(const attribute_value_unknown_attributes& value)
{
  std::size_t size = 0;
  for (const auto& attribute : value.get_value()) {
    size += byte_size_long_value(attribute);
  }
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_software>(const attribute_value_software& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_alternate_server>(const attribute_value_alternate_server& value)
{
  return byte_size_long_value<attribute_value_mapped_address_base>(value);
}

template <>
std::size_t byte_size_long_value<attribute_value_priority>(const attribute_value_priority& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_use_candidate>(const attribute_value_use_candidate& value)
{
  return 0;
}

template <>
std::size_t byte_size_long_value<attribute_value_ice_controlled>(const attribute_value_ice_controlled& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_ice_controlling>(const attribute_value_ice_controlling& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_change_request>(const attribute_value_change_request& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_response_port>(const attribute_value_response_port& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_padding>(const attribute_value_padding& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_value());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_value_cache_timeout>(const attribute_value_cache_timeout& value)
{
  return 0;
}

template <>
std::size_t byte_size_long_value<attribute_value_response_origin>(const attribute_value_response_origin& value)
{
  return byte_size_long_value<attribute_value_mapped_address_base>(value);
}

template <>
std::size_t byte_size_long_value<attribute_value_other_address>(const attribute_value_other_address& value)
{
  return byte_size_long_value<attribute_value_mapped_address_base>(value);
}

template <>
void serialize_value<stun_address_family>(void* dst, const stun_address_family& src, std::size_t& offset)
{
  std::uint8_t value;
  if (src == stun_address_family::IPV4) {
    value = 0x01;
  }
  else if (src == stun_address_family::IPV6) {
    value = 0x02;
  }
  else {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "unknown IP family");
  }
  serialize_value(dst, value, offset);
}

template <>
void serialize_value<attribute_value_mapped_address_base>(void* dst, const attribute_value_mapped_address_base& src, std::size_t& offset)
{
  serialize_value(dst, src.get_padding(), offset);
  serialize_value(dst, src.get_family(), offset);
  serialize_value(dst, htons(src.get_port()), offset);
  serialize_value(dst, src.get_address_memory(), offset);
}

template <>
void serialize_value<attribute_value_mapped_address>(void* dst, const attribute_value_mapped_address& src, std::size_t& offset)
{
  serialize_value<attribute_value_mapped_address_base>(dst, src, offset);
}

template <>
void serialize_value<attribute_value_xor_mapped_address>(void* dst, const attribute_value_xor_mapped_address& src, std::size_t& offset)
{
  attribute_value_xor_mapped_address::mask_type mask;
  attribute_value_xor_mapped_address::read_mask(dst, mask);
  attribute_value_xor_mapped_address tmp = src;
  tmp.get_address_memory()               = tmp.get_address_memory().copy();
  tmp.apply_mask(mask);
  serialize_value<attribute_value_mapped_address_base>(dst, tmp, offset);
}

template <>
void serialize_value<attribute_value_username>(void* dst, const attribute_value_username& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_message_integrity::input_data>(void* dst, const attribute_value_message_integrity::input_data& src, std::size_t& offset)
{
  auto diff = sizeof(attribute_value_message_integrity::output_data);
  auto data = &((std::uint8_t*)dst)[offset];
  attribute_value_message_integrity::hmac_sha1(dst, (offset - byte_size_long_value(attribute_header())), src.m_password, data);
  offset += diff;
}

template <>
void serialize_value<attribute_value_message_integrity::output_data>(void* dst, const attribute_value_message_integrity::output_data& src, std::size_t& offset)
{
  auto diff = sizeof(attribute_value_message_integrity::output_data);
  auto data = &((std::uint8_t*)dst)[offset];
  ::memcpy(data, src.m_serialized.data(), diff);
  offset += diff;
}

template <>
void serialize_value<attribute_value_message_integrity>(void* dst, const attribute_value_message_integrity& src, std::size_t& offset)
{
  auto type = src.get_data_type();
  if (type == attribute_value_message_integrity::data_type::input) {
    serialize_value(dst, src.get_input_data(), offset);
  }
  else if (type == attribute_value_message_integrity::data_type::output) {
    serialize_value(dst, src.get_output_data(), offset);
  }
}

template <>
void serialize_value<attribute_value_fingerprint>(void* dst, const attribute_value_fingerprint& src, std::size_t& offset)
{
  std::uint32_t value;
  if (src.get_state() == attribute_value_fingerprint::state::processed) {
    value = src.get_value();
  }
  else {
    value = htonl(attribute_value_fingerprint::crc32(dst, (offset - byte_size_long_value(attribute_header()))));
  }
  serialize_value(dst, value, offset);
}

template <>
void serialize_value<attribute_value_error_code>(void* dst, const attribute_value_error_code& src, std::size_t& offset)
{
  serialize_value(dst, src.get_code_class(), offset);
  serialize_value(dst, src.get_code_number(), offset);
  serialize_value(dst, src.get_reason(), offset);
}

template <>
void serialize_value<attribute_value_realm>(void* dst, const attribute_value_realm& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_nonce>(void* dst, const attribute_value_nonce& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_unknown_attributes>(void* dst, const attribute_value_unknown_attributes& src, std::size_t& offset)
{
  for (const auto& attribute : src.get_value()) {
    serialize_value(dst, attribute, offset);
  }
}

template <>
void serialize_value<attribute_value_software>(void* dst, const attribute_value_software& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_alternate_server>(void* dst, const attribute_value_alternate_server& src, std::size_t& offset)
{
  serialize_value<attribute_value_mapped_address_base>(dst, src, offset);
}

template <>
void serialize_value<attribute_value_priority>(void* dst, const attribute_value_priority& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_use_candidate>(void* dst, const attribute_value_use_candidate& src, std::size_t& offset)
{
}

template <>
void serialize_value<attribute_value_ice_controlled>(void* dst, const attribute_value_ice_controlled& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_ice_controlling>(void* dst, const attribute_value_ice_controlling& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_change_request>(void* dst, const attribute_value_change_request& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_response_port>(void* dst, const attribute_value_response_port& src, std::size_t& offset)
{
  serialize_value(dst, htons(src.get_value()), offset);
}

template <>
void serialize_value<attribute_value_padding>(void* dst, const attribute_value_padding& src, std::size_t& offset)
{
  serialize_value(dst, src.get_value(), offset);
}

template <>
void serialize_value<attribute_value_cache_timeout>(void* dst, const attribute_value_cache_timeout& src, std::size_t& offset)
{
}

template <>
void serialize_value<attribute_value_response_origin>(void* dst, const attribute_value_response_origin& src, std::size_t& offset)
{
  serialize_value<attribute_value_mapped_address_base>(dst, src, offset);
}

template <>
void serialize_value<attribute_value_other_address>(void* dst, const attribute_value_other_address& src, std::size_t& offset)
{
  serialize_value<attribute_value_mapped_address_base>(dst, src, offset);
}

template <>
void parse_value<stun_address_family>(stun_address_family& dst, const rstream::core::memory memory, std::size_t& offset)
{
  std::uint8_t value;
  parse_value(value, memory, offset);
  if (value == 0x01) {
    dst = stun_address_family::IPV4;
  }
  else if (value == 0x02) {
    dst = stun_address_family::IPV6;
  }
  else {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "unknown IP family");
  }
}

template <>
void parse_value<attribute_value_mapped_address_base>(attribute_value_mapped_address_base& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_padding(), memory, offset);
  parse_value(dst.get_family(), memory, offset);
  parse_value(dst.get_port(), memory, offset);
  dst.get_port() = ntohs(dst.get_port());
  std::size_t expected_address_length;
  if (dst.get_family() == stun_address_family::IPV4) {
    expected_address_length = 4;
  }
  else {
    expected_address_length = 16;
  }
  std::size_t address_length = memory.get_size() - offset;
  if (address_length != expected_address_length) {
    throw std::runtime_error("parsing error");
  }
  dst.get_address_memory() = memory.copy(offset, address_length);
  offset += address_length;
}

template <>
void parse_value<attribute_value_mapped_address>(attribute_value_mapped_address& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value<attribute_value_mapped_address_base>(dst, memory, offset);
}

template <>
void parse_value<attribute_value_xor_mapped_address>(attribute_value_xor_mapped_address& dst, const rstream::core::memory memory, std::size_t& offset)
{
  attribute_value_xor_mapped_address::mask_type mask;
  attribute_value_xor_mapped_address::read_mask(memory.get_const_data(), mask);
  parse_value<attribute_value_mapped_address_base>(dst, memory, offset);
  dst.apply_mask(mask);
}

template <>
void parse_value<attribute_value_username>(attribute_value_username& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_message_integrity::output_data>(attribute_value_message_integrity::output_data& dst, const rstream::core::memory memory, std::size_t& offset)
{
  auto diff = sizeof(attribute_value_message_integrity::output_data);
  auto data = &((const std::uint8_t*)memory.get_const_data())[offset];
  if ((offset + diff) > memory.get_size()) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "data has invalid size");
  }
  ::memcpy(dst.m_serialized.data(), data, diff);
  offset += diff;
}

template <>
void parse_value<attribute_value_message_integrity>(attribute_value_message_integrity& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_output_data(), memory, offset);
}

template <>
void parse_value<attribute_value_fingerprint>(attribute_value_fingerprint& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
  dst.get_value() = ntohl(dst.get_value());
}

template <>
void parse_value<attribute_value_error_code>(attribute_value_error_code& dst, const rstream::core::memory memory, std::size_t& offset)
{
  std::uint16_t padding;
  parse_value(padding, memory, offset);
  if (padding != 0) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid padding");
  }
  std::uint8_t code_class;
  parse_value(code_class, memory, offset);
  if ((code_class & 0xF8) != 0) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid error code class");
  }
  dst.get_code_class() = (code_class & 0x07);
  std::uint8_t code_number;
  parse_value(code_number, memory, offset);
  dst.get_code_number() = code_number;
  parse_value(dst.get_reason(), memory, offset);
}

template <>
void parse_value<attribute_value_realm>(attribute_value_realm& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_nonce>(attribute_value_nonce& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_unknown_attributes>(attribute_value_unknown_attributes& dst, const rstream::core::memory memory, std::size_t& offset)
{
  auto size = memory.get_size();
  if (size % 4 != 0) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid attribute size");
  }
  while (offset < size) {
    msg_type type;
    parse_value(type, memory, offset);
    dst.get_value().push_back(type);
  }
}

template <>
void parse_value<attribute_value_software>(attribute_value_software& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_alternate_server>(attribute_value_alternate_server& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value<attribute_value_mapped_address_base>(dst, memory, offset);
}

template <>
void parse_value<attribute_value_priority>(attribute_value_priority& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_use_candidate>(attribute_value_use_candidate& dst, const rstream::core::memory memory, std::size_t& offset)
{
}

template <>
void parse_value<attribute_value_ice_controlled>(attribute_value_ice_controlled& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_ice_controlling>(attribute_value_ice_controlling& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_change_request>(attribute_value_change_request& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_response_port>(attribute_value_response_port& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
  dst.get_value() = ntohs(dst.get_value());
}

template <>
void parse_value<attribute_value_padding>(attribute_value_padding& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_value(), memory, offset);
}

template <>
void parse_value<attribute_value_cache_timeout>(attribute_value_cache_timeout& dst, const rstream::core::memory memory, std::size_t& offset)
{
}

template <>
void parse_value<attribute_value_response_origin>(attribute_value_response_origin& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value<attribute_value_mapped_address_base>(dst, memory, offset);
}

template <>
void parse_value<attribute_value_other_address>(attribute_value_other_address& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value<attribute_value_mapped_address_base>(dst, memory, offset);
}

template <>
void serialize_json_value<attribute_header>(nlohmann::json& json, const attribute_header& value)
{
  auto iterator = m_attribute_properties.find(get_attribute_type(value.get_type()));
  if (iterator == m_attribute_properties.end()) {
    throw boost::system::system_error(error::code::unknown_stun_attribute);
  }
  json["name"] = iterator->second.m_name;
}

template <>
void serialize_json_value<attribute>(nlohmann::json& json, const attribute& value)
{
  serialize_json_value(json, value.get_header());
  nlohmann::json content;
  auto iterator = m_attribute_properties.find(get_attribute_type(value.get_header().get_type()));
  if (iterator == m_attribute_properties.end()) {
    throw boost::system::system_error(error::code::unknown_stun_attribute);
  }
  if (value.get_data_type() == attribute::data_type::parsed) {
    value.get_data_parsed().serialize_json(content);
  }
  else {
    iterator->second.m_attribute_serialize_json_func(content, value.get_data_serialized());
  }
  if (!content.is_null()) {
    json["content"] = content;
  }
}

template <>
void serialize_json_value<attribute_value_mapped_address_base>(nlohmann::json& json, const attribute_value_mapped_address_base& value)
{
  json["address"] = value.get_address().to_string();
  json["port"]    = value.get_port();
}

template <>
void serialize_json_value<attribute_value_mapped_address>(nlohmann::json& json, const attribute_value_mapped_address& value)
{
  serialize_json_value<attribute_value_mapped_address_base>(json, value);
}

template <>
void serialize_json_value<attribute_value_xor_mapped_address>(nlohmann::json& json, const attribute_value_xor_mapped_address& value)
{
  serialize_json_value<attribute_value_mapped_address_base>(json, value);
}

template <>
void serialize_json_value<attribute_value_username>(nlohmann::json& json, const attribute_value_username& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_message_integrity>(nlohmann::json& json, const attribute_value_message_integrity& value)
{
  auto type = value.get_data_type();
  if (type == attribute_value_message_integrity::data_type::input) {
    json["password"] = value.get_input_data().m_password;
  }
  else if (type == attribute_value_message_integrity::data_type::output) {
    json["integrity"] = attribute_value_message_integrity::to_string(value.get_output_data().m_serialized);
  }
}

template <>
void serialize_json_value<attribute_value_fingerprint>(nlohmann::json& json, const attribute_value_fingerprint& value)
{
  if (value.get_state() == attribute_value_fingerprint::state::processed) {
    json = value.get_value();
  }
}

template <>
void serialize_json_value<attribute_value_error_code>(nlohmann::json& json, const attribute_value_error_code& value)
{
  json["code"]["class"]  = value.get_code_class();
  json["code"]["number"] = value.get_code_number();
  json["reason"]         = value.get_reason();
}

template <>
void serialize_json_value<attribute_value_realm>(nlohmann::json& json, const attribute_value_realm& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_nonce>(nlohmann::json& json, const attribute_value_nonce& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_unknown_attributes>(nlohmann::json& json, const attribute_value_unknown_attributes& value)
{
  for (const auto& attribute : value.get_value()) {
    json.push_back(attribute);
  }
}

template <>
void serialize_json_value<attribute_value_software>(nlohmann::json& json, const attribute_value_software& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_alternate_server>(nlohmann::json& json, const attribute_value_alternate_server& value)
{
  serialize_json_value<attribute_value_mapped_address_base>(json, value);
}

template <>
void serialize_json_value<attribute_value_priority>(nlohmann::json& json, const attribute_value_priority& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_use_candidate>(nlohmann::json& json, const attribute_value_use_candidate& value)
{
}

template <>
void serialize_json_value<attribute_value_ice_controlled>(nlohmann::json& json, const attribute_value_ice_controlled& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_ice_controlling>(nlohmann::json& json, const attribute_value_ice_controlling& value)
{
  json = value.get_value();
}

template <>
void serialize_json_value<attribute_value_change_request>(nlohmann::json& json, const attribute_value_change_request& value)
{
  if (value.get_value() | attribute_value_change_request::change_ip) {
    json["flags"].push_back("change_ip");
  }
  if (value.get_value() | attribute_value_change_request::change_port) {
    json["flags"].push_back("change_port");
  }
}

template <>
void serialize_json_value<attribute_value_response_port>(nlohmann::json& json, const attribute_value_response_port& value)
{
  json["port"] = value.get_value();
}

template <>
void serialize_json_value<attribute_value_padding>(nlohmann::json& json, const attribute_value_padding& value)
{
  json["padding"] = value.get_value();
}

template <>
void serialize_json_value<attribute_value_cache_timeout>(nlohmann::json& json, const attribute_value_cache_timeout& value)
{
}

template <>
void serialize_json_value<attribute_value_response_origin>(nlohmann::json& json, const attribute_value_response_origin& value)
{
  serialize_json_value<attribute_value_mapped_address_base>(json, value);
}

template <>
void serialize_json_value<attribute_value_other_address>(nlohmann::json& json, const attribute_value_other_address& value)
{
  serialize_json_value<attribute_value_mapped_address_base>(json, value);
}

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
