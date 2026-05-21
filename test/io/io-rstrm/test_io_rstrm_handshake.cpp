// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/io-rstrm/detail/handshake.hpp>
#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>

namespace protobuf = rstream::io_rstrm::protobuf;

using socket_type    = boost::asio::local::stream_protocol::socket;
using payloader_type = rstream::io::payloader<socket_type&>;

class test_stream {
 public:
  using executor_type = socket_type::executor_type;
  test_stream(socket_type& socket, bool secure)
      : m_socket(socket),
        m_secure(secure)
  {
  }
  executor_type get_executor() const
  {
    return m_socket.get_executor();
  }
  bool is_secure() const
  {
    return m_secure;
  }
  template <typename buffer_sequence, typename handler_type>
  auto async_write_some(const buffer_sequence& buffers, handler_type&& handler)
  {
    return m_socket.async_write_some(buffers, std::forward<handler_type>(handler));
  }
  template <typename buffer_sequence, typename handler_type>
  auto async_read_some(const buffer_sequence& buffers, handler_type&& handler)
  {
    return m_socket.async_read_some(buffers, std::forward<handler_type>(handler));
  }

 private:
  socket_type& m_socket;
  bool m_secure;
};

using handshake_type = rstream::io_rstrm::detail::handshake<test_stream&>;

static void assert_error_code(const boost::system::error_code& actual, int value, const char* category)
{
  if (actual.value() != value || std::string(actual.category().name()) != category) {
    std::cerr << "unexpected error code: value=" << actual.value()
              << " category=" << actual.category().name()
              << " message=" << actual.message() << std::endl;
    assert(false);
  }
}

static rstream::core::buffer serialize_message(const protobuf::Message& message)
{
  auto buffer = rstream::core::make_buffer_allocated(message.ByteSizeLong());
  message.SerializeToArray(buffer.map().get_data(), buffer.get_size());
  return buffer;
}

static void check_token_is_rejected_on_insecure_stream()
{
  boost::asio::io_context io_context;
  socket_type socket(io_context.get_executor());
  test_stream stream(socket, false);
  rstream::io_rstrm::config config;
  config.m_token = "secret-token";
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  bool called = false;
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::protocol_error), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  io_context.run();
  assert(called);
}

static void check_zero_rtt_stream_request_does_not_wait_for_response()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, true);
  rstream::io_rstrm::config config;
  config.m_token     = "secret-token";
  config.m_zero_rtt  = true;
  bool client_called = false;
  bool server_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert(!error_code);
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b, &server_called]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto buffer = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(buffer, boost::asio::use_awaitable);
    protobuf::Message message;
    const auto parsed = message.ParseFromArray(buffer.map().get_const_data(), buffer.get_size());
    assert(parsed);
    assert(message.has_stream_req());
    assert(message.stream_req().tunnel_id_name() == "api");
    assert(message.stream_req().has_zero_rtt());
    assert(message.stream_req().zero_rtt().value());
    assert(message.stream_req().client_details().has_token());
    assert(message.stream_req().client_details().token().value() == "secret-token");
    server_called = true;
    socket->close();
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
  assert(server_called);
}

static void check_stream_response_must_have_payload()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::protocol_error), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    rstream::core::buffer empty_response;
    co_await payloader.async_send(empty_response, boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
}

static void check_stream_response_error_is_mapped()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::unauthorized), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message response;
    response.mutable_stream_rsp()->mutable_error()->set_code(protobuf::ERROR_CODE_UNAUTHORIZED);
    auto payload = serialize_message(response);
    co_await payloader.async_send(payload, boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
}

static void check_stream_success_response_completes()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert(!error_code);
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message response;
    response.mutable_stream_rsp()->set_stream_id("stream-123");
    auto payload = serialize_message(response);
    co_await payloader.async_send(payload, boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
}

static void check_proxy_success_response_completes()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  bool server_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::proxy_req, "stream-123", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert(!error_code);
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b, &server_called]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message message;
    const auto parsed = message.ParseFromArray(request.map().get_const_data(), request.get_size());
    assert(parsed);
    assert(message.has_proxy_req());
    assert(message.proxy_req().stream_id() == "stream-123");
    assert(!message.proxy_req().has_zero_rtt());
    protobuf::Message response;
    response.mutable_proxy_rsp();
    auto payload = serialize_message(response);
    co_await payloader.async_send(payload, boost::asio::use_awaitable);
    server_called = true;
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
  assert(server_called);
}

static void check_proxy_secret_is_allowed_with_mtls_agent_auth()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, true);
  rstream::io_rstrm::config config;
  config.m_token     = "agent-token";
  config.m_zero_rtt  = false;
  bool client_called = false;
  bool server_called = false;
  handshake_type handshake(stream, rstream::io::make_address("tcp://engine.example:443?ssl&ssl.cert_file=client.pem&ssl.key_file=client-key.pem"), config);
  handshake.async_run(handshake_type::type::proxy_req, "stream-123", std::string("proxy-secret"), [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert(!error_code);
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b, &server_called]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message message;
    const auto parsed = message.ParseFromArray(request.map().get_const_data(), request.get_size());
    assert(parsed);
    assert(message.has_proxy_req());
    assert(message.proxy_req().stream_id() == "stream-123");
    assert(message.proxy_req().client_details().token().value() == "proxy-secret");
    protobuf::Message response;
    response.mutable_proxy_rsp();
    auto payload = serialize_message(response);
    co_await payloader.async_send(payload, boost::asio::use_awaitable);
    server_called = true;
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
  assert(server_called);
}

static void check_unexpected_response_type_is_rejected()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::protocol_error), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message response;
    response.mutable_proxy_rsp();
    auto payload = serialize_message(response);
    co_await payloader.async_send(payload, boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
}

static void check_invalid_protobuf_response_is_rejected()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token  = true;
  config.m_zero_rtt  = false;
  bool client_called = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::stream_req, "api", boost::none, [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::protocol_error), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    auto invalid = rstream::core::make_buffer_allocated(1);
    static_cast<std::uint8_t*>(invalid.map().get_data())[0] = 0xff;
    co_await payloader.async_send(invalid, boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(client_called);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_token_is_rejected_on_insecure_stream();
  check_zero_rtt_stream_request_does_not_wait_for_response();
  check_stream_response_must_have_payload();
  check_stream_response_error_is_mapped();
  check_stream_success_response_completes();
  check_proxy_success_response_completes();
  check_proxy_secret_is_allowed_with_mtls_agent_auth();
  check_unexpected_response_type_is_rejected();
  check_invalid_protobuf_response_is_rejected();
  return 0;
}
