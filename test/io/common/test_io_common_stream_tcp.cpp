// See LICENSE file in the project root for license information.

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/detail/stream/error.hpp>
#include <rstream/io/stream.hpp>

using tcp = boost::asio::ip::tcp;

static void assert_stream_error(const boost::system::error_code& actual, rstream::io::detail::stream::error::code expected)
{
  assert(actual.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(actual.value() == static_cast<int>(expected));
}

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static rstream::io::stream::endpoint resolve_one(boost::asio::io_context& io_context, const std::string& uri)
{
  rstream::io::stream::resolver resolver(io_context.get_executor());
  rstream::io::stream::resolver::results_type results;
  boost::system::error_code error_code;
  bool completed = false;
  resolver.async_resolve(uri, [&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type& resolved) {
    error_code = error;
    results    = resolved;
    completed  = true;
  });
  io_context.run();
  io_context.restart();
  assert(completed);
  assert(!error_code);
  assert(!results.empty());
  return results.front().endpoint();
}

static rstream::io::stream::resolver::results_type make_results(const std::vector<rstream::io::stream::endpoint>& endpoints)
{
  rstream::io::stream::resolver::results_type results;
  for (const auto& endpoint : endpoints) {
    results.emplace_back(endpoint, endpoint.get_url());
  }
  return results;
}

static void check_async_resolve_owns_uri_components()
{
  boost::asio::io_context io_context;
  rstream::io::stream::resolver resolver(io_context.get_executor());
  rstream::io::stream::resolver::results_type results;
  boost::system::error_code error_code;
  bool completed  = false;
  const auto port = std::to_string(unused_tcp_port());
  {
    std::string uri = "tcp://localhost:" + port;
    resolver.async_resolve(uri, [&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type& resolved) {
      error_code = error;
      results    = resolved;
      completed  = true;
    });
    uri.assign(4096, 'x');
  }
  std::vector<std::string> overwritten_storage(64, std::string(4096, 'y'));
  assert(!overwritten_storage.empty());
  io_context.run();
  assert(completed);
  assert(!error_code);
  assert(!results.empty());
  assert(results.front().url().host() == "localhost");
  assert(results.front().url().port() == port);
}

static void check_uninitialized_socket_operations_fail()
{
  boost::asio::io_context io_context;
  rstream::io::stream::stream_socket socket(io_context.get_executor());

  boost::system::error_code error_code;
  (void)socket.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
  assert(!socket.is_secure());

  std::array<char, 4> read_buffer{};
  const std::string write_buffer = "ping";
  bool saw_read                  = false;
  bool saw_write                 = false;
  socket.async_read_some(boost::asio::buffer(read_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    assert(size == 0);
    saw_read = true;
  });
  socket.async_write_some(boost::asio::buffer(write_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    assert(size == 0);
    saw_write = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_read);
  assert(saw_write);

  std::array<boost::asio::const_buffer, 0> empty_write{};
  std::array<boost::asio::mutable_buffer, 0> empty_read{};
  bool saw_empty_write = false;
  bool saw_empty_read  = false;
  socket.async_write_some(empty_write, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    saw_empty_write = true;
  });
  socket.async_read_some(empty_read, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    saw_empty_read = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_empty_write);
  assert(saw_empty_read);
}

static void check_socket_move_preserves_moved_from_invariants()
{
  boost::asio::io_context io_context;
  rstream::io::stream::stream_socket original(io_context.get_executor());
  rstream::io::stream::stream_socket moved(std::move(original));
  boost::system::error_code error_code;
  // A moved-from rstream socket has a documented, queryable empty state.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  (void)original.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
  error_code = {};
  (void)moved.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  rstream::io::stream::stream_socket assigned(io_context.get_executor());
  assigned   = std::move(moved);
  error_code = {};
  // A moved-from rstream socket has a documented, queryable empty state.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  (void)moved.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
  error_code = {};
  (void)assigned.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
}

static void check_completion_tokens_are_forwarded()
{
  boost::asio::io_context io_context;
  rstream::io::stream::resolver resolver(io_context.get_executor());
  auto invalid_uri = resolver.async_resolve("://bad", boost::asio::use_future);
  std::thread io_thread([&io_context]() { io_context.run(); });
  bool resolve_failed = false;
  try {
    (void)invalid_uri.get();
  }
  catch (const boost::system::system_error&) {
    resolve_failed = true;
  }
  io_thread.join();
  assert(resolve_failed);

  io_context.restart();
  rstream::io::stream::stream_socket socket(io_context.get_executor());
  std::array<boost::asio::const_buffer, 0> empty_write{};
  std::array<boost::asio::mutable_buffer, 0> empty_read{};
  auto write_result = socket.async_write_some(empty_write, boost::asio::use_future);
  auto read_result  = socket.async_read_some(empty_read, boost::asio::use_future);
  io_context.run();
  assert(write_result.get() == 0);
  assert(read_result.get() == 0);
}

static void check_deferred_completion_tokens_are_forwarded()
{
  boost::asio::io_context io_context;
  rstream::io::stream::resolver resolver(io_context.get_executor());
  auto resolve_operation = resolver.async_resolve("://bad", boost::asio::deferred);
  bool resolved          = false;
  std::move(resolve_operation)([&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type&) {
    assert(error);
    resolved = true;
  });
  assert(!resolved);
  io_context.run();
  assert(resolved);

  io_context.restart();
  rstream::io::stream::stream_socket socket(io_context.get_executor());
  std::array<boost::asio::const_buffer, 0> empty_write{};
  std::array<boost::asio::mutable_buffer, 0> empty_read{};
  auto write_operation         = socket.async_write_some(empty_write, boost::asio::deferred);
  auto read_operation          = socket.async_read_some(empty_read, boost::asio::deferred);
  std::size_t completion_count = 0;
  std::move(write_operation)([&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    ++completion_count;
  });
  std::move(read_operation)([&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 0);
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 2);
}

static void check_deferred_accept_and_connect_are_lazy()
{
  boost::asio::io_context io_context;
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);

  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  auto accept_operation        = acceptor.async_accept(server_peer, remote_endpoint, boost::asio::deferred);
  auto connect_operation       = client.async_connect(endpoint, boost::asio::deferred);
  auto owning_accept_operation = acceptor.async_accept(boost::asio::deferred);
  rstream::io::stream::stream_socket second_client(io_context.get_executor());
  auto second_connect_operation = second_client.async_connect(endpoint, boost::asio::deferred);
  assert(io_context.poll() == 0);
  io_context.restart();

  std::size_t completion_count = 0;
  std::move(accept_operation)([&](const boost::system::error_code& error) {
    assert(!error);
    ++completion_count;
  });
  std::move(connect_operation)([&](const boost::system::error_code& error) {
    assert(!error);
    ++completion_count;
  });
  std::move(owning_accept_operation)([&](const boost::system::error_code& error, rstream::io::stream::stream_socket peer) {
    assert(!error);
    boost::system::error_code remote_error;
    (void)peer.remote_endpoint(remote_error);
    assert(!remote_error);
    ++completion_count;
  });
  std::move(second_connect_operation)([&](const boost::system::error_code& error) {
    assert(!error);
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 4);
}

static void check_uninitialized_acceptor_operations_fail()
{
  boost::asio::io_context io_context;
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.bind(endpoint, error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  error_code = {};
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  error_code = {};
  (void)acceptor.local_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);

  rstream::io::stream::stream_socket peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  bool saw_accept = false;
  acceptor.async_accept(peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert_stream_error(error, rstream::io::detail::stream::error::code::uninitialized_object);
    saw_accept = true;
  });
  io_context.run();
  io_context.restart();
  assert(saw_accept);
}

