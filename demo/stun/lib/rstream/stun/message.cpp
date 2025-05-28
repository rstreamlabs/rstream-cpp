// See LICENSE file in the project root for license information.

#include "message.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include <sstream>

#include <boost/format.hpp>
#include <boost/system/system_error.hpp>

#include <string.h>

#include <rstream/config.hpp>
#include <rstream/core/crc32.hpp>
#include <rstream/core/random.hpp>

#include "attribute.hpp"
#include "error.hpp"

#ifdef _WIN32
#include <winsock.h>  // included last to avoif boost.asio errors
#endif

#define STUN_CLASS_MASK            0x0110
#define STUN_HEADER_SIZE           20
#define STUN_MAGIC                 0x2112A442
#define STUN_ATTRIBUTE_HEADER_SIZE 4

#define STUN_GET_CLASS(msg_type)  (msg_type & STUN_CLASS_MASK)
#define STUN_GET_METHOD(msg_type) (msg_type & ~STUN_CLASS_MASK)

namespace rstream {
namespace stun {

static const boost::bimap<stun_class, msg_type> m_stun_classes = boost::assign::list_of<boost::bimap<stun_class, msg_type>::relation>(stun_class::request, 0x0000)(stun_class::indication, 0x0010)(stun_class::response_success, 0x0100)(stun_class::response_error, 0x0110);

static const boost::bimap<stun_class, std::string> m_stun_classes_str = boost::assign::list_of<boost::bimap<stun_class, std::string>::relation>(stun_class::request, "request")(stun_class::indication, "indication")(stun_class::response_success, "response_success")(stun_class::response_error, "response_error");

static const boost::bimap<stun_method, msg_type> m_stun_methods = boost::assign::list_of<boost::bimap<stun_method, msg_type>::relation>(stun_method::binding, 0x0001);

static const boost::bimap<stun_method, std::string> m_stun_methods_str = boost::assign::list_of<boost::bimap<stun_method, std::string>::relation>(stun_method::binding, "binding");

std::string to_string(msg_transaction_id msg_transaction_id)
{
  std::stringstream str;
  for (auto i = 0; i != msg_transaction_id.size(); ++i) {
    str << (boost::format("%02x") % (int)msg_transaction_id.data()[i]);
  }
  return str.str();
}

msg_transaction_id random_msg_transaction_id()
{
  msg_transaction_id msg_transaction_id;
  rstream::core::random_bytes(&msg_transaction_id, sizeof(msg_transaction_id));
  return msg_transaction_id;
}

msg_type encode_msg_type(stun_class stun_class, stun_method stun_method)
{
  return m_stun_classes.left.at(stun_class) | m_stun_methods.left.at(stun_method);
}

stun_class parse_stun_class(msg_type msg_type)
{
  auto iterator = m_stun_classes.right.find(STUN_GET_CLASS(msg_type));
  if (iterator != m_stun_classes.right.end()) {
    return iterator->second;
  }
  throw boost::system::system_error(error::code::unknown_stun_class);
}

std::string to_string(stun_class stun_class)
{
  auto iterator = m_stun_classes_str.left.find(stun_class);
  if (iterator != m_stun_classes_str.left.end()) {
    return iterator->second;
  }
  else {
    return "undefined";
  }
}

stun_method parse_stun_method(msg_type msg_type)
{
  auto iterator = m_stun_methods.right.find(STUN_GET_METHOD(msg_type));
  if (iterator != m_stun_methods.right.end()) {
    return iterator->second;
  }
  throw boost::system::system_error(error::code::unknown_stun_method);
}

std::string to_string(stun_method stun_method)
{
  auto iterator = m_stun_methods_str.left.find(stun_method);
  if (iterator != m_stun_methods_str.left.end()) {
    return iterator->second;
  }
  else {
    return "undefined";
  }
}

msg_type& header::get_type()
{
  return m_type;
}

const msg_type& header::get_type() const
{
  return m_type;
}

std::uint16_t& header::get_payload_length()
{
  return m_payload_length;
}

const std::uint16_t& header::get_payload_length() const
{
  return m_payload_length;
}

msg_magic& header::get_magic()
{
  return m_magic;
}

const msg_magic& header::get_magic() const
{
  return m_magic;
}

msg_transaction_id& header::get_transaction_id()
{
  return m_transaction_id;
}

const msg_transaction_id& header::get_transaction_id() const
{
  return m_transaction_id;
}

stun_class header::get_stun_class() const
{
  return parse_stun_class(get_type());
}

stun_method header::get_stun_method() const
{
  return parse_stun_method(get_type());
}

bool header::is_response() const
{
  return (STUN_GET_CLASS(get_type()) & 0x0100);
}

msg_type& attribute_header::get_type()
{
  return m_type;
}

const msg_type& attribute_header::get_type() const
{
  return m_type;
}

std::uint16_t& attribute_header::get_length()
{
  return m_length;
}

const std::uint16_t& attribute_header::get_length() const
{
  return m_length;
}

const rstream::core::memory attribute::data_serialized::get() const
{
  return m_data.share(m_offset, m_size);
}

attribute::attribute()
    : m_data_type(data_type::undefined)
{
}

attribute::data_type attribute::get_data_type() const
{
  return m_data_type;
}

attribute_header& attribute::get_header()
{
  return m_header;
}

const attribute_header& attribute::get_header() const
{
  return m_header;
}

const attribute::data_serialized& attribute::get_data_serialized() const
{
  if (m_data_type != data_type::serialized) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "attribute is not serialized");
  }
  return *std::static_pointer_cast<attribute::data_serialized>(m_data);
}

