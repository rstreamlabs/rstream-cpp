// See LICENSE file in the project root for license information.

#include <iostream>
#include <sstream>

#include <rstream/config.hpp>
#include <rstream/stun/attribute.hpp>

using namespace rstream::stun;

template <class T>
void compare(const T& current, const T& expected)
{
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "attribute has unexpected value [current: " << current << ", expected: " << expected << "]";
    throw std::runtime_error(stringstream.str());
  }
}

template <>
void compare<msg_transaction_id>(const msg_transaction_id& current, const msg_transaction_id& expected)
{
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "transaction ID mismatch";
    throw std::runtime_error(stringstream.str());
  }
}

void test_1()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  // sample request
  const uint8_t test_message[] = {
      0x00, 0x01, 0x00, 0x58,  //    Request type and message length
      0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
      0xb7, 0xe7, 0xa7, 0x01,  // }
      0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
      0xfa, 0x87, 0xdf, 0xae,  // }
      0x80, 0x22, 0x00, 0x10,  //    SOFTWARE attribute header
      0x53, 0x54, 0x55, 0x4e,  // }
      0x20, 0x74, 0x65, 0x73,  // }  User-agent...
      0x74, 0x20, 0x63, 0x6c,  // }  ...name
      0x69, 0x65, 0x6e, 0x74,  // }
      0x00, 0x24, 0x00, 0x04,  //    PRIORITY attribute header
      0x6e, 0x00, 0x01, 0xff,  //    ICE priority value
      0x80, 0x29, 0x00, 0x08,  //    ICE-CONTROLLED attribute header
      0x93, 0x2f, 0xf9, 0xb1,  // }  Pseudo-random tie breaker...
      0x51, 0x26, 0x3b, 0x36,  // }   ...for ICE control
      0x00, 0x06, 0x00, 0x09,  //    USERNAME attribute header
      0x65, 0x76, 0x74, 0x6a,  // }
      0x3a, 0x68, 0x36, 0x76,  // }  Username (9 bytes) and padding (3 bytes)
      0x59, 0x20, 0x20, 0x20,  // }
      0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
      0x9a, 0xea, 0xa7, 0x0c,  // }
      0xbf, 0xd8, 0xcb, 0x56,  // }
      0x78, 0x1e, 0xf2, 0xb5,  // }  HMAC-SHA1 fingerprint
      0xb2, 0xd3, 0xf2, 0x49,  // }
      0xc1, 0xb5, 0x71, 0xa2,  // }
      0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
      0xe5, 0x7a, 0x3b, 0xcf   //    CRC32 fingerprint
  };

  static const msg_transaction_id msg_transaction_id = {
      0xb7,
      0xe7,
      0xa7,
      0x01,
      0xbc,
      0x34,
      0xd6,
      0x86,
      0xfa,
      0x87,
      0xdf,
      0xae,
  };

  const std::string software         = "STUN test client";
  const std::string username         = "evtj:h6vY";
  const std::string integrity_key    = "VOkJxbRl1RmTxUk/WvJxBt";
  const std::uint32_t priority       = 4278255726;
  const std::uint64_t ice_controlled = 3907759233994141587;
  // create buffer
  auto memory = rstream::core::make_memory_wrapped(test_message, sizeof(test_message));
  // parse buffer
  auto message = rstream::stun::message().parse(memory);
  // check transaction ID
  compare(message.get_header().get_transaction_id(), msg_transaction_id);
  // check 'SOFTWARE' attribute
  compare(get_attribute<attribute_value_software>(message).get_value(), software);
  // check 'PRIORITY' attribute
  compare(get_attribute<attribute_value_priority>(message).get_value(), priority);
  // check 'ICE-CONTROLLED' attribute
  compare(get_attribute<attribute_value_ice_controlled>(message).get_value(), ice_controlled);
  // check 'USERNAME' attribute
  compare(get_attribute<attribute_value_username>(message).get_value(), username);
  // check 'MESSAGE-INTEGRITY' attribute
  message.check_message_integrity(integrity_key);
  // check 'FINGERPRINT' attribute
  message.check_fingerprint();
}

