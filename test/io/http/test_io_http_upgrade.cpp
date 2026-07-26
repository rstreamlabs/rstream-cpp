// See LICENSE file in the project root for license information.

#include <array>
#include <chrono>
#include <iostream>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include <rstream/core/crc32.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io/detail/http/upgrade.hpp>

static const std::size_t g_buffer_size = 200;

static void check_upgrade_predicates()
{
  namespace http = boost::beast::http;

  http::request<http::empty_body> request;
  request.method(http::verb::get);
  request.version(11);
  request.set(http::field::connection, "keep-alive, Upgrade");
  request.set(http::field::upgrade, "rstrm");
  assert(rstream::io::detail::http::is_upgrade_request(request));

  request.method(http::verb::post);
  assert(!rstream::io::detail::http::is_upgrade_request(request));
  request.method(http::verb::get);
  request.version(10);
  assert(!rstream::io::detail::http::is_upgrade_request(request));
  request.version(11);
  request.set(http::field::upgrade, "websocket");
  assert(!rstream::io::detail::http::is_upgrade_request(request));

  http::response<http::empty_body> response;
  response.version(11);
  response.result(http::status::switching_protocols);
  response.set(http::field::connection, "Upgrade");
  response.set(http::field::upgrade, "rstrm");
  assert(rstream::io::detail::http::is_upgrade_response(response));

  response.result(http::status::ok);
  assert(!rstream::io::detail::http::is_upgrade_response(response));
  response.result(http::status::switching_protocols);
  response.set(http::field::connection, "close");
  assert(!rstream::io::detail::http::is_upgrade_response(response));
}

static void check_handshake_rejects_non_upgrade_response()
{
  namespace http = boost::beast::http;

  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  auto socket_a      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_a     = std::make_shared<adaptor_type>(*socket_a);
  auto socket_b      = std::make_shared<socket_type>(io_context.get_executor());
  boost::asio::local::connect_pair(*socket_a, *socket_b);

  bool client_called = false;
  adaptor_a->async_handshake("host", "/", [&](const boost::system::error_code& error_code) {
    client_called = true;
    assert(error_code == http::error::bad_status);
  });

  auto response = std::make_shared<std::string>("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
  boost::asio::async_write(*socket_b, boost::asio::buffer(*response), [response](const boost::system::error_code& error_code, std::size_t) {
    assert(!error_code);
  });

  io_context.run();
  assert(client_called);
}

static void check_accept_rejects_non_upgrade_request()
{
  namespace http = boost::beast::http;

  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  auto socket_a      = std::make_shared<socket_type>(io_context.get_executor());
  auto socket_b      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_b     = std::make_shared<adaptor_type>(*socket_b);
  boost::asio::local::connect_pair(*socket_a, *socket_b);

  bool server_called = false;
  adaptor_b->async_accept([&](const boost::system::error_code& error_code) {
    server_called = true;
    assert(error_code == http::error::bad_status);
  });

  bool client_called = false;
  auto request       = std::make_shared<std::string>("GET / HTTP/1.1\r\nHost: host\r\n\r\n");
  boost::asio::async_write(*socket_a, boost::asio::buffer(*request), [request](const boost::system::error_code& error_code, std::size_t) {
    assert(!error_code);
  });
  auto response = std::make_shared<std::array<char, 512>>();
  socket_a->async_read_some(boost::asio::buffer(*response), [response, &client_called](const boost::system::error_code& error_code, std::size_t length) {
    assert(!error_code);
    std::string raw(response->data(), length);
    assert(raw.find("502") != std::string::npos);
    client_called = true;
  });

  io_context.run();
  assert(server_called);
  assert(client_called);
}

static void check_handshake_cancellation_reaches_the_transport()
{
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  adaptor_type adaptor(socket_a);
  boost::asio::local::connect_pair(socket_a, socket_b);
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer deadline(io_context, std::chrono::milliseconds(200));
  bool deadline_expired = false;
  bool handler_called   = false;
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      deadline_expired = true;
      socket_a.close();
    }
  });
  adaptor.async_handshake(
      "host", "/",
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        handler_called = true;
        assert(!deadline_expired);
        assert(error_code == boost::asio::error::operation_aborted);
        deadline.cancel();
      }));
  boost::asio::post(io_context, [&] {
    cancellation.emit(boost::asio::cancellation_type::terminal);
  });
  io_context.run();
  assert(handler_called);
  assert(!deadline_expired);
}

static void check_accept_cancellation_reaches_the_transport()
{
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  adaptor_type adaptor(socket_a);
  boost::asio::local::connect_pair(socket_a, socket_b);
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer deadline(io_context, std::chrono::milliseconds(200));
  bool deadline_expired = false;
  bool handler_called   = false;
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      deadline_expired = true;
      socket_a.close();
    }
  });
  adaptor.async_accept(
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        handler_called = true;
        assert(!deadline_expired);
        assert(error_code == boost::asio::error::operation_aborted);
        deadline.cancel();
      }));
  boost::asio::post(io_context, [&] {
    cancellation.emit(boost::asio::cancellation_type::terminal);
  });
  io_context.run();
  assert(handler_called);
  assert(!deadline_expired);
}