const attribute::data_parsed& attribute::get_data_parsed() const
{
  if (m_data_type != data_type::parsed) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "attribute has no data");
  }
  return *std::static_pointer_cast<attribute::data_parsed>(m_data);
}

attribute attribute::make(const rstream::core::memory& data, std::size_t& offset)
{
  class attribute attribute;
  attribute.m_data_type = data_type::serialized;
  parse_value(attribute.get_header(), data, offset);
  attribute::data_serialized data_serialized = {
      .m_data   = data,
      .m_offset = offset,
      .m_size   = attribute.get_header().get_length(),
  };
  attribute.m_data = std::make_shared<attribute::data_serialized>(data_serialized);
  // attributes are aligned on 4 bytes
  auto length = data_serialized.m_size;
  while (length & 0x03) {
    ++length;
  }
  offset += length;
  return attribute;
}

attribute make_attribute(const rstream::core::memory& data, std::size_t& offset)
{
  return attribute::make(data, offset);
}

header& message::get_header()
{
  return m_header;
}

const header& message::get_header() const
{
  return m_header;
}

attributes& message::get_attributes()
{
  return m_attributes;
}

const attributes& message::get_attributes() const
{
  return m_attributes;
}

void message::check_fingerprint() const
{
  auto attribute = get_attribute(*this, get_attribute_msg_type(get_attribute_type<attribute_value_fingerprint>()));
  auto current   = attribute.get<attribute_value_fingerprint>().get_value();
  auto expected  = attribute_value_fingerprint::crc32(attribute.get_data_serialized().m_data.get_const_data(), (attribute.get_data_serialized().m_offset - byte_size_long_value(attribute_header())));
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "STUN fingerprint mismatch [expected: " << expected << ", current: " << current << "]";
    throw boost::system::system_error(error::code::invalid_integrity, stringstream.str());
  }
}

static std::uint16_t get_message_header_payload_length(const void* src)
{
  return ntohs(*((const std::uint16_t*)&((const std::uint8_t*)src)[2]));
};

static void set_message_header_payload_length(void* dst, std::uint16_t value)
{
  *((std::uint16_t*)&((std::uint8_t*)dst)[2]) = htons(value);
};

void message::check_message_integrity(const std::string& password) const
{
  std::size_t tmp_payload_length = 0;
  auto attribute_msg_type        = get_attribute_msg_type(get_attribute_type<attribute_value_message_integrity>());
  auto it                        = get_attributes().begin();
  for (; it != get_attributes().end(); ++it) {
    tmp_payload_length += byte_size_long_value(*it);
    if (it->get_header().get_type() == attribute_msg_type) {
      break;
    }
  }
  if (it == get_attributes().end()) {
    throw rstream::core::system_error(error::code::unknown_stun_attribute, "missing message-integrity attribute");
  }
  auto current = it->get<attribute_value_message_integrity>().get_output_data().m_serialized;
  attribute_value_message_integrity::output_data::array_type expected;
  {
    auto memory                      = it->get_data_serialized().m_data;
    std::uint16_t old_payload_length = get_message_header_payload_length(memory.get_const_data());
    auto tmp                         = memory;
    auto writable                    = tmp.is_mutable();
    if (!writable) {
      tmp = tmp.copy();
    }
    set_message_header_payload_length(tmp.get_data(), (std::uint16_t)tmp_payload_length);
    attribute_value_message_integrity::hmac_sha1(tmp.get_data(), (it->get_data_serialized().m_offset - byte_size_long_value(attribute_header())), password, expected.data());
    if (writable) {
      set_message_header_payload_length(memory.get_data(), old_payload_length);
    }
  }
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "STUN message integrity mismatch [expected: " << attribute_value_message_integrity::to_string(expected) << ", current: " << attribute_value_message_integrity::to_string(current) << "]";
    throw boost::system::system_error(error::code::invalid_integrity, stringstream.str());
  }
}