static void check_tcp_accept_connect_and_transfer()
{
  boost::asio::io_context io_context;
  const auto port     = unused_tcp_port();
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port) + "?tcp.no_delay=true&tcp.keep_alive=true");

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);

  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  io_context.run();
  io_context.restart();
  assert(accepted);
  assert(connected);
  assert(!client.is_secure());
  assert(!server_peer.is_secure());

  std::array<char, 4> server_buffer{};
  bool server_read = false;
  bool client_sent = false;
  boost::asio::async_read(server_peer, boost::asio::buffer(server_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == server_buffer.size());
    assert(std::string(server_buffer.data(), server_buffer.size()) == "ping");
    server_read = true;
  });
  boost::asio::async_write(client, boost::asio::buffer(std::string("ping")), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 4);
    client_sent = true;
  });
  io_context.run();
  io_context.restart();
  assert(server_read);
  assert(client_sent);

  std::array<char, 4> client_buffer{};
  bool client_read = false;
  bool server_sent = false;
  boost::asio::async_read(client, boost::asio::buffer(client_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == client_buffer.size());
    assert(std::string(client_buffer.data(), client_buffer.size()) == "pong");
    client_read = true;
  });
  boost::asio::async_write(server_peer, boost::asio::buffer(std::string("pong")), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 4);
    server_sent = true;
  });
  io_context.run();
  io_context.restart();
  assert(client_read);
  assert(server_sent);

  error_code = {};
  client.close(error_code);
  assert(!error_code);
  error_code = {};
  (void)client.remote_endpoint(error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::uninitialized_object);
}