void test_2()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  // sample IPv4 response
  const uint8_t test_message[] = {
      0xff, 0xff, 0xff, 0xff,  //    Garbage data
      0x01, 0x01, 0x00, 0x3c,  //    Response type and message length
      0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
      0xb7, 0xe7, 0xa7, 0x01,  // }
      0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
      0xfa, 0x87, 0xdf, 0xae,  // }
      0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
      0x74, 0x65, 0x73, 0x74,  // }
      0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
      0x74, 0x6f, 0x72, 0x20,  // }
      0x00, 0x20, 0x00, 0x08,  //    XOR-MAPPED-ADDRESS attribute header
      0x00, 0x01, 0xa1, 0x47,  //    Address family (IPv4) and xor'd mapped port
      0xe1, 0x12, 0xa6, 0x43,  //    Xor'd mapped IPv4 address
      0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
      0x2b, 0x91, 0xf5, 0x99,  // }
      0xfd, 0x9e, 0x90, 0xc3,  // }
      0x8c, 0x74, 0x89, 0xf9,  // }  HMAC-SHA1 fingerprint
      0x2a, 0xf9, 0xba, 0x53,  // }
      0xf0, 0x6b, 0xe7, 0xd7,  // }
      0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
      0xc0, 0x7d, 0x4c, 0x96,  //    CRC32 fingerprint
      0xff, 0xff, 0xff, 0xff   //    Garbage data
  };
  static const msg_transaction_id msg_transaction_id = {
      0xb7,
      0xe7,
      0xa7,
      0x01,
      0xbc,
      0x34,
      0xd6,
      0x86,
      0xfa,
      0x87,
      0xdf,
      0xae,
  };
  const std::string software      = "test vector";
  const auto address              = std::make_pair("192.0.2.1", 32853);
  const std::string integrity_key = "VOkJxbRl1RmTxUk/WvJxBt";
  // create buffer
  auto memory = rstream::core::make_memory_wrapped(test_message, sizeof(test_message));
  memory.resize(4, sizeof(test_message) - 8);
  // parse buffer
  auto message = rstream::stun::message().parse(memory);
  // check transaction ID
  compare(message.get_header().get_transaction_id(), msg_transaction_id);
  // check 'SOFTWARE' attribute
  compare(get_attribute<attribute_value_software>(message).get_value(), software);
  // check 'XOR-MAPPED-ADDRESS' attribute
  {
    const auto& attribute = get_attribute<attribute_value_xor_mapped_address>(message);
    compare(attribute.get_address().to_string(), std::string(address.first));
    compare(attribute.get_port(), (std::uint16_t)address.second);
  }
  // check 'MESSAGE-INTEGRITY' attribute
  message.check_message_integrity(integrity_key);
  // check 'FINGERPRINT' attribute
  message.check_fingerprint();
}

void test_3()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  // sample IPv6 response
  const uint8_t test_message[] = {
      0x01, 0x01, 0x00, 0x48,  //    Response type and message length
      0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
      0xb7, 0xe7, 0xa7, 0x01,  // }
      0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
      0xfa, 0x87, 0xdf, 0xae,  // }
      0x80, 0x22, 0x00, 0x0b,  //    SOFTWARE attribute header
      0x74, 0x65, 0x73, 0x74,  // }
      0x20, 0x76, 0x65, 0x63,  // }  UTF-8 server name
      0x74, 0x6f, 0x72, 0x20,  // }
      0x00, 0x20, 0x00, 0x14,  //    XOR-MAPPED-ADDRESS attribute header
      0x00, 0x02, 0xa1, 0x47,  //    Address family (IPv6) and xor'd mapped port.
      0x01, 0x13, 0xa9, 0xfa,  // }
      0xa5, 0xd3, 0xf1, 0x79,  // }  Xor'd mapped IPv6 address
      0xbc, 0x25, 0xf4, 0xb5,  // }
      0xbe, 0xd2, 0xb9, 0xd9,  // }
      0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
      0xa3, 0x82, 0x95, 0x4e,  // }
      0x4b, 0xe6, 0x7b, 0xf1,  // }
      0x17, 0x84, 0xc9, 0x7c,  // }  HMAC-SHA1 fingerprint
      0x82, 0x92, 0xc2, 0x75,  // }
      0xbf, 0xe3, 0xed, 0x41,  // }
      0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
      0xc8, 0xfb, 0x0b, 0x4c   //    CRC32 fingerprint
  };
  static const msg_transaction_id msg_transaction_id = {
      0xb7,
      0xe7,
      0xa7,
      0x01,
      0xbc,
      0x34,
      0xd6,
      0x86,
      0xfa,
      0x87,
      0xdf,
      0xae,
  };
  const std::string software      = "test vector";
  const auto address              = std::make_pair("2001:db8:1234:5678:11:2233:4455:6677", 32853);
  const std::string integrity_key = "VOkJxbRl1RmTxUk/WvJxBt";
  // create buffer
  auto memory = rstream::core::make_memory_wrapped(test_message, sizeof(test_message));
  // parse buffer
  auto message = rstream::stun::message().parse(memory);
  // check transaction ID
  compare(message.get_header().get_transaction_id(), msg_transaction_id);
  // check 'SOFTWARE' attribute
  compare(get_attribute<attribute_value_software>(message).get_value(), software);
  // check 'XOR-MAPPED-ADDRESS' attribute
  {
    const auto& attribute = get_attribute<attribute_value_xor_mapped_address>(message);
    compare(attribute.get_address().to_string(), std::string(address.first));
    compare(attribute.get_port(), (std::uint16_t)address.second);
  }
  // check 'MESSAGE-INTEGRITY' attribute
  message.check_message_integrity(integrity_key);
  // check 'FINGERPRINT' attribute
  message.check_fingerprint();
}