bool message::has_integrity() const
{
  return (find_attribute(*this, get_attribute_msg_type(get_attribute_type<attribute_value_message_integrity>())) != get_attributes().end());
}

std::uint32_t message::get_priority() const
{
  std::uint32_t priority = 0;
  auto it                = find_attribute(*this, get_attribute_msg_type(get_attribute_type<attribute_value_priority>()));
  if (it != get_attributes().end()) {
    priority = it->get<attribute_value_priority>().get_value();
  }
  return priority;
}

std::string message::to_string() const
{
  nlohmann::json json;
  json << *this;
  return json.dump(2);
}

attributes::const_iterator find_attribute(const message& message, msg_type attribute_msg_type)
{
  attributes::const_iterator it;
  for (it = message.get_attributes().cbegin(); it != message.get_attributes().cend(); ++it) {
    if (it->get_header().get_type() == attribute_msg_type) {
      break;
    }
  }
  return it;
}

attribute get_attribute(const message& message, msg_type attribute_msg_type)
{
  auto it = find_attribute(message, attribute_msg_type);
  if (it != message.get_attributes().end()) {
    return *it;
  }
  throw boost::system::system_error(error::code::unknown_stun_attribute);
}

void is_stun_datagram(const rstream::core::memory memory, std::exception_ptr& error)
{
  header header;
  std::size_t offset = 0;
  try {
    parse_value(header, memory, offset);
  }
  catch (...) {
    error = std::current_exception();
  }
}

bool is_stun_datagram(const rstream::core::memory memory)
{
  std::exception_ptr error;
  is_stun_datagram(memory, error);
  return error != nullptr;
}

void is_stun_datagram(rstream::core::buffer buffer, std::exception_ptr& error)
{
  return is_stun_datagram(buffer.map(rstream::core::buffer::map_mode::read), error);
}

bool is_stun_datagram(rstream::core::buffer buffer)
{
  std::exception_ptr error;
  is_stun_datagram(buffer, error);
  return error != nullptr;
}

bool has_attribute(const message& message, msg_type attribute_msg_type)
{
  return (find_attribute(message, attribute_msg_type) != message.get_attributes().end());
}

message_builder::message_builder(msg_type msg_type, const msg_transaction_id& transaction_id)
{
  auto& header                = m_message.get_header();
  header.get_type()           = msg_type;
  header.get_magic()          = STUN_MAGIC;
  header.get_transaction_id() = transaction_id;
}

message_builder::message_builder(stun_class stun_class, stun_method stun_method, const msg_transaction_id& transaction_id)
    : message_builder(encode_msg_type(stun_class, stun_method), transaction_id)
{
}
message_builder::message_builder(msg_type msg_type)
    : message_builder(msg_type, random_msg_transaction_id())
{
}
message_builder::message_builder(stun_class stun_class, stun_method stun_method)
    : message_builder(encode_msg_type(stun_class, stun_method))
{
}

attributes& message_builder::get_attributes()
{
  return m_message.get_attributes();
}

const attributes& message_builder::get_attributes() const
{
  return m_message.get_attributes();
}

const message& message_builder::build()
{
  m_message.get_header().get_payload_length() = byte_size_long_value(get_attributes());

  return m_message;
}

void message_builder::add_software()
{
  attribute_value_software attribute;
  attribute.get_value() = "rstream STUN client";
  add_attribute(attribute);
}

void message_builder::add_message_integrity(const std::string& password)
{
  attribute_value_message_integrity attribute;
  attribute.get_input_data() = {
      .m_password = password,
  };
  add_attribute(attribute);
}