static void check_async_connect_rejects_empty_sequence()
{
  boost::asio::io_context io_context;
  rstream::io::stream::stream_socket socket(io_context.get_executor());
  rstream::io::stream::resolver::results_type results;
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  boost::asio::async_connect(socket, results, [&](const boost::system::error_code& error, const rstream::io::stream::endpoint&) {
    completion_error = error;
    ++completion_count;
  });
  assert(completion_count == 0);
  io_context.run();
  assert(completion_count == 1);
  assert(completion_error == boost::asio::error::not_found);
}

static void check_async_connect_falls_back_to_next_endpoint()
{
  boost::asio::io_context io_context;
  const auto unavailable = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  const auto available   = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(available, error_code);
  assert(!error_code);
  acceptor.bind(available, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  bool accepted = false;
  acceptor.async_accept(peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  rstream::io::stream::stream_socket socket(io_context.get_executor());
  const auto results = make_results({unavailable, available});
  boost::system::error_code completion_error;
  rstream::io::stream::endpoint connected_endpoint;
  std::size_t completion_count = 0;
  boost::asio::async_connect(socket, results, [&](const boost::system::error_code& error, const rstream::io::stream::endpoint& endpoint) {
    completion_error   = error;
    connected_endpoint = endpoint;
    ++completion_count;
  });
  io_context.run();
  assert(completion_count == 1);
  assert(!completion_error);
  assert(accepted);
  assert(connected_endpoint.to_string() == available.to_string());
}

static void check_async_connect_cancellation_does_not_fall_back()
{
  boost::asio::io_context io_context;
  const auto pending   = resolve_one(io_context, "tcp://192.0.2.1:65000");
  const auto available = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(available, error_code);
  assert(!error_code);
  acceptor.bind(available, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  bool accepted = false;
  acceptor.async_accept(peer, remote_endpoint, [&](const boost::system::error_code& error) {
    if (!error) {
      accepted = true;
    }
  });
  rstream::io::stream::stream_socket socket(io_context.get_executor());
  const auto results = make_results({pending, available});
  boost::asio::cancellation_signal cancellation;
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  boost::asio::async_connect(
      socket,
      results,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error, const rstream::io::stream::endpoint&) {
        completion_error = error;
        ++completion_count;
        acceptor.close();
      }));
  boost::asio::post(io_context, [&] { cancellation.emit(boost::asio::cancellation_type::all); });
  io_context.run();
  assert(completion_count == 1);
  assert(completion_error == boost::asio::error::operation_aborted);
  assert(!accepted);
}

static void check_tcp_accept_preserves_peer_executor()
{
  boost::asio::io_context io_context;
  boost::asio::io_context peer_io_context;
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket server_peer(peer_io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  io_context.run();
  assert(accepted);
  assert(connected);
  assert(server_peer.get_executor() == peer_io_context.get_executor());
}

static void check_concurrent_tcp_accepts_are_independent()
{
  constexpr std::size_t connection_count = 16;
  boost::asio::io_context io_context;
  const auto port     = unused_tcp_port();
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  std::vector<std::unique_ptr<rstream::io::stream::stream_socket>> peers;
  std::vector<std::unique_ptr<rstream::io::stream::stream_socket>> clients;
  std::vector<rstream::io::stream::endpoint> remote_endpoints(connection_count);
  peers.reserve(connection_count);
  clients.reserve(connection_count);
  std::atomic_size_t accepted  = 0;
  std::atomic_size_t connected = 0;
  std::atomic_bool completed   = false;
  bool deadline_expired        = false;
  boost::asio::steady_timer deadline(io_context, std::chrono::seconds(2));
  deadline.async_wait([&](const boost::system::error_code& error) {
    if (!error) {
      deadline_expired = true;
      acceptor.close();
    }
  });
  const auto finish_if_complete = [&] {
    if (accepted == connection_count && connected == connection_count && !completed.exchange(true)) {
      deadline.cancel();
    }
  };
  for (std::size_t index = 0; index < connection_count; ++index) {
    peers.push_back(std::make_unique<rstream::io::stream::stream_socket>(io_context.get_executor()));
    clients.push_back(std::make_unique<rstream::io::stream::stream_socket>(io_context.get_executor()));
    acceptor.async_accept(*peers[index], remote_endpoints[index], [&, index](const boost::system::error_code& error) {
      assert(!error);
      boost::system::error_code remote_error;
      (void)peers[index]->remote_endpoint(remote_error);
      assert(!remote_error);
      ++accepted;
      finish_if_complete();
    });
    clients[index]->async_connect(endpoint, [&](const boost::system::error_code& error) {
      assert(!error);
      ++connected;
      finish_if_complete();
    });
  }
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (std::size_t index = 0; index < 4; ++index) {
    threads.emplace_back([&io_context] {
      io_context.run();
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  assert(!deadline_expired);
  assert(accepted == connection_count);
  assert(connected == connection_count);
}

static void check_concurrent_tcp_accepts_return_independent_sockets()
{
  constexpr std::size_t connection_count = 16;
  boost::asio::io_context io_context;
  const auto port     = unused_tcp_port();
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  std::vector<std::unique_ptr<rstream::io::stream::stream_socket>> clients;
  clients.reserve(connection_count);
  std::atomic_size_t accepted  = 0;
  std::atomic_size_t connected = 0;
  std::atomic_bool completed   = false;
  bool deadline_expired        = false;
  boost::asio::steady_timer deadline(io_context, std::chrono::seconds(2));
  deadline.async_wait([&](const boost::system::error_code& error) {
    if (!error) {
      deadline_expired = true;
      acceptor.close();
    }
  });
  const auto finish_if_complete = [&] {
    if (accepted == connection_count && connected == connection_count && !completed.exchange(true)) {
      deadline.cancel();
    }
  };
  for (std::size_t index = 0; index < connection_count; ++index) {
    clients.push_back(std::make_unique<rstream::io::stream::stream_socket>(io_context.get_executor()));
    acceptor.async_accept([&](const boost::system::error_code& error, rstream::io::stream::stream_socket peer) {
      assert(!error);
      boost::system::error_code remote_error;
      (void)peer.remote_endpoint(remote_error);
      assert(!remote_error);
      ++accepted;
      finish_if_complete();
    });
    clients[index]->async_connect(endpoint, [&](const boost::system::error_code& error) {
      assert(!error);
      ++connected;
      finish_if_complete();
    });
  }
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (std::size_t index = 0; index < 4; ++index) {
    threads.emplace_back([&io_context] {
      io_context.run();
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  assert(!deadline_expired);
  assert(accepted == connection_count);
  assert(connected == connection_count);
}

static void check_owning_accept_survives_acceptor_wrapper_destruction()
{
  boost::asio::io_context io_context;
  const auto port     = unused_tcp_port();
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port));
  auto acceptor       = std::make_unique<rstream::io::stream::acceptor>(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor->open(endpoint, error_code);
  assert(!error_code);
  acceptor->bind(endpoint, error_code);
  assert(!error_code);
  acceptor->listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  bool accepted         = false;
  bool connected        = false;
  bool completed        = false;
  bool deadline_expired = false;
  boost::asio::steady_timer deadline(io_context, std::chrono::seconds(2));
  const auto finish_if_complete = [&] {
    if (accepted && connected && !completed) {
      completed = true;
      deadline.cancel();
    }
  };
  acceptor->async_accept([&](const boost::system::error_code& error, rstream::io::stream::stream_socket peer) {
    assert(!error);
    boost::system::error_code remote_error;
    (void)peer.remote_endpoint(remote_error);
    assert(!remote_error);
    accepted = true;
    finish_if_complete();
  });
  acceptor.reset();
  rstream::io::stream::stream_socket client(io_context.get_executor());
  client.async_connect(endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
    finish_if_complete();
  });
  deadline.async_wait([&](const boost::system::error_code& error) {
    if (!error) {
      deadline_expired = true;
      io_context.stop();
    }
  });
  io_context.run();
  assert(!deadline_expired);
  assert(accepted);
  assert(connected);
}

static void check_owning_accept_propagates_cancellation()
{
  boost::asio::io_context io_context;
  const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(endpoint, error_code);
  assert(!error_code);
  acceptor.bind(endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  boost::asio::cancellation_signal cancellation;
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  bool deadline_expired        = false;
  boost::asio::steady_timer deadline(io_context, std::chrono::seconds(2));
  acceptor.async_accept(
      boost::asio::bind_cancellation_slot(
          cancellation.slot(),
          [&](const boost::system::error_code& error, rstream::io::stream::stream_socket peer) {
            completion_error = error;
            ++completion_count;
            deadline.cancel();
            boost::system::error_code remote_error;
            (void)peer.remote_endpoint(remote_error);
            assert_stream_error(remote_error, rstream::io::detail::stream::error::code::uninitialized_object);
          }));
  boost::asio::steady_timer cancel_timer(io_context, std::chrono::milliseconds(10));
  cancel_timer.async_wait([&](const boost::system::error_code& error) {
    if (!error) {
      cancellation.emit(boost::asio::cancellation_type::terminal);
    }
  });
  deadline.async_wait([&](const boost::system::error_code& error) {
    if (!error && completion_count == 0) {
      deadline_expired = true;
      acceptor.close();
    }
  });
  io_context.run();
  assert(!deadline_expired);
  assert(completion_count == 1);
  assert(completion_error == boost::asio::error::operation_aborted);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_async_resolve_owns_uri_components();
  check_uninitialized_socket_operations_fail();
  check_socket_move_preserves_moved_from_invariants();
  check_completion_tokens_are_forwarded();
  check_deferred_completion_tokens_are_forwarded();
  check_deferred_accept_and_connect_are_lazy();
  check_uninitialized_acceptor_operations_fail();
  check_tcp_accept_connect_and_transfer();
  check_async_connect_rejects_empty_sequence();
  check_async_connect_falls_back_to_next_endpoint();
  check_async_connect_cancellation_does_not_fall_back();
  check_tcp_accept_preserves_peer_executor();
  check_concurrent_tcp_accepts_are_independent();
  check_concurrent_tcp_accepts_return_independent_sockets();
  check_owning_accept_survives_acceptor_wrapper_destruction();
  check_owning_accept_propagates_cancellation();
  return 0;
}