void test_4()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  // sample request with long-term authentication
  const uint8_t test_message[] = {
      0x00, 0x01, 0x00, 0x60,  //    Request type and message length
      0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
      0x78, 0xad, 0x34, 0x33,  // }
      0xc6, 0xad, 0x72, 0xc0,  // }  Transaction ID
      0x29, 0xda, 0x41, 0x2e,  // }
      0x00, 0x06, 0x00, 0x12,  //    USERNAME attribute header
      0xe3, 0x83, 0x9e, 0xe3,  // }
      0x83, 0x88, 0xe3, 0x83,  // }
      0xaa, 0xe3, 0x83, 0x83,  // }  Username value (18 bytes) and padding (2 bytes)
      0xe3, 0x82, 0xaf, 0xe3,  // }
      0x82, 0xb9, 0x00, 0x00,  // }
      0x00, 0x15, 0x00, 0x1c,  //    NONCE attribute header
      0x66, 0x2f, 0x2f, 0x34,  // }
      0x39, 0x39, 0x6b, 0x39,  // }
      0x35, 0x34, 0x64, 0x36,  // }
      0x4f, 0x4c, 0x33, 0x34,  // }  Nonce value
      0x6f, 0x4c, 0x39, 0x46,  // }
      0x53, 0x54, 0x76, 0x79,  // }
      0x36, 0x34, 0x73, 0x41,  // }
      0x00, 0x14, 0x00, 0x0b,  //    REALM attribute header
      0x65, 0x78, 0x61, 0x6d,  // }
      0x70, 0x6c, 0x65, 0x2e,  // }  Realm value (11 bytes) and padding (1 byte)
      0x6f, 0x72, 0x67, 0x00,  // }
      0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
      0xf6, 0x70, 0x24, 0x65,  // }
      0x6d, 0xd6, 0x4a, 0x3e,  // }
      0x02, 0xb8, 0xe0, 0x71,  // }  HMAC-SHA1 fingerprint
      0x2e, 0x85, 0xc9, 0xa2,  // }
      0x8c, 0xa8, 0x96, 0x66   // }
  };
  static const msg_transaction_id msg_transaction_id = {
      0x78,
      0xad,
      0x34,
      0x33,
      0xc6,
      0xad,
      0x72,
      0xc0,
      0x29,
      0xda,
      0x41,
      0x2e,
  };
  const std::string username = "\xe3\x83\x9e\xe3\x83\x88\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf\xe3\x82\xb9";
  const std::string nonce    = "f//499k954d6OL34oL9FSTvy64sA";
  const std::string realm    = "example.org";
  const std::string password = "TheMatrIX";
  // create buffer
  auto memory = rstream::core::make_memory_wrapped(test_message, sizeof(test_message));
  // parse buffer
  auto message = rstream::stun::message().parse(memory);
  // check transaction ID
  compare(message.get_header().get_transaction_id(), msg_transaction_id);
  // check 'USERNAME' attribute
  compare(get_attribute<attribute_value_username>(message).get_value(), username);
  // check 'NONCE' attribute
  compare(get_attribute<attribute_value_nonce>(message).get_value(), nonce);
  // check 'REALM' attribute
  compare(get_attribute<attribute_value_realm>(message).get_value(), realm);
  // check 'MESSAGE-INTEGRITY' attribute
  message.check_message_integrity(attribute_value_message_integrity::compute_long_term_credentials(username, realm, password));
}