void message_builder::add_fingerprint()
{
  attribute_value_fingerprint attribute;
  add_attribute(attribute);
}

std::size_t byte_size_long_value(const std::string& value);
std::size_t byte_size_long_value(const rstream::core::memory& value);
void serialize_value(void* dst, const std::string& src, std::size_t& offset);
void serialize_value(void* dst, const rstream::core::memory& src, std::size_t& offset);
void parse_value(std::string& dst, const rstream::core::memory memory, std::size_t& offset);

}  // namespace stun
}  // namespace rstream

namespace rstream {
namespace stun {
namespace helpers {

template <>
std::size_t byte_size_long_value<header>(const header& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(msg_type());
  size += byte_size_long_value(std::uint16_t());
  size += byte_size_long_value(msg_magic());
  size += byte_size_long_value(msg_transaction_id());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute_header>(const attribute_header& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_type());
  size += byte_size_long_value(value.get_length());
  return size;
}

template <>
std::size_t byte_size_long_value<attribute>(const attribute& value)
{
  std::size_t size = 0;
  if (value.get_data_type() == attribute::data_type::undefined) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "attribute is undefined");
  }
  else if (value.get_data_type() == attribute::data_type::serialized) {
    size += byte_size_long_value(value.get_data_serialized().get());
  }
  else {
    size += value.get_data_parsed().byte_size_long();
  }
  // attributes are aligned on 4 bytes
  while (size & 0x03) {
    ++size;
  }
  // add header
  size += byte_size_long_value(attribute_header());
  return size;
}

template <>
std::size_t byte_size_long_value<attributes>(const attributes& value)
{
  std::size_t size = 0;
  for (const auto& attribute : value) {
    size += byte_size_long_value(attribute);
  }
  return size;
}

template <>
std::size_t byte_size_long_value<message>(const message& value)
{
  std::size_t size = 0;
  size += byte_size_long_value(value.get_header());
  size += byte_size_long_value(value.get_attributes());
  return size;
}

template <>
void serialize_value<header>(void* dst, const header& src, std::size_t& offset)
{
  serialize_value(dst, htons(src.get_type()), offset);
  serialize_value(dst, htons(src.get_payload_length()), offset);
  serialize_value(dst, htonl(src.get_magic()), offset);
  serialize_value(dst, src.get_transaction_id(), offset);
}

template <>
void serialize_value<attribute_header>(void* dst, const attribute_header& src, std::size_t& offset)
{
  serialize_value(dst, htons(src.get_type()), offset);
  serialize_value(dst, htons(src.get_length()), offset);
}

template <>
void serialize_value<attribute>(void* dst, const attribute& src, std::size_t& offset)
{
  const auto size           = byte_size_long_value(src);
  const auto initial_offset = offset;
  if (src.get_data_type() == attribute::data_type::undefined) {
    throw rstream::core::system_error(error::code::invalid_stun_attribute, "attribute is undefined");
  }
  else {
    serialize_value(dst, src.get_header(), offset);
    if (src.get_data_type() == attribute::data_type::serialized) {
      serialize_value(dst, src.get_data_serialized().get(), offset);
    }
    else {
      src.get_data_parsed().serialize(dst, offset);
    }
  }
  // attributes are aligned on 4 bytes
  while ((offset - initial_offset) < size) {
    offset++;
    ((std::uint8_t*)dst)[offset] = 0x00;  // 'padding bits are ignored, and may be any value' (RFC5389)
  }
}

template <>
void serialize_value<attributes>(void* dst, const attributes& src, std::size_t& offset)
{
  std::uint16_t payload_length = 0;
  for (const auto& attribute : src) {
    payload_length += byte_size_long_value(attribute);
    set_message_header_payload_length(dst, payload_length);
    serialize_value(dst, attribute, offset);
  }
}

template <>
void serialize_value<message>(void* dst, const message& src, std::size_t& offset)
{
  serialize_value(dst, src.get_header(), offset);
  serialize_value(dst, src.get_attributes(), offset);
}

template <>
void parse_value<header>(header& dst, const rstream::core::memory memory, std::size_t& offset)
{
  /*
   * STUN message header (20 bytes)
   * See https://tools.ietf.org/html/rfc5389#section-6
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |0 0|     STUN Message Type     |         Message Length        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                    Magic Cookie = 0x2112A442                  |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                                                               |
   * |                     Transaction ID (96 bits)                  |
   * |                                                               |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  const auto size = (memory.get_size() - offset);
  // RFC 5389: The most significant 2 bits of every STUN message MUST be zeroes
  if (size < STUN_HEADER_SIZE) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid header size");
  }
  else {
    std::uint8_t byte = *((const uint8_t*)memory.get_const_data());
    if (byte & 0xC0) {
      throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid start byte");
    }
  }
  parse_value(dst.get_type(), memory, offset);
  parse_value(dst.get_payload_length(), memory, offset);
  parse_value(dst.get_magic(), memory, offset);
  dst.get_type()           = ntohs(dst.get_type());
  dst.get_payload_length() = ntohs(dst.get_payload_length());
  dst.get_magic()          = ntohl(dst.get_magic());
  if (dst.get_magic() != STUN_MAGIC) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid magic");
  }
  // RFC 5389: The message length MUST contain the size, in bytes, of the message not including
  // the 20-byte STUN header. Since all STUN attributes are padded to a multiple of 4 bytes, the
  // last 2 bits of this field are always zero.
  if (dst.get_payload_length() & 0x03) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid payload size");
  }
  if (size != STUN_HEADER_SIZE + dst.get_payload_length()) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid message size");
  }
  ::memcpy(&dst.get_transaction_id(), &((const std::uint8_t*)memory.get_const_data())[offset], STUN_TRANSACTION_ID_SIZE);
  offset += STUN_TRANSACTION_ID_SIZE;
  // sanity checks
  dst.get_stun_class();
  dst.get_stun_method();
}

template <>
void parse_value<attribute_header>(attribute_header& dst, const rstream::core::memory memory, std::size_t& offset)
{
  /*
   * STUN attribute header (4 bytes)
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |             Type              |            Length             |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  const auto size = (memory.get_size() - offset);
  if (size < STUN_ATTRIBUTE_HEADER_SIZE) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid attribute header size");
  }
  parse_value(dst.get_type(), memory, offset);
  parse_value(dst.get_length(), memory, offset);
  dst.get_type()   = ntohs(dst.get_type());
  dst.get_length() = ntohs(dst.get_length());
  if (size < STUN_ATTRIBUTE_HEADER_SIZE + dst.get_length()) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid attribute size");
  }
}

template <>
void parse_value<attribute>(attribute& dst, const rstream::core::memory memory, std::size_t& offset)
{
  dst = make_attribute(memory, offset);
}

template <>
void parse_value<attributes>(attributes& dst, const rstream::core::memory memory, std::size_t& offset)
{
  dst.clear();
  while (offset < memory.get_size()) {
    attribute attribute;
    parse_value(attribute, memory, offset);
    dst.push_back(attribute);
  }
  if (offset != memory.get_size()) {
    throw rstream::core::system_error(rstream::io::error::code::deserialization_error, "invalid attribute size");
  }
}

template <>
void parse_value<message>(message& dst, const rstream::core::memory memory, std::size_t& offset)
{
  parse_value(dst.get_header(), memory, offset);
  parse_value(dst.get_attributes(), memory, offset);
}

template <>
void serialize_json_value<header>(nlohmann::json& json, const header& value)
{
  json["magic"]          = (boost::format("%02x") % value.get_magic()).str();
  json["transaction_id"] = to_string(value.get_transaction_id());
  json["type"]["class"]  = to_string(value.get_stun_class());
  json["type"]["method"] = to_string(value.get_stun_method());
}

template <>
void serialize_json_value<attributes>(nlohmann::json& json, const attributes& value)
{
  for (const auto& attribute : value) {
    std::exception_ptr error = nullptr;
    nlohmann::json tmp;
    try {
      tmp << attribute;
    }
    catch (...) {
      error = std::current_exception();
    }
    if (error) {
      std::stringstream str;
      str << "undefined attribute 0x" << std::hex << std::uppercase << attribute.get_header().get_type();
      tmp = str.str();
    }
    json.push_back(tmp);
  }
}

template <>
void serialize_json_value<message>(nlohmann::json& json, const message& value)
{
  serialize_json_value(json["header"], value.get_header());
  const auto& attributes = value.get_attributes();
  if (!attributes.empty()) {
    serialize_json_value(json["attributes"], value.get_attributes());
  }
}

}  // namespace helpers
}  // namespace stun
}  // namespace rstream