static void check_decorators_are_applied()
{
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  adaptor_type adaptor_a(socket_a);
  adaptor_type adaptor_b(socket_b);
  boost::asio::local::connect_pair(socket_a, socket_b);
  bool request_decorated  = false;
  bool response_decorated = false;
  bool client_called      = false;
  bool server_called      = false;
  adaptor_a.set_decorator(
      [&](adaptor_type::request_type& request) {
        request_decorated = true;
        request.set("X-Rstream-Test", "request");
      });
  adaptor_b.set_decorator(
      [&](adaptor_type::response_type& response) {
        response_decorated = true;
        response.set("X-Rstream-Test", "response");
      });
  adaptor_a.async_handshake("host", "/", [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    client_called = true;
  });
  adaptor_b.async_accept([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    server_called = true;
  });
  io_context.run();
  assert(request_decorated);
  assert(response_decorated);
  assert(client_called);
  assert(server_called);
}

static void check_request_decorator_exception_completes_handshake()
{
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  socket_type socket(io_context.get_executor());
  adaptor_type adaptor(socket);
  adaptor.set_decorator(
      [](adaptor_type::request_type&) {
        throw boost::system::system_error(rstream::io::error::code::unsupported_operation);
      });
  bool handler_called = false;
  adaptor.async_handshake("host", "/", [&](const boost::system::error_code& error_code) {
    handler_called = true;
    assert(error_code == rstream::io::error::code::unsupported_operation);
  });
  assert(!handler_called);
  io_context.run();
  assert(handler_called);
}

static void check_upgrade_roundtrip()
{
  rstream::core::log::enable_ansicolor_stdout_mt();
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  auto socket_a      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_a     = std::make_shared<adaptor_type>(*socket_a);
  auto socket_b      = std::make_shared<socket_type>(io_context.get_executor());
  auto adaptor_b     = std::make_shared<adaptor_type>(*socket_b);
  boost::asio::local::connect_pair(*socket_a, *socket_b);
  auto run_a = [socket = socket_a, adaptor = adaptor_a]() -> boost::asio::awaitable<void> {
    std::cout << "async handshake..." << std::endl;
    co_await adaptor->async_handshake("host", "/", boost::asio::use_awaitable);
    std::cout << "handshake OK" << std::endl;
    auto memory = rstream::core::make_memory_allocated(g_buffer_size);
    rstream::core::random_bytes(memory.get_data(), memory.get_size());
    std::uint32_t crc32_1 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
    std::cout << "async write..." << std::endl;
    co_await boost::asio::async_write(*socket, boost::asio::const_buffer(memory.get_const_data(), memory.get_size()), boost::asio::use_awaitable);
    std::cout << "async write OK" << std::endl;
    std::cout << "async read..." << std::endl;
    auto size = co_await boost::asio::async_read(*socket, boost::asio::mutable_buffer(memory.get_data(), memory.get_size()), boost::asio::use_awaitable);
    std::cout << "async read OK" << std::endl;
    assert(size == g_buffer_size);
    std::uint32_t crc32_2 = rstream::core::crc32(memory.get_const_data(), memory.get_size());
    assert(crc32_1 == crc32_2);
    socket->close();
    co_return;
  };
  auto run_b = [socket = socket_b, adaptor = adaptor_b]() -> boost::asio::awaitable<void> {
    co_await adaptor->async_accept(boost::asio::use_awaitable);
    auto memory = rstream::core::make_memory_allocated(g_buffer_size);
    auto size   = co_await boost::asio::async_read(*socket, boost::asio::mutable_buffer(memory.get_data(), memory.get_size()), boost::asio::use_awaitable);
    memory.set_size(size);
    co_await boost::asio::async_write(*socket, boost::asio::const_buffer(memory.get_const_data(), memory.get_size()), boost::asio::use_awaitable);
    socket->close();
    co_return;
  };
  boost::asio::co_spawn(io_context.get_executor(), run_a, boost::asio::detached);
  boost::asio::co_spawn(io_context.get_executor(), run_b, boost::asio::detached);
  io_context.run();
}

static void check_deferred_upgrade_is_lazy()
{
  boost::asio::io_context io_context;
  using socket_type  = boost::asio::local::stream_protocol::socket;
  using adaptor_type = rstream::io::detail::http::upgrade<socket_type&>;
  socket_type socket_a(io_context.get_executor());
  socket_type socket_b(io_context.get_executor());
  boost::asio::local::connect_pair(socket_a, socket_b);
  adaptor_type adaptor_a(socket_a);
  adaptor_type adaptor_b(socket_b);
  auto handshake_operation = adaptor_a.async_handshake("host", "/", boost::asio::deferred);
  auto accept_operation    = adaptor_b.async_accept(boost::asio::deferred);
  assert(io_context.poll() == 0);
  io_context.restart();

  std::size_t completion_count = 0;
  std::move(handshake_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++completion_count;
  });
  std::move(accept_operation)([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 2);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_upgrade_predicates();
  check_handshake_rejects_non_upgrade_response();
  check_accept_rejects_non_upgrade_request();
  check_handshake_cancellation_reaches_the_transport();
  check_accept_cancellation_reaches_the_transport();
  check_decorators_are_applied();
  check_request_decorator_exception_completes_handshake();
  check_upgrade_roundtrip();
  check_deferred_upgrade_is_lazy();
  return 0;
}
