// See LICENSE file in the project root for license information.

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <rstream/core/buffer.hpp>
#include <rstream/core/detail/protobuf.hpp>
#include <rstream/io-rstrm/detail/handshake.hpp>
#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/test/stream_pair.hpp>

namespace protobuf = rstream::io_rstrm::protobuf;

using socket_type    = rstream::test::stream_socket;
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
  rstream::core::buffer buffer;
  assert(rstream::core::detail::serialize_protobuf_message(message, buffer));
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, true);
  rstream::io_rstrm::config config;
#ifdef RSTREAM_WITH_IO_STREAMS
  config.m_token     = "secret-token";
#else
  config.m_no_token  = true;
#endif
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
    const auto parsed = rstream::core::detail::parse_protobuf_message(message, buffer.map().get_const_data(), buffer.get_size());
    assert(parsed);
    assert(message.has_stream_req());
    assert(message.stream_req().tunnel_id_name() == "api");
    assert(message.stream_req().has_zero_rtt());
    assert(message.stream_req().zero_rtt().value());
#ifdef RSTREAM_WITH_IO_STREAMS
    assert(message.stream_req().client_details().has_token());
    assert(message.stream_req().client_details().token().value() == "secret-token");
#else
    assert(!message.stream_req().client_details().has_token());
#endif
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
    const auto parsed = rstream::core::detail::parse_protobuf_message(message, request.map().get_const_data(), request.get_size());
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

static void check_proxy_response_preserves_coalesced_payload()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
  test_stream stream(*socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token = true;
  config.m_zero_rtt = false;
  bool handshake_called = false;
  bool payload_called = false;
  std::array<char, 5> received{};
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  handshake.async_run(handshake_type::type::proxy_req, "stream-123", boost::none, [&](const boost::system::error_code& error_code) {
    handshake_called = true;
    assert(!error_code);
    boost::asio::async_read(*socket_a, boost::asio::buffer(received), [&](const boost::system::error_code& read_error, std::size_t size) {
      assert(!read_error);
      assert(size == received.size());
      payload_called = true;
    });
  });
  boost::asio::co_spawn(io_context.get_executor(), [socket = socket_b]() -> boost::asio::awaitable<void> {
    payloader_type payloader(*socket);
    auto request = rstream::core::make_buffer_allocated(4096);
    co_await payloader.async_recv(request, boost::asio::use_awaitable);
    protobuf::Message response;
    response.mutable_proxy_rsp();
    auto payload = serialize_message(response);
    const std::string application_payload = "world";
    const auto payload_size = static_cast<std::uint32_t>(payload.get_size());
    std::vector<std::uint8_t> wire(sizeof(payload_size) + payload_size + application_payload.size());
    wire[0] = static_cast<std::uint8_t>(payload_size >> 24);
    wire[1] = static_cast<std::uint8_t>(payload_size >> 16);
    wire[2] = static_cast<std::uint8_t>(payload_size >> 8);
    wire[3] = static_cast<std::uint8_t>(payload_size);
    std::memcpy(wire.data() + sizeof(payload_size), payload.map().get_const_data(), payload_size);
    std::memcpy(wire.data() + sizeof(payload_size) + payload_size, application_payload.data(), application_payload.size());
    co_await boost::asio::async_write(*socket, boost::asio::buffer(wire), boost::asio::use_awaitable);
    co_return; }, boost::asio::detached);
  io_context.run();
  assert(handshake_called);
  assert(payload_called);
  assert(std::string(received.data(), received.size()) == "world");
}

#ifdef RSTREAM_WITH_IO_STREAMS
static void check_proxy_secret_is_allowed_with_mtls_agent_auth()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
    const auto parsed = rstream::core::detail::parse_protobuf_message(message, request.map().get_const_data(), request.get_size());
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
#endif

static void check_unexpected_response_type_is_rejected()
{
  boost::asio::io_context io_context;
  auto socket_a = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b = std::make_shared<socket_type>(io_context.get_executor());
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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
  rstream::test::connect_stream_pair(*socket_a, *socket_b);
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

static void check_cancellation_reaches_the_transport()
{
  boost::asio::io_context io_context;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  rstream::test::connect_stream_pair(socket_a, socket_b);
  test_stream stream(socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token = true;
  config.m_zero_rtt = false;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer deadline(io_context, std::chrono::milliseconds(200));
  payloader_type receiver(socket_b);
  auto request          = rstream::core::make_buffer_allocated(4096);
  bool deadline_expired = false;
  bool handler_called   = false;
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      deadline_expired = true;
      socket_a.close();
    }
  });
  handshake.async_run(
      handshake_type::type::stream_req, "api", boost::none,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        handler_called = true;
        assert(!deadline_expired);
        assert(error_code == boost::asio::error::operation_aborted);
        deadline.cancel();
      }));
  receiver.async_recv(request, [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    boost::asio::post(io_context, [&] {
      cancellation.emit(boost::asio::cancellation_type::terminal);
    });
  });
  io_context.run();
  assert(handler_called);
  assert(!deadline_expired);
}

static void check_deferred_handshake_is_lazy()
{
  boost::asio::io_context io_context;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  rstream::test::connect_stream_pair(socket_a, socket_b);
  test_stream stream(socket_a, false);
  rstream::io_rstrm::config config;
  config.m_no_token = true;
  handshake_type handshake(stream, rstream::io::make_address("engine.example:443"), config);
  auto operation = handshake.async_run(
      handshake_type::type::stream_req, "api", std::string("forbidden-token"),
      boost::asio::deferred);
  assert(io_context.poll() == 0);
  io_context.restart();

  std::size_t completion_count = 0;
  std::move(operation)([&](const boost::system::error_code& error_code) {
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::protocol_error), rstream::io_rstrm::error::rstream_rstream_error_category().name());
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 1);
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
  check_proxy_response_preserves_coalesced_payload();
#ifdef RSTREAM_WITH_IO_STREAMS
  check_proxy_secret_is_allowed_with_mtls_agent_auth();
#endif
  check_unexpected_response_type_is_rejected();
  check_invalid_protobuf_response_is_rejected();
  check_cancellation_reaches_the_transport();
  check_deferred_handshake_is_lazy();
  return 0;
}