void test_5()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  const std::string password = "password";
  auto builder               = rstream::stun::message_builder(rstream::stun::stun_class::request, rstream::stun::stun_method::binding);
  builder.add_message_integrity(password);
  builder.add_fingerprint();
  auto check = [&](const message& message) {
    message.check_message_integrity(password);
  };
  check(rstream::stun::message().parse(builder.build().serialize_to_memory()));
}

void test_6()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  const auto address = std::make_pair("192.0.2.1", 32853);
  auto builder       = rstream::stun::message_builder(rstream::stun::stun_class::request, rstream::stun::stun_method::binding);
  {
    attribute_value_mapped_address attribute;
    attribute.set_address(boost::asio::ip::make_address(address.first));
    attribute.get_port() = address.second;
    builder.add_attribute(attribute);
  }
  {
    attribute_value_xor_mapped_address attribute;
    attribute.set_address(boost::asio::ip::make_address(address.first));
    attribute.get_port() = address.second;
    builder.add_attribute(attribute);
  }
  builder.add_fingerprint();
  auto check = [&](const message& message, bool check_fingerprint) {
    if (check_fingerprint) {
      message.check_fingerprint();
    }
    {
      const auto& attribute = get_attribute<attribute_value_mapped_address>(message);
      compare(attribute.get_address().to_string(), std::string(address.first));
      compare(attribute.get_port(), (std::uint16_t)address.second);
    }
    {
      const auto& attribute = get_attribute<attribute_value_xor_mapped_address>(message);
      compare(attribute.get_address().to_string(), std::string(address.first));
      compare(attribute.get_port(), (std::uint16_t)address.second);
    }
  };
  check(builder.build(), false);
  check(rstream::stun::message().parse(builder.build().serialize_to_memory()), true);
}

void test_7()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  const auto address = std::make_pair("2001:db8:1234:5678:11:2233:4455:6677", 32853);
  auto builder       = rstream::stun::message_builder(rstream::stun::stun_class::request, rstream::stun::stun_method::binding);
  {
    attribute_value_mapped_address attribute;
    attribute.set_address(boost::asio::ip::make_address(address.first));
    attribute.get_port() = address.second;
    builder.add_attribute(attribute);
  }
  {
    attribute_value_xor_mapped_address attribute;
    attribute.set_address(boost::asio::ip::make_address(address.first));
    attribute.get_port() = address.second;
    builder.add_attribute(attribute);
  }
  builder.add_fingerprint();
  auto check = [&](const message& message, bool check_fingerprint) {
    if (check_fingerprint) {
      message.check_fingerprint();
    }
    {
      const auto& attribute = get_attribute<attribute_value_mapped_address>(message);
      compare(attribute.get_address().to_string(), std::string(address.first));
      compare(attribute.get_port(), (std::uint16_t)address.second);
    }
    {
      const auto& attribute = get_attribute<attribute_value_xor_mapped_address>(message);
      compare(attribute.get_address().to_string(), std::string(address.first));
      compare(attribute.get_port(), (std::uint16_t)address.second);
    }
  };
  check(builder.build(), false);
  check(rstream::stun::message().parse(builder.build().serialize_to_memory()), true);
}

void test_8()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  // sample request
  const uint8_t test_message[] = {
      0x00, 0x01, 0x00, 0x58,  //    Request type and message length
      0x21, 0x12, 0xa4, 0x42,  //    Magic cookie
      0xb7, 0xe7, 0xa7, 0x01,  // }
      0xbc, 0x34, 0xd6, 0x86,  // }  Transaction ID
      0xfa, 0x87, 0xdf, 0xae,  // }
      0x80, 0x22, 0x00, 0x10,  //    SOFTWARE attribute header
      0x53, 0x54, 0x55, 0x4e,  // }
      0x20, 0x74, 0x65, 0x73,  // }  User-agent...
      0x74, 0x20, 0x63, 0x6c,  // }  ...name
      0x69, 0x65, 0x6e, 0x74,  // }
      0x00, 0x24, 0x00, 0x04,  //    PRIORITY attribute header
      0x6e, 0x00, 0x01, 0xff,  //    ICE priority value
      0x80, 0x29, 0x00, 0x08,  //    ICE-CONTROLLED attribute header
      0x93, 0x2f, 0xf9, 0xb1,  // }  Pseudo-random tie breaker...
      0x51, 0x26, 0x3b, 0x36,  // }   ...for ICE control
      0x00, 0x06, 0x00, 0x09,  //    USERNAME attribute header
      0x65, 0x76, 0x74, 0x6a,  // }
      0x3a, 0x68, 0x36, 0x76,  // }  Username (9 bytes) and padding (3 bytes)
      0x59, 0x20, 0x20, 0x20,  // }
      0x00, 0x08, 0x00, 0x14,  //    MESSAGE-INTEGRITY attribute header
      0x9a, 0xea, 0xa7, 0x0c,  // }
      0xbf, 0xd8, 0xcb, 0x56,  // }
      0x78, 0x1e, 0xf2, 0xb5,  // }  HMAC-SHA1 fingerprint
      0xb2, 0xd3, 0xf2, 0x49,  // }
      0xc1, 0xb5, 0x71, 0xa2,  // }
      0x80, 0x28, 0x00, 0x04,  //    FINGERPRINT attribute header
      0xe5, 0x7a, 0x3b, 0xcf   //    CRC32 fingerprint
  };
  // create buffer
  auto memory = rstream::core::make_memory_wrapped(test_message, sizeof(test_message));
  // parse buffer
  auto message = rstream::stun::message().parse(memory);
  // serialize message
  const auto serialized = "{\"attributes\":[{\"content\":\"STUN test client\",\"name\":\"software\"},{\"content\":4278255726,\"name\":\"priority\"},{\"content\":3907759233994141587,\"name\":\"ice_controlled\"},{\"content\":\"evtj:h6vY\",\"name\":\"username\"},{\"content\":{\"integrity\":\"9aeaa70cbfd8cb56781ef2b5b2d3f249c1b571a2\"},\"name\":\"message_integrity\"},{\"content\":3849993167,\"name\":\"fingerprint\"}],\"header\":{\"magic\":\"2112a442\",\"transaction_id\":\"b7e7a701bc34d686fa87dfae\",\"type\":{\"class\":\"request\",\"method\":\"binding\"}}}";
  nlohmann::json json;
  json << message;
  compare(json, nlohmann::json().parse(serialized));
}

void test_9()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  rstream::stun::message copy;
  {
    auto builder = rstream::stun::message_builder(rstream::stun::stun_class::request, rstream::stun::stun_method::binding);
    builder.add_software();
    builder.add_fingerprint();
    rstream::stun::message message = builder.build();
    copy                           = message;
  }
  nlohmann::json json;
  json << copy;
}

void test_10()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto check = [](std::uint32_t value, const nlohmann::json& expected) {
    attribute_value_change_request change_request;
    change_request.get_value() = value;
    nlohmann::json json;
    rstream::stun::helpers::serialize_json_value(json, change_request);
    compare(json, expected);
  };
  check(0, nullptr);
  check(attribute_value_change_request::change_ip, nlohmann::json::parse(R"({"flags":["change_ip"]})"));
  check(attribute_value_change_request::change_port, nlohmann::json::parse(R"({"flags":["change_port"]})"));
  check(attribute_value_change_request::change_ip | attribute_value_change_request::change_port, nlohmann::json::parse(R"({"flags":["change_ip","change_port"]})"));
}

void test_11()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  header message_header;
  compare(message_header.get_type(), static_cast<msg_type>(0));
  compare(message_header.get_payload_length(), static_cast<std::uint16_t>(0));
  compare(message_header.get_magic(), static_cast<msg_magic>(0));
  compare(message_header.get_transaction_id(), msg_transaction_id{});
  attribute_header attribute;
  compare(attribute.get_type(), static_cast<msg_type>(0));
  compare(attribute.get_length(), static_cast<std::uint16_t>(0));
}

void run()
{
  test_1();
  test_2();
  test_3();
  test_4();
  test_5();
  test_6();
  test_7();
  test_8();
  test_9();
  test_10();
  test_11();
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  std::exception_ptr exception_ptr = nullptr;
  try {
    run();
  }
  catch (...) {
    exception_ptr = std::current_exception();
  }
  if (exception_ptr) {
    try {
      std::rethrow_exception(exception_ptr);
    }
    catch (const std::exception& exception) {
      std::cerr << "an error occurred: " << exception.what() << std::endl;
    }
  }
  return (exception_ptr ? -1 : 0);
}
