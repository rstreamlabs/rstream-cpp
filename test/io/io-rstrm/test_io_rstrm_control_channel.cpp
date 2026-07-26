// See LICENSE file in the project root for license information.

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/system_error.hpp>

#include <arpa/inet.h>

#include <rstream/io-rstrm/acceptor.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/detail/stable_domain.hpp>
#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/test/time.hpp>

namespace protobuf = rstream::io_rstrm::protobuf;
using tcp          = boost::asio::ip::tcp;

static constexpr unsigned int kControlChannelTimeoutMs = rstream::test::timeout_ms(10000);
static constexpr auto kFakeEngineIoTimeout             = rstream::test::timeout(std::chrono::seconds(10));
static constexpr auto kWatchdogTimeout                 = rstream::test::timeout(std::chrono::seconds(30));

[[noreturn]] static void fail(const std::string& message)
{
  std::cerr << message << std::endl;
  std::abort();
}

static void check(bool condition, const std::string& message)
{
  if (!condition) {
    fail(message);
  }
}

static tcp::socket accept_connection(tcp::acceptor& acceptor)
{
  const auto was_non_blocking = acceptor.non_blocking();
  boost::system::error_code error_code;
  acceptor.non_blocking(true, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  tcp::socket socket(acceptor.get_executor());
  const auto deadline = std::chrono::steady_clock::now() + kFakeEngineIoTimeout;
  for (;;) {
    acceptor.accept(socket, error_code);
    if (!error_code) {
      break;
    }
    if (error_code != boost::asio::error::would_block && error_code != boost::asio::error::try_again) {
      const auto accept_error = error_code;
      acceptor.non_blocking(was_non_blocking, error_code);
      throw boost::system::system_error(accept_error);
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      acceptor.non_blocking(was_non_blocking, error_code);
      throw std::runtime_error("timed out waiting for the SDK to connect to the fake engine");
    }
    error_code.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  acceptor.non_blocking(was_non_blocking, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return socket;
}

static void read_exact(tcp::socket& socket, void* data, std::size_t size)
{
  const auto was_non_blocking = socket.non_blocking();
  boost::system::error_code error_code;
  socket.non_blocking(true, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  const auto deadline = std::chrono::steady_clock::now() + kFakeEngineIoTimeout;
  std::size_t read    = 0;
  while (read < size) {
    const auto bytes_read = socket.read_some(boost::asio::buffer(static_cast<char*>(data) + read, size - read), error_code);
    if (!error_code) {
      read += bytes_read;
      continue;
    }
    if (error_code != boost::asio::error::would_block && error_code != boost::asio::error::try_again) {
      const auto read_error = error_code;
      socket.non_blocking(was_non_blocking, error_code);
      throw boost::system::system_error(read_error);
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      socket.non_blocking(was_non_blocking, error_code);
      throw std::runtime_error("timed out waiting for framed protobuf data");
    }
    error_code.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  socket.non_blocking(was_non_blocking, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

static protobuf::Message read_message(tcp::socket& socket)
{
  std::uint32_t network_size = 0;
  read_exact(socket, &network_size, sizeof(network_size));
  const auto size = ntohl(network_size);
  check(size <= 1024 * 1024, "framed protobuf message is too large");
  std::vector<char> payload(size);
  if (size > 0) {
    read_exact(socket, payload.data(), payload.size());
  }
  protobuf::Message message;
  check(message.ParseFromArray(payload.data(), static_cast<int>(payload.size())), "failed to parse protobuf message");
  return message;
}

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  const auto size          = static_cast<std::uint32_t>(message.ByteSizeLong());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  std::vector<char> payload(size);
  check(message.SerializeToArray(payload.data(), static_cast<int>(payload.size())), "failed to serialize protobuf message");
  if (!payload.empty()) {
    boost::asio::write(socket, boost::asio::buffer(payload));
  }
}

static void wait_for_peer_close(tcp::socket& socket)
{
  for (;;) {
    try {
      const auto message = read_message(socket);
      check(message.has_heartbeat(), "unexpected message while waiting for the peer to close");
    }
    catch (const boost::system::system_error& error) {
      check(error.code() == boost::asio::error::eof || error.code() == boost::asio::error::connection_reset, "unexpected transport error while waiting for the peer to close");
      return;
    }
  }
}

static void wait_until_ready(const std::atomic_bool& ready, const std::string& timeout_message)
{
  const auto deadline = std::chrono::steady_clock::now() + kFakeEngineIoTimeout;
  while (!ready.load(std::memory_order_acquire)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error(timeout_message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

static protobuf::Message open_control_response()
{
  protobuf::Message response;
  auto* ok = response.mutable_open_control_channel_rsp()->mutable_ok();
  ok->set_client_id("client-1");
  ok->mutable_server_details()->mutable_plan()->set_value("enterprise");
  ok->mutable_server_details()->mutable_provider()->set_value("test");
  ok->mutable_server_details()->mutable_region()->set_value("local");
  return response;
}

static protobuf::Message open_tunnel_response(const protobuf::OpenTunnelReq& request)
{
  protobuf::Message response;
  auto* payload = response.mutable_open_tunnel_rsp();
  payload->set_request_id(request.request_id());
  auto* properties = payload->mutable_tunnel_properties();
  properties->mutable_id()->set_value("tunnel-1");
  properties->mutable_name()->set_value(request.tunnel_properties().name().value());
  properties->mutable_type()->set_value("bytestream");
  properties->mutable_publish()->set_value(true);
  properties->mutable_protocol()->set_value("http");
  properties->mutable_hostname()->set_value("api.t.localhost.rstream.test");
  properties->mutable_port()->set_value(443);
  return response;
}

static protobuf::Message close_control_response()
{
  protobuf::Message response;
  response.mutable_close_control_channel_rsp();
  return response;
}

static protobuf::Message close_tunnel_response(const std::string& tunnel_id)
{
  protobuf::Message response;
  response.mutable_close_tunnel_rsp()->set_tunnel_id(tunnel_id);
  return response;
}

static protobuf::Message stream_response(const std::string& stream_id)
{
  protobuf::Message response;
  response.mutable_stream_rsp()->set_stream_id(stream_id);
  return response;
}

class fake_engine {
 public:
  using script_type       = std::function<void(tcp::socket&)>;
  using multi_script_type = std::function<void(tcp::acceptor&)>;

  fake_engine()
      : m_io_context(),
        m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0))
  {
  }

  ~fake_engine()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  std::string address() const
  {
    return std::string("127.0.0.1:") + std::to_string(port());
  }

  void start(script_type script)
  {
    m_thread = std::thread([this, script = std::move(script)] {
      try {
        auto socket = accept_connection(m_acceptor);
        script(socket);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void start_multi(multi_script_type script)
  {
    m_thread = std::thread([this, script = std::move(script)] {
      try {
        script(m_acceptor);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class watchdog {
 public:
  explicit watchdog(boost::asio::io_context& io_context)
      : m_thread([this, &io_context] {
          std::unique_lock<std::mutex> lock(m_mutex);
          m_started = true;
          m_cv.notify_one();
          if (m_cv.wait_for(lock, kWatchdogTimeout, [this] { return m_completed; })) {
            return;
          }
          m_timed_out = true;
          io_context.stop();
        })
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_started; });
  }

  ~watchdog()
  {
    complete();
  }

  void complete()
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_completed = true;
    }
    m_cv.notify_one();
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  bool timed_out() const
  {
    return m_timed_out.load();
  }

 private:
  std::atomic_bool m_timed_out = false;
  bool m_started               = false;
  bool m_completed             = false;
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::thread m_thread;
};

static boost::filesystem::path write_config_file(const std::string& content)
{
  auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("rstream-control-channel-%%%%-%%%%.yaml");
  std::ofstream stream(path.string());
  check(stream.is_open(), "failed to create temporary configuration file");
  stream << content;
  stream.close();
  check(static_cast<bool>(stream), "failed to write temporary configuration file");
  return path;
}

static void check_client_snapshots_configuration_at_construction()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    check(open_request.has_open_control_channel_req(), "missing open control channel request");
    const auto& details = open_request.open_control_channel_req().client_details();
    check(!details.has_token(), "unexpected token in configuration snapshot");
    write_message(socket, open_control_response());
    auto close_request = read_message(socket);
    check(close_request.has_close_control_channel_req(), "missing close control channel request");
    write_message(socket, close_control_response());
  });

  const auto path = write_config_file(
      "defaults:\n"
      "  context:\n"
      "    name: snapshot\n"
      "contexts:\n"
      "  - name: snapshot\n"
      "    engine: tcp://"
      + engine.address() + "\n");
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_config_path           = path.string();
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  boost::filesystem::remove(path);

  bool connected = false;
  bool closed    = false;
  watchdog test_watchdog(io_context);
  client.async_connect([&](const boost::system::error_code& error_code) {
    check(!error_code, "client did not use its construction-time engine configuration: " + error_code.message());
    connected = true;
    client.close();
  });
  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    check(!error_code, "client did not close cleanly");
    closed = true;
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  check(!callback_error, "failed to set control callbacks");

  io_context.run();
  test_watchdog.complete();
  engine.join();

  check(!test_watchdog.timed_out(), "configuration snapshot check timed out");
  check(connected, "client never connected with its configuration snapshot");
  check(closed, "client never closed after the configuration snapshot check");
}

static void check_client_can_create_and_close_tunnel()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    assert(!open_request.open_control_channel_req().client_details().has_token());
    write_message(socket, open_control_response());

    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    assert(!tunnel_request.open_tunnel_req().request_id().empty());
    assert(tunnel_request.open_tunnel_req().tunnel_properties().name().value() == "api");
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));

    auto close_tunnel_request = read_message(socket);
    assert(close_tunnel_request.has_close_tunnel_req());
    assert(close_tunnel_request.close_tunnel_req().tunnel_id() == "tunnel-1");
    write_message(socket, close_tunnel_response("tunnel-1"));

    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  config.m_heartbeat_interval_ms = 0;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool saw_connected     = false;
  bool saw_tunnel        = false;
  bool saw_disconnected  = false;
  bool saw_server_status = false;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_status_cb = [&](const rstream::io_rstrm::status& status) {
    saw_server_status = true;
    assert(status.m_plan && status.m_plan.value() == "enterprise");
    assert(status.m_provider && status.m_provider.value() == "test");
    assert(status.m_region && status.m_region.value() == "local");
  };
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    saw_disconnected = true;
    assert(!error_code);
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);

  auto connect_operation = client.async_connect(rstream::io::make_address(engine.address()), boost::asio::deferred);
  std::move(connect_operation)([&](const boost::system::error_code& error_code) {
    if (error_code) {
      std::cerr << "client connection failed: " << error_code.category().name() << ":" << error_code.value() << " " << error_code.message() << std::endl;
    }
    assert(!error_code);
    saw_connected = true;
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name     = "api";
    properties.m_type     = "bytestream";
    properties.m_publish  = true;
    auto create_operation = client.async_create_tunnel(properties, boost::asio::deferred);
    std::move(create_operation)([&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(!create_error);
      assert(tunnel);
      saw_tunnel = true;
      boost::system::error_code endpoint_error;
      const auto endpoint = tunnel.local_endpoint(endpoint_error);
      assert(!endpoint_error);
      assert(endpoint.m_id_name && endpoint.m_id_name.value() == "tunnel-1");
      boost::system::error_code properties_error;
      const auto current_properties = tunnel.properties(properties_error);
      assert(!properties_error);
      assert(current_properties.m_id && current_properties.m_id.value() == "tunnel-1");
      assert(current_properties.m_hostname && current_properties.m_hostname.value() == "api.t.localhost.rstream.test");
      tunnel.close();
      client.close();
    });
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(saw_connected);
  assert(saw_tunnel);
  assert(saw_disconnected);
  assert(saw_server_status);
}

static void check_client_rejects_operations_before_connection()
{
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token = true;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  boost::system::error_code address_error;
  auto address = client.address(address_error);
  (void)address;
  assert(address_error == rstream::io_rstrm::error::code::invalid_state);

  bool create_rejected = false;
  rstream::io_rstrm::tunnel_properties properties;
  properties.m_type = "bytestream";
  client.async_create_tunnel(properties, [&](const boost::system::error_code& error_code, rstream::io_rstrm::tunnel tunnel) {
    assert(error_code == rstream::io_rstrm::error::code::invalid_state);
    assert(!tunnel);
    create_rejected = true;
  });

  io_context.run();
  assert(create_rejected);
}

static void check_client_connect_honors_immediate_cancellation()
{
  fake_engine engine;
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = 100;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  boost::asio::cancellation_signal cancellation;
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  client.async_connect(
      rstream::io::make_address(engine.address()),
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        completion_error = error_code;
        ++completion_count;
      }));
  cancellation.emit(boost::asio::cancellation_type::all);
  io_context.run();
  assert(completion_count == 1);
  assert(completion_error == boost::asio::error::operation_aborted);
}

static void check_client_create_tunnel_honors_cancellation()
{
  fake_engine engine;
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  boost::asio::cancellation_signal cancellation;
  boost::system::error_code completion_error;
  std::size_t completion_count            = 0;
  std::atomic_bool cancellation_completed = false;
  engine.start([&](tcp::socket& socket) {
    auto open_request = read_message(socket);
    check(open_request.has_open_control_channel_req(), "missing open control channel request");
    write_message(socket, open_control_response());
    auto tunnel_request = read_message(socket);
    check(tunnel_request.has_open_tunnel_req(), "missing open tunnel request");
    boost::asio::post(io_context, [&cancellation] { cancellation.emit(boost::asio::cancellation_type::all); });
    wait_until_ready(cancellation_completed, "tunnel creation cancellation did not complete");
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));
    auto close_tunnel_request = read_message(socket);
    check(close_tunnel_request.has_close_tunnel_req(), "client did not close the tunnel created after cancellation");
    check(close_tunnel_request.close_tunnel_req().tunnel_id() == "tunnel-1", "client closed the wrong tunnel after cancellation");
    write_message(socket, close_tunnel_response("tunnel-1"));
    boost::asio::post(io_context, [&client] { client.close(); });
    auto close_request = read_message(socket);
    check(close_request.has_close_control_channel_req(), "missing close control channel request");
    write_message(socket, close_control_response());
  });
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    check(!error_code, "client failed to connect");
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name = "api";
    client.async_create_tunnel(
        properties,
        boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
          completion_error = create_error;
          ++completion_count;
          if (tunnel) {
            tunnel.close();
          }
          cancellation_completed.store(true, std::memory_order_release);
        }));
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  check(!test_watchdog.timed_out(), "tunnel creation cancellation check timed out");
  check(completion_count == 1, "tunnel creation cancellation completed more than once");
  check(completion_error == boost::asio::error::operation_aborted, "tunnel creation cancellation returned the wrong error");
}

static void check_tunnel_accept_honors_cancellation()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));
    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint endpoint;
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer cancellation_timer(io_context);
  boost::asio::steady_timer close_timer(io_context);
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name = "api";
    client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(!create_error);
      assert(tunnel);
      auto accept_operation = tunnel.async_accept(peer, endpoint, boost::asio::deferred);
      std::move(accept_operation)(
          boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& accept_error) {
            completion_error = accept_error;
            ++completion_count;
          }));
      cancellation_timer.expires_after(std::chrono::milliseconds(20));
      cancellation_timer.async_wait([&](const boost::system::error_code& timer_error) {
        assert(!timer_error);
        cancellation.emit(boost::asio::cancellation_type::all);
      });
      close_timer.expires_after(std::chrono::milliseconds(80));
      close_timer.async_wait([&](const boost::system::error_code& timer_error) {
        assert(!timer_error);
        client.close();
      });
    });
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(completion_count == 1);
  assert(completion_error == boost::asio::error::operation_aborted);
}

static void check_client_normalizes_published_tcp_tunnel()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    const auto& properties = tunnel_request.open_tunnel_req().tunnel_properties();
    assert(properties.protocol().value() == rstream::io_rstrm::protocol::tcp);
    assert(properties.type().value() == "bytestream");
    assert(properties.publish().value());
    assert(properties.port().value() == 10042);
    assert(properties.allow_cross_region_routing().value());
    auto response = open_tunnel_response(tunnel_request.open_tunnel_req());
    response.mutable_open_tunnel_rsp()->mutable_tunnel_properties()->mutable_protocol()->set_value(rstream::io_rstrm::protocol::tcp);
    response.mutable_open_tunnel_rsp()->mutable_tunnel_properties()->mutable_port()->set_value(10042);
    write_message(socket, response);
    auto close_tunnel_request = read_message(socket);
    assert(close_tunnel_request.has_close_tunnel_req());
    write_message(socket, close_tunnel_response("tunnel-1"));
    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  config.m_heartbeat_interval_ms = 0;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  bool created = false;
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name                       = "ssh";
    properties.m_protocol                   = rstream::io_rstrm::protocol::tcp;
    properties.m_port                       = 10042;
    properties.m_allow_cross_region_routing = true;
    client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(!create_error);
      assert(tunnel);
      created = true;
      tunnel.close();
      client.close();
    });
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(created);
}

static void check_client_rejects_invalid_operations_after_connection()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());

    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    assert(tunnel_request.open_tunnel_req().tunnel_properties().protocol().value() == rstream::io_rstrm::protocol::http);
    assert(tunnel_request.open_tunnel_req().tunnel_properties().allow_cross_region_routing().value());
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));

    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool connected                 = false;
  bool callbacks_rejected        = false;
  bool second_connect_rejected   = false;
  bool invalid_tunnel_rejected   = false;
  bool invalid_tcp_rejected      = false;
  bool cross_region_routing_sent = false;
  watchdog test_watchdog(io_context);

  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    connected = true;

    boost::system::error_code address_error;
    auto address = client.address(address_error);
    assert(!address_error);
    assert(address.host() == "127.0.0.1");

    rstream::io_rstrm::client::control_callbacks callbacks;
    boost::system::error_code callbacks_error;
    client.set_control_callbacks(callbacks, callbacks_error);
    assert(callbacks_error == rstream::io_rstrm::error::code::invalid_state);
    callbacks_rejected = true;

    client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& connect_error) {
      assert(connect_error == rstream::io_rstrm::error::code::invalid_state);
      second_connect_rejected = true;
    });

    rstream::io_rstrm::tunnel_properties properties;
    properties.m_type = "datagram";
    client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(create_error == rstream::io_rstrm::error::code::invalid_configuration);
      assert(!tunnel);
      invalid_tunnel_rejected = true;
      rstream::io_rstrm::tunnel_properties tcp_properties;
      tcp_properties.m_protocol = rstream::io_rstrm::protocol::tcp;
      tcp_properties.m_hostname = "ssh.example.test";
      client.async_create_tunnel(tcp_properties, [&](const boost::system::error_code& tcp_error, rstream::io_rstrm::tunnel tcp_tunnel) {
        assert(tcp_error == rstream::io_rstrm::error::code::invalid_configuration);
        assert(!tcp_tunnel);
        invalid_tcp_rejected = true;
        rstream::io_rstrm::tunnel_properties routing_properties;
        routing_properties.m_protocol                   = rstream::io_rstrm::protocol::http;
        routing_properties.m_allow_cross_region_routing = true;
        client.async_create_tunnel(routing_properties, [&](const boost::system::error_code& routing_error, rstream::io_rstrm::tunnel routing_tunnel) {
          assert(!routing_error);
          assert(routing_tunnel);
          cross_region_routing_sent = true;
          client.close();
        });
      });
    });
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(connected);
  assert(callbacks_rejected);
  assert(second_connect_rejected);
  assert(invalid_tunnel_rejected);
  assert(invalid_tcp_rejected);
  assert(cross_region_routing_sent);
}

static void check_client_rejects_malformed_open_response()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    protobuf::Message malformed;
    malformed.mutable_open_control_channel_rsp()->mutable_ok();
    write_message(socket, malformed);
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool rejected = false;
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::protocol_error);
    rejected = true;
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(rejected);
}

static void check_client_reports_open_response_error()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    protobuf::Message rejected;
    auto* error = rejected.mutable_open_control_channel_rsp()->mutable_error();
    error->set_code(protobuf::ErrorCode::ERROR_CODE_UNAUTHORIZED);
    error->mutable_message()->set_value("blocked");
    write_message(socket, rejected);
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool rejected = false;
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::unauthorized);
    rejected = true;
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(rejected);
}

static void check_client_rejects_empty_open_response()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    protobuf::Message empty;
    empty.mutable_open_control_channel_rsp();
    write_message(socket, empty);
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool rejected = false;
  watchdog test_watchdog(io_context);
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::protocol_error);
    rejected = true;
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(rejected);
}

static protobuf::Message tunnel_response_with_missing_id(const protobuf::OpenTunnelReq& request)
{
  protobuf::Message response;
  auto* payload = response.mutable_open_tunnel_rsp();
  payload->set_request_id(request.request_id());
  payload->mutable_tunnel_properties()->mutable_type()->set_value("bytestream");
  return response;
}

static protobuf::Message tunnel_response_with_invalid_type(const protobuf::OpenTunnelReq& request)
{
  auto response = open_tunnel_response(request);
  response.mutable_open_tunnel_rsp()->mutable_tunnel_properties()->mutable_type()->set_value("datagram");
  return response;
}

static protobuf::Message tunnel_response_with_error(const protobuf::OpenTunnelReq& request)
{
  protobuf::Message response;
  auto* payload = response.mutable_open_tunnel_rsp();
  payload->set_request_id(request.request_id());
  payload->mutable_error()->set_code(protobuf::ErrorCode::ERROR_CODE_UNAUTHORIZED);
  return response;
}

static protobuf::Message tunnel_response_without_payload(const protobuf::OpenTunnelReq& request)
{
  protobuf::Message response;
  response.mutable_open_tunnel_rsp()->set_request_id(request.request_id());
  return response;
}

static protobuf::Message tunnel_response_with_unknown_request_id(const protobuf::OpenTunnelReq& request)
{
  auto response = open_tunnel_response(request);
  response.mutable_open_tunnel_rsp()->set_request_id("unknown-request");
  return response;
}

static void check_client_rejects_malformed_tunnel_responses()
{
  using response_factory                                                                 = protobuf::Message (*)(const protobuf::OpenTunnelReq&);
  const std::array<std::pair<response_factory, rstream::io_rstrm::error::code>, 5> cases = {{
      {tunnel_response_with_missing_id, rstream::io_rstrm::error::code::protocol_error},
      {tunnel_response_with_invalid_type, rstream::io_rstrm::error::code::invalid_configuration},
      {tunnel_response_with_error, rstream::io_rstrm::error::code::unauthorized},
      {tunnel_response_without_payload, rstream::io_rstrm::error::code::protocol_error},
      {tunnel_response_with_unknown_request_id, rstream::io_rstrm::error::code::protocol_error},
  }};

  for (const auto& item : cases) {
    fake_engine engine;
    engine.start([factory = item.first](tcp::socket& socket) {
      auto open_request = read_message(socket);
      assert(open_request.has_open_control_channel_req());
      write_message(socket, open_control_response());

      auto tunnel_request = read_message(socket);
      assert(tunnel_request.has_open_tunnel_req());
      write_message(socket, factory(tunnel_request.open_tunnel_req()));
    });

    boost::asio::io_context io_context;
    rstream::io_rstrm::config_client config;
    config.m_no_token              = true;
    config.m_hearbeat              = false;
    config.m_connection_timeout_ms = kControlChannelTimeoutMs;
    rstream::io_rstrm::client client(io_context.get_executor(), config);

    bool rejected = false;
    watchdog test_watchdog(io_context);
    client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
      assert(!error_code);
      rstream::io_rstrm::tunnel_properties properties;
      properties.m_name = "api";
      properties.m_type = "bytestream";
      client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
        assert(create_error == item.second);
        assert(!tunnel);
        rejected = true;
      });
    });

    io_context.run();
    test_watchdog.complete();
    engine.join();

    assert(!test_watchdog.timed_out());
    assert(rejected);
  }
}

static void check_client_rejects_duplicate_active_tunnel_id()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    check(open_request.has_open_control_channel_req(), "missing open control channel request");
    write_message(socket, open_control_response());

    auto first_request = read_message(socket);
    check(first_request.has_open_tunnel_req(), "missing first tunnel request");
    write_message(socket, open_tunnel_response(first_request.open_tunnel_req()));

    auto second_request = read_message(socket);
    check(second_request.has_open_tunnel_req(), "missing second tunnel request");
    write_message(socket, open_tunnel_response(second_request.open_tunnel_req()));
    wait_for_peer_close(socket);
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  bool rejected     = false;
  bool disconnected = false;
  watchdog test_watchdog(io_context);
  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    check(error_code == rstream::io_rstrm::error::code::protocol_error, "duplicate tunnel ID did not close the protocol session");
    disconnected = true;
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  check(!callback_error, "failed to set control callbacks");
  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    check(!error_code, "client failed to connect");
    rstream::io_rstrm::tunnel_properties first_properties;
    first_properties.m_name = "first";
    first_properties.m_type = "bytestream";
    client.async_create_tunnel(first_properties, [&](const boost::system::error_code& first_error, rstream::io_rstrm::tunnel first_tunnel) {
      check(!first_error && first_tunnel, "first tunnel was not created");
      rstream::io_rstrm::tunnel_properties second_properties;
      second_properties.m_name = "second";
      second_properties.m_type = "bytestream";
      client.async_create_tunnel(second_properties, [&](const boost::system::error_code& second_error, rstream::io_rstrm::tunnel second_tunnel) {
        check(second_error == rstream::io_rstrm::error::code::protocol_error, "duplicate active tunnel ID was accepted");
        check(!second_tunnel, "duplicate active tunnel ID returned a tunnel");
        rejected = true;
      });
    });
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  check(!test_watchdog.timed_out(), "duplicate tunnel ID check timed out");
  check(rejected, "duplicate tunnel ID was not rejected");
  check(disconnected, "duplicate tunnel ID did not disconnect the client");
}

static void check_client_heartbeat_and_unexpected_close_response()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());

    auto heartbeat = read_message(socket);
    assert(heartbeat.has_heartbeat());
    write_message(socket, close_control_response());
    wait_for_peer_close(socket);
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = true;
  config.m_heartbeat_interval_ms = 1;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool connected    = false;
  bool disconnected = false;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::server_error);
    disconnected = true;
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);

  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    connected = true;
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(connected);
  assert(disconnected);
}

static void check_client_can_reconnect_from_disconnection_callback()
{
  fake_engine first_engine;
  first_engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    write_message(socket, close_control_response());
    wait_for_peer_close(socket);
  });
  fake_engine second_engine;
  second_engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  bool first_connected  = false;
  bool first_closed     = false;
  bool second_connected = false;
  watchdog test_watchdog(io_context);
  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    check(error_code == rstream::io_rstrm::error::code::server_error, "unexpected first disconnection cause");
    first_closed = true;
    client.async_connect(rstream::io::make_address(second_engine.address()), [&](const boost::system::error_code& reconnect_error) {
      check(!reconnect_error, "client rejected reconnect from disconnection callback: " + reconnect_error.message());
      second_connected = true;
      client.close();
    });
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);
  client.async_connect(rstream::io::make_address(first_engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    first_connected = true;
  });
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (std::size_t index = 0; index < 4; ++index) {
    workers.emplace_back([&io_context] { io_context.run(); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  test_watchdog.complete();
  first_engine.join();
  second_engine.join();
  assert(!test_watchdog.timed_out());
  assert(first_connected);
  assert(first_closed);
  assert(second_connected);
}

static void check_client_drains_active_control_write_before_reconnect()
{
  fake_engine first_engine;
  first_engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    check(open_request.has_open_control_channel_req(), "missing first control-channel request");
    write_message(socket, open_control_response());
    std::uint32_t network_size = 0;
    read_exact(socket, &network_size, sizeof(network_size));
    check(ntohl(network_size) > 4 * 1024 * 1024, "control write did not exceed the socket send buffer");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
  });
  fake_engine second_engine;
  second_engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    check(open_request.has_open_control_channel_req(), "missing reconnected control-channel request");
    write_message(socket, open_control_response());
    auto close_request = read_message(socket);
    check(close_request.has_close_control_channel_req(), "missing reconnected close request");
    write_message(socket, close_control_response());
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = 500;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  std::size_t create_completion_count = 0;
  std::size_t disconnection_count     = 0;
  bool first_connected                = false;
  bool second_connected               = false;
  watchdog test_watchdog(io_context);
  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    check(error_code == rstream::io_rstrm::error::code::operation_timeout, "unexpected active-write disconnection cause");
    ++disconnection_count;
    client.async_connect(rstream::io::make_address(second_engine.address()), [&](const boost::system::error_code& reconnect_error) {
      check(!reconnect_error, "client failed to reconnect after draining its control write: " + reconnect_error.message());
      second_connected = true;
      client.close();
    });
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  check(!callback_error, "failed to install active-write disconnection callback");
  client.async_connect(rstream::io::make_address(first_engine.address()), [&](const boost::system::error_code& error_code) {
    check(!error_code, "first active-write connection failed: " + error_code.message());
    first_connected = true;
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name = std::string(8 * 1024 * 1024, 'a');
    client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      check(create_error == rstream::io_rstrm::error::code::operation_timeout, "active tunnel creation completed with the wrong error");
      check(!tunnel, "cancelled active tunnel creation returned a tunnel");
      ++create_completion_count;
    });
    client.close();
  });
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (std::size_t index = 0; index < 4; ++index) {
    workers.emplace_back([&io_context] { io_context.run(); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  test_watchdog.complete();
  first_engine.join();
  second_engine.join();
  check(!test_watchdog.timed_out(), "active control-write drain check timed out");
  check(first_connected, "client never established the first active-write connection");
  check(create_completion_count == 1, "active tunnel creation did not complete exactly once");
  check(disconnection_count == 1, "active-write disconnection callback did not run exactly once");
  check(second_connected, "client never reconnected after draining its active control write");
}

static protobuf::Message proxy_connection_request(const std::string& stream_id, const std::string& tunnel_id)
{
  protobuf::Message message;
  auto* request = message.mutable_proxy_conn_req();
  request->set_tunnel_id(tunnel_id);
  request->set_stream_id(stream_id);
  auto* source_ip = request->mutable_source_ip();
  source_ip->set_v4(boost::asio::ip::make_address_v4("203.0.113.10").to_uint());
  return message;
}

static protobuf::Message redirected_proxy_connection_request(const std::string& stream_id,
                                                             const std::string& tunnel_id,
                                                             const std::string& proxy_endpoint,
                                                             bool include_secret,
                                                             const std::string& secret = "stream-secret")
{
  auto message  = proxy_connection_request(stream_id, tunnel_id);
  auto* request = message.mutable_proxy_conn_req();
  request->mutable_proxy_endpoint()->set_value(proxy_endpoint);
  if (include_secret) {
    request->mutable_secret()->set_value(secret);
  }
  return message;
}

static protobuf::Message proxy_response()
{
  protobuf::Message response;
  response.mutable_proxy_rsp();
  return response;
}

static bool is_zero_rtt_proxy_request(const protobuf::Message& message)
{
  check(message.has_proxy_req(), "expected proxy stream handshake");
  const auto& request = message.proxy_req();
  return request.has_zero_rtt() && request.zero_rtt().value();
}

static void write_proxy_response_if_needed(tcp::socket& socket, const protobuf::Message& request)
{
  if (!is_zero_rtt_proxy_request(request)) {
    write_message(socket, proxy_response());
  }
}

static void wait_until_proxy_stream_is_ready(boost::asio::io_context& io_context, std::shared_ptr<boost::asio::steady_timer> timer, std::atomic_bool& ready, std::function<void()> callback)
{
  if (ready.load()) {
    callback();
    return;
  }
  timer->expires_after(std::chrono::milliseconds(1));
  timer->async_wait([&io_context, timer, &ready, callback = std::move(callback)](const boost::system::error_code& error_code) mutable {
    assert(!error_code);
    wait_until_proxy_stream_is_ready(io_context, timer, ready, std::move(callback));
  });
}

static void check_client_accepts_delayed_proxy_stream_and_rejects_max_streams()
{
  fake_engine engine;
  std::atomic_bool first_stream_ready = false;
  engine.start_multi([&first_stream_ready](tcp::acceptor& acceptor) {
    auto control      = accept_connection(acceptor);
    auto open_request = read_message(control);
    assert(open_request.has_open_control_channel_req());
    write_message(control, open_control_response());

    auto tunnel_request = read_message(control);
    assert(tunnel_request.has_open_tunnel_req());
    write_message(control, open_tunnel_response(tunnel_request.open_tunnel_req()));

    write_message(control, proxy_connection_request("stream-1", "tunnel-1"));

    auto stream    = accept_connection(acceptor);
    auto handshake = read_message(stream);
    assert(handshake.has_proxy_req());
    assert(handshake.proxy_req().stream_id() == "stream-1");
    assert(!handshake.proxy_req().client_details().has_token());
    write_proxy_response_if_needed(stream, handshake);

    auto proxy_ack = read_message(control);
    assert(proxy_ack.has_proxy_conn_rsp());
    assert(proxy_ack.proxy_conn_rsp().stream_id() == "stream-1");
    assert(!proxy_ack.proxy_conn_rsp().has_error());

    write_message(control, proxy_connection_request("stream-2", "tunnel-1"));
    auto proxy_rejection = read_message(control);
    assert(proxy_rejection.has_proxy_conn_rsp());
    assert(proxy_rejection.proxy_conn_rsp().stream_id() == "stream-2");
    assert(proxy_rejection.proxy_conn_rsp().has_error());
    assert(proxy_rejection.proxy_conn_rsp().error().code() == static_cast<protobuf::ErrorCode>(rstream::io_rstrm::error::make_error_code(rstream::io_rstrm::error::code::operation_aborted).value()));

    first_stream_ready        = true;
    const std::string inbound = "hello";
    boost::asio::write(stream, boost::asio::buffer(inbound));
    std::array<char, 5> reply = {};
    boost::asio::read(stream, boost::asio::buffer(reply));
    assert(std::string(reply.data(), reply.size()) == "world");

    auto close_request = read_message(control);
    assert(close_request.has_close_control_channel_req());
    write_message(control, close_control_response());
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token               = true;
  config.m_hearbeat               = false;
  config.m_connection_timeout_ms  = kControlChannelTimeoutMs;
  config.m_max_ongoing_streams    = 1;
  config.m_async_stream_operation = true;
  boost::asio::io_context peer_io_context;
  auto peer_work = boost::asio::make_work_guard(peer_io_context);
  std::thread peer_thread([&peer_io_context] { peer_io_context.run(); });
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool accepted_delayed_stream                                  = false;
  bool read_stream_payload                                      = false;
  auto accepted_tunnel                                          = std::make_shared<rstream::io_rstrm::tunnel>();
  const rstream::io::io_object::executor_type accepted_executor = peer_io_context.get_executor();
  auto accepted_peer                                            = std::make_shared<rstream::io_rstrm::socket>(accepted_executor);
  auto accepted_endpoint                                        = std::make_shared<rstream::io_rstrm::endpoint>();
  auto accept_timer                                             = std::make_shared<boost::asio::steady_timer>(io_context);
  watchdog test_watchdog(io_context);

  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name = "api";
    properties.m_type = "bytestream";
    client.async_create_tunnel(properties, [&, accepted_tunnel, accepted_peer, accepted_endpoint, accept_timer](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(!create_error);
      assert(tunnel);
      *accepted_tunnel = std::move(tunnel);
      wait_until_proxy_stream_is_ready(io_context, accept_timer, first_stream_ready, [&, accepted_tunnel, accepted_peer, accepted_endpoint] {
        accepted_tunnel->async_accept(*accepted_peer, *accepted_endpoint, [&, accepted_peer, accepted_endpoint](const boost::system::error_code& accept_error) {
          assert(!accept_error);
          assert(accepted_peer->get_executor() == accepted_executor);
          accepted_delayed_stream = true;
          assert(accepted_endpoint->m_id_name && accepted_endpoint->m_id_name.value() == "stream-1");
          assert(!accepted_endpoint->m_secret);
          assert(accepted_endpoint->m_source_ip);
          assert(accepted_endpoint->m_source_ip->to_string() == "203.0.113.10");
          auto read_buffer = std::make_shared<std::array<char, 5>>();
          accepted_peer->async_read_some(boost::asio::buffer(*read_buffer), [&, accepted_peer, read_buffer](const boost::system::error_code& read_error, std::size_t read) {
            assert(peer_io_context.get_executor().running_in_this_thread());
            assert(!read_error);
            assert(read == read_buffer->size());
            assert(std::string(read_buffer->data(), read_buffer->size()) == "hello");
            read_stream_payload = true;
            auto reply          = std::make_shared<std::string>("world");
            accepted_peer->async_write_some(boost::asio::buffer(*reply), [&, reply](const boost::system::error_code& write_error, std::size_t written) {
              assert(peer_io_context.get_executor().running_in_this_thread());
              assert(!write_error);
              assert(written == reply->size());
              client.close();
            });
          });
        });
      });
    });
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();
  peer_work.reset();
  peer_thread.join();

  assert(!test_watchdog.timed_out());
  assert(accepted_delayed_stream);
  assert(read_stream_payload);
}

static void check_client_rejects_proxy_request_for_unknown_tunnel()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    write_message(socket, proxy_connection_request("stream-1", "missing-tunnel"));
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token              = true;
  config.m_hearbeat              = false;
  config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  rstream::io_rstrm::client client(io_context.get_executor(), config);

  bool connected    = false;
  bool disconnected = false;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::protocol_error);
    disconnected = true;
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);

  client.async_connect(rstream::io::make_address(engine.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    connected = true;
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(connected);
  assert(disconnected);
}

static void check_client_reports_redirected_proxy_failures()
{
  boost::asio::io_context port_context;
  tcp::acceptor unavailable(port_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  const auto unavailable_address = std::string("127.0.0.1:") + std::to_string(unavailable.local_endpoint().port());
  unavailable.close();

  std::atomic_bool failures_reported = false;
  fake_engine owner;
  owner.start([&](tcp::socket& control) {
    auto open_request = read_message(control);
    assert(open_request.has_open_control_channel_req());
    write_message(control, open_control_response());
    auto tunnel_request = read_message(control);
    assert(tunnel_request.has_open_tunnel_req());
    write_message(control, open_tunnel_response(tunnel_request.open_tunnel_req()));

    write_message(control, redirected_proxy_connection_request("stream-unavailable", "tunnel-1", unavailable_address, true));
    auto unavailable_response = read_message(control);
    assert(unavailable_response.has_proxy_conn_rsp());
    assert(unavailable_response.proxy_conn_rsp().has_error());

    write_message(control, redirected_proxy_connection_request("stream-missing-secret", "tunnel-1", unavailable_address, false));
    auto missing_secret_response = read_message(control);
    assert(missing_secret_response.has_proxy_conn_rsp());
    assert(missing_secret_response.proxy_conn_rsp().has_error());

    write_message(control, redirected_proxy_connection_request("stream-empty-endpoint", "tunnel-1", "", true));
    auto empty_endpoint_response = read_message(control);
    assert(empty_endpoint_response.has_proxy_conn_rsp());
    assert(empty_endpoint_response.proxy_conn_rsp().has_error());

    write_message(control, redirected_proxy_connection_request("stream-empty-secret", "tunnel-1", unavailable_address, true, ""));
    auto empty_secret_response = read_message(control);
    assert(empty_secret_response.has_proxy_conn_rsp());
    assert(empty_secret_response.proxy_conn_rsp().has_error());
    failures_reported = true;
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::config_client config;
  config.m_no_token               = true;
  config.m_hearbeat               = false;
  config.m_connection_timeout_ms  = kControlChannelTimeoutMs;
  config.m_async_stream_operation = true;
  rstream::io_rstrm::client client(io_context.get_executor(), config);
  bool tunnel_created = false;
  bool disconnected   = false;
  watchdog test_watchdog(io_context);
  rstream::io_rstrm::client::control_callbacks callbacks;
  callbacks.m_on_disconnection_cb = [&](const boost::system::error_code&) {
    disconnected = true;
  };
  boost::system::error_code callback_error;
  client.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);
  client.async_connect(rstream::io::make_address(owner.address()), [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    rstream::io_rstrm::tunnel_properties properties;
    properties.m_name = "api";
    properties.m_type = "bytestream";
    client.async_create_tunnel(properties, [&](const boost::system::error_code& create_error, rstream::io_rstrm::tunnel tunnel) {
      assert(!create_error);
      assert(tunnel);
      tunnel_created = true;
    });
  });

  io_context.run();
  test_watchdog.complete();
  owner.join();
  assert(!test_watchdog.timed_out());
  assert(tunnel_created);
  assert(disconnected);
  assert(failures_reported.load());
}

static void check_socket_rejects_invalid_state_operations()
{
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket socket(io_context.get_executor());

  boost::system::error_code remote_error;
  auto remote_endpoint = socket.remote_endpoint(remote_error);
  (void)remote_endpoint;
  assert(remote_error == rstream::io_rstrm::error::code::invalid_state);

  bool connect_rejected = false;
  rstream::io_rstrm::endpoint missing_id;
  missing_id.m_server_address = rstream::io::make_address("127.0.0.1:1");
  socket.async_connect(missing_id, [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::invalid_endpoint);
    connect_rejected = true;
  });

  const std::string payload = "hello";
  bool write_rejected       = false;
  socket.async_write_some(boost::asio::buffer(payload), [&](const boost::system::error_code& error_code, std::size_t size) {
    assert(error_code == rstream::io_rstrm::error::code::invalid_state);
    assert(size == 0);
    write_rejected = true;
  });

  std::array<char, 5> read_buffer{};
  bool read_rejected = false;
  socket.async_read_some(boost::asio::buffer(read_buffer), [&](const boost::system::error_code& error_code, std::size_t size) {
    assert(error_code == rstream::io_rstrm::error::code::invalid_state);
    assert(size == 0);
    read_rejected = true;
  });

  io_context.run();
  assert(connect_rejected);
  assert(write_rejected);
  assert(read_rejected);
}

static void check_acceptor_rejects_invalid_state_operations()
{
  boost::asio::io_context io_context;
  rstream::io_rstrm::settings_acceptor settings;
  settings.m_config.m_no_token              = true;
  settings.m_config.m_hearbeat              = false;
  settings.m_config.m_connection_timeout_ms = 100;
  settings.m_auto_reconnect                 = false;
  settings.m_auto_recreate_tunnel           = false;
  settings.m_tunnel_properties.m_type       = "bytestream";
  settings.m_tunnel_properties.m_publish    = true;
  rstream::io_rstrm::acceptor acceptor(io_context.get_executor(), settings);

  boost::system::error_code local_error;
  auto local_endpoint = acceptor.local_endpoint(local_error);
  (void)local_endpoint;
  assert(local_error == rstream::io_rstrm::error::code::invalid_state);

  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint accepted_endpoint;
  bool missing_endpoint_rejected = false;
  acceptor.async_accept(peer, accepted_endpoint, [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::no_valid_endpoint);
    missing_endpoint_rejected = true;
  });
  io_context.run();
  io_context.restart();
  assert(missing_endpoint_rejected);

  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address("127.0.0.1:1");
  boost::system::error_code bind_error;
  acceptor.bind(endpoint, bind_error);
  assert(!bind_error);

  bool first_accept_completed = false;
  acceptor.async_accept(peer, accepted_endpoint, [&](const boost::system::error_code& error_code) {
    assert(error_code);
    first_accept_completed = true;
  });
  assert(io_context.poll_one() == 1);
  io_context.restart();

  boost::system::error_code settings_error;
  acceptor.settings(settings, settings_error);
  assert(settings_error == rstream::io_rstrm::error::code::invalid_state);

  boost::system::error_code rebind_error;
  acceptor.bind(endpoint, rebind_error);
  assert(rebind_error == rstream::io_rstrm::error::code::invalid_state);

  rstream::io_rstrm::socket second_peer(io_context.get_executor());
  rstream::io_rstrm::endpoint second_endpoint;
  bool second_accept_rejected = false;
  acceptor.async_accept(second_peer, second_endpoint, [&](const boost::system::error_code& error_code) {
    assert(error_code == rstream::io_rstrm::error::code::operation_in_progress);
    second_accept_rejected = true;
  });

  watchdog test_watchdog(io_context);
  io_context.run();
  test_watchdog.complete();

  assert(!test_watchdog.timed_out());
  assert(first_accept_completed);
  assert(second_accept_rejected);
}

static void check_acceptor_opens_tunnel_and_aborts_pending_accept_on_close()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    assert(!open_request.open_control_channel_req().client_details().has_token());
    write_message(socket, open_control_response());

    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    assert(tunnel_request.open_tunnel_req().tunnel_properties().name().value() == "api");
    assert(tunnel_request.open_tunnel_req().tunnel_properties().publish().value());
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));

    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::settings_acceptor settings;
  settings.m_config.m_no_token              = true;
  settings.m_config.m_hearbeat              = false;
  settings.m_config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  settings.m_auto_reconnect                 = false;
  settings.m_auto_recreate_tunnel           = false;
  settings.m_tunnel_properties.m_type       = "bytestream";
  settings.m_tunnel_properties.m_publish    = true;
  settings.m_tunnel_properties.m_protocol   = "http";
  rstream::io_rstrm::acceptor acceptor(io_context.get_executor(), settings);

  bool saw_tunnel_properties = false;
  bool saw_online_status     = false;
  bool accept_aborted        = false;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::acceptor::control_callbacks callbacks;
  callbacks.m_on_tunnel_properties_cb = [&](const rstream::io_rstrm::tunnel_properties& properties) {
    saw_tunnel_properties = true;
    assert(properties.m_id && properties.m_id.value() == "tunnel-1");
    boost::system::error_code close_error;
    acceptor.close(close_error);
    assert(!close_error);
  };
  callbacks.m_on_status_cb = [&](const rstream::io_rstrm::status_extd& status) {
    if (status.m_status && status.m_status.value() == "online") {
      saw_online_status = true;
      assert(status.m_tunnel_id && status.m_tunnel_id.value() == "tunnel-1");
      assert(status.m_forwarding && status.m_forwarding.value() == "https://api.t.localhost.rstream.test");
    }
  };
  boost::system::error_code callback_error;
  acceptor.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);

  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::system::error_code bind_error;
  acceptor.bind(endpoint, bind_error);
  assert(!bind_error);

  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint accepted_endpoint;
  acceptor.async_accept(peer, accepted_endpoint, [&](const boost::system::error_code& error_code) {
    accept_aborted = true;
    assert(error_code == rstream::io_rstrm::error::code::operation_aborted);
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(saw_tunnel_properties);
  assert(!saw_online_status);
  assert(accept_aborted);
}

static void check_acceptor_honors_pending_accept_cancellation()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());
    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));
    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::settings_acceptor settings;
  settings.m_config.m_no_token              = true;
  settings.m_config.m_hearbeat              = false;
  settings.m_config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  settings.m_auto_reconnect                 = false;
  settings.m_auto_recreate_tunnel           = false;
  settings.m_tunnel_properties.m_type       = "bytestream";
  settings.m_tunnel_properties.m_publish    = true;
  rstream::io_rstrm::acceptor acceptor(io_context.get_executor(), settings);
  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::system::error_code bind_error;
  acceptor.bind(endpoint, bind_error);
  assert(!bind_error);
  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint accepted_endpoint;
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer cancellation_timer(io_context);
  boost::asio::steady_timer close_timer(io_context);
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  watchdog test_watchdog(io_context);
  acceptor.async_accept(
      peer,
      accepted_endpoint,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        completion_error = error_code;
        ++completion_count;
      }));
  cancellation_timer.expires_after(std::chrono::milliseconds(20));
  cancellation_timer.async_wait([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    cancellation.emit(boost::asio::cancellation_type::all);
  });
  close_timer.expires_after(std::chrono::milliseconds(80));
  close_timer.async_wait([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    assert(completion_count == 1);
    assert(completion_error == boost::asio::error::operation_aborted);
    boost::system::error_code close_error;
    acceptor.close(close_error);
    assert(!close_error);
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(completion_count == 1);
}

static void check_generated_stable_domain()
{
  const auto hostname = rstream::io_rstrm::detail::generate_stable_domain(
      rstream::io::make_address("tcp://project.cluster.example:443"));
  assert(hostname);
  const std::string suffix = "-project.t.cluster.example";
  assert(hostname->size() > suffix.size());
  assert(hostname->find(suffix) == hostname->size() - suffix.size());
}

static void check_acceptor_stable_domain_behavior(const std::string& protocol, bool expects_hostname)
{
  fake_engine engine;
  engine.start([protocol, expects_hostname](tcp::socket& socket) {
    auto open_request = read_message(socket);
    assert(open_request.has_open_control_channel_req());
    write_message(socket, open_control_response());

    auto tunnel_request = read_message(socket);
    assert(tunnel_request.has_open_tunnel_req());
    assert(tunnel_request.open_tunnel_req().tunnel_properties().name().value() == "project");
    assert(tunnel_request.open_tunnel_req().tunnel_properties().protocol().value() == protocol);
    assert(tunnel_request.open_tunnel_req().tunnel_properties().has_hostname() == expects_hostname);
    if (expects_hostname) {
      assert(tunnel_request.open_tunnel_req().tunnel_properties().hostname().value() == "stream.example.test");
    }
    write_message(socket, open_tunnel_response(tunnel_request.open_tunnel_req()));

    auto close_request = read_message(socket);
    assert(close_request.has_close_control_channel_req());
    write_message(socket, close_control_response());
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::settings_acceptor settings;
  settings.m_config.m_no_token              = true;
  settings.m_config.m_hearbeat              = false;
  settings.m_config.m_connection_timeout_ms = kControlChannelTimeoutMs;
  settings.m_auto_reconnect                 = false;
  settings.m_auto_recreate_tunnel           = false;
  settings.m_tunnel_properties.m_type       = "bytestream";
  settings.m_tunnel_properties.m_publish    = true;
  settings.m_tunnel_properties.m_protocol   = protocol;
  if (expects_hostname) {
    settings.m_tunnel_properties.m_hostname = "stream.example.test";
  }
  rstream::io_rstrm::acceptor acceptor(io_context.get_executor(), settings);

  bool saw_tunnel_properties          = false;
  std::size_t accept_completion_count = 0;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::acceptor::control_callbacks callbacks;
  callbacks.m_on_tunnel_properties_cb = [&](const rstream::io_rstrm::tunnel_properties& properties) {
    saw_tunnel_properties = true;
    assert(properties.m_id && properties.m_id.value() == "tunnel-1");
    boost::system::error_code close_error;
    acceptor.close(close_error);
    assert(!close_error);
  };
  boost::system::error_code callback_error;
  acceptor.set_control_callbacks(callbacks, callback_error);
  assert(!callback_error);

  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "project";
  endpoint.m_server_address = rstream::io::make_address(std::string("127.0.0.1:") + std::to_string(engine.port()));
  boost::system::error_code bind_error;
  acceptor.bind(endpoint, bind_error);
  assert(!bind_error);

  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint accepted_endpoint;
  acceptor.async_accept(peer, accepted_endpoint, [&](const boost::system::error_code& error_code) {
    ++accept_completion_count;
    check(
        error_code == rstream::io_rstrm::error::code::operation_aborted,
        "pending accept did not complete with rstream operation_aborted: "
            + std::string(error_code.category().name()) + ":" + std::to_string(error_code.value()) + " " + error_code.message());
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(saw_tunnel_properties);
  assert(accept_completion_count == 1);
}

static void check_acceptor_generates_stable_domain_from_project_endpoint()
{
  check_generated_stable_domain();
  check_acceptor_stable_domain_behavior(rstream::io_rstrm::protocol::http, true);
}

static void check_acceptor_skips_stable_domain_for_published_tcp()
{
  check_acceptor_stable_domain_behavior(rstream::io_rstrm::protocol::tcp, false);
}

static void check_socket_stream_handshake_then_transfers_bytes()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    assert(stream_request.stream_req().tunnel_id_name() == "api");
    assert(!stream_request.stream_req().client_details().has_token());
    assert(!stream_request.stream_req().has_zero_rtt() || !stream_request.stream_req().zero_rtt().value());
    write_message(socket, stream_response("stream-1"));

    std::array<char, 5> inbound = {};
    boost::asio::read(socket, boost::asio::buffer(inbound));
    assert(std::string(inbound.data(), inbound.size()) == "hello");
    const std::string outbound = "world";
    boost::asio::write(socket, boost::asio::buffer(outbound));
  });

  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor());
  rstream::io_rstrm::settings_socket settings;
  settings.m_config.m_no_token = true;
  settings.m_config.m_zero_rtt = false;
  boost::system::error_code settings_error;
  rstream_socket.settings(settings, settings_error);
  assert(!settings_error);

  bool saw_connected = false;
  bool saw_write     = false;
  bool saw_read      = false;
  watchdog test_watchdog(io_context);

  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  auto outbound             = std::make_shared<std::string>("hello");
  auto inbound              = std::make_shared<std::array<char, 5>>();
  rstream_socket.async_connect(endpoint, [&](const boost::system::error_code& error_code) {
    if (error_code) {
      std::cerr << "socket connection failed: " << error_code.category().name() << ":" << error_code.value() << " " << error_code.message() << std::endl;
    }
    assert(!error_code);
    saw_connected = true;
    boost::system::error_code remote_error;
    const auto remote_endpoint = rstream_socket.remote_endpoint(remote_error);
    assert(!remote_error);
    assert(remote_endpoint.m_id_name && remote_endpoint.m_id_name.value() == "api");
    assert(!remote_endpoint.m_secret);
    rstream_socket.async_write_some(boost::asio::buffer(*outbound), [&, outbound, inbound](const boost::system::error_code& write_error, std::size_t written) {
      assert(!write_error);
      assert(written == outbound->size());
      saw_write = true;
      rstream_socket.async_read_some(boost::asio::buffer(*inbound), [&, inbound](const boost::system::error_code& read_error, std::size_t read) {
        assert(!read_error);
        assert(read == inbound->size());
        assert(std::string(inbound->data(), inbound->size()) == "world");
        saw_read = true;
        boost::system::error_code close_error;
        rstream_socket.close(close_error);
        assert(!close_error);
      });
    });
  });

  io_context.run();
  test_watchdog.complete();
  engine.join();

  assert(!test_watchdog.timed_out());
  assert(saw_connected);
  assert(saw_write);
  assert(saw_read);
}

static rstream::io_rstrm::settings_socket make_test_socket_settings()
{
  rstream::io_rstrm::settings_socket settings;
  settings.m_config.m_no_token = true;
  settings.m_config.m_zero_rtt = false;
  return settings;
}

static void check_socket_first_use_is_thread_safe()
{
  static constexpr std::size_t operation_count = 8;
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor());
  std::array<std::array<char, 1>, operation_count> buffers = {};
  std::atomic_size_t ready                                 = 0;
  std::atomic_bool start                                   = false;
  std::atomic_size_t completion_count                      = 0;
  std::vector<std::thread> initiation_threads;
  initiation_threads.reserve(operation_count);
  for (std::size_t index = 0; index < operation_count; ++index) {
    initiation_threads.emplace_back([&, index] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      rstream_socket.async_read_some(
          boost::asio::buffer(buffers[index]),
          [&completion_count](const boost::system::error_code& error_code, std::size_t transferred) {
            assert(error_code == rstream::io_rstrm::error::code::invalid_state);
            assert(transferred == 0);
            completion_count.fetch_add(1, std::memory_order_relaxed);
          });
    });
  }
  while (ready.load(std::memory_order_acquire) != operation_count) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);
  for (auto& thread : initiation_threads) {
    thread.join();
  }
  io_context.run();
  assert(completion_count.load(std::memory_order_relaxed) == operation_count);
}

static void check_socket_connect_honors_immediate_cancellation()
{
  fake_engine engine;
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor(), make_test_socket_settings());
  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer close_timer(io_context);
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  watchdog test_watchdog(io_context);
  rstream_socket.async_connect(
      endpoint,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        completion_error = error_code;
        ++completion_count;
      }));
  cancellation.emit(boost::asio::cancellation_type::all);
  close_timer.expires_after(std::chrono::milliseconds(80));
  close_timer.async_wait([&](const boost::system::error_code& error_code) {
    assert(!error_code);
    assert(completion_count == 1);
    assert(completion_error == boost::asio::error::operation_aborted);
    boost::system::error_code close_error;
    rstream_socket.close(close_error);
    assert(!close_error);
  });
  io_context.run();
  test_watchdog.complete();
  assert(!test_watchdog.timed_out());
  assert(completion_count == 1);
}

static void check_socket_connect_honors_pending_cancellation_and_can_reconnect()
{
  fake_engine first_engine;
  std::atomic_bool handshake_seen = false;
  first_engine.start([&handshake_seen](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    handshake_seen = true;
    wait_for_peer_close(socket);
  });
  fake_engine second_engine;
  second_engine.start([](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    write_message(socket, stream_response("stream-2"));
    std::array<char, 1> buffer = {};
    boost::system::error_code error_code;
    socket.read_some(boost::asio::buffer(buffer), error_code);
    assert(error_code);
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor(), make_test_socket_settings());
  rstream::io_rstrm::endpoint first_endpoint;
  first_endpoint.m_id_name        = "api";
  first_endpoint.m_server_address = rstream::io::make_address(first_engine.address());
  rstream::io_rstrm::endpoint second_endpoint;
  second_endpoint.m_id_name        = "api";
  second_endpoint.m_server_address = rstream::io::make_address(second_engine.address());
  boost::asio::cancellation_signal cancellation;
  auto cancellation_timer        = std::make_shared<boost::asio::steady_timer>(io_context);
  std::size_t cancellation_count = 0;
  bool reconnected               = false;
  watchdog test_watchdog(io_context);
  rstream_socket.async_connect(
      first_endpoint,
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error_code) {
        assert(error_code == boost::asio::error::operation_aborted);
        ++cancellation_count;
        rstream_socket.async_connect(second_endpoint, [&](const boost::system::error_code& reconnect_error) {
          assert(!reconnect_error);
          reconnected = true;
          boost::system::error_code close_error;
          rstream_socket.close(close_error);
          assert(!close_error);
        });
      }));
  wait_until_proxy_stream_is_ready(io_context, cancellation_timer, handshake_seen, [&cancellation] {
    cancellation.emit(boost::asio::cancellation_type::all);
  });
  io_context.run();
  test_watchdog.complete();
  first_engine.join();
  second_engine.join();
  assert(!test_watchdog.timed_out());
  assert(cancellation_count == 1);
  assert(reconnected);
}

static void check_socket_read_honors_immediate_cancellation()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    write_message(socket, stream_response("stream-1"));
    std::array<char, 1> buffer = {};
    boost::system::error_code error_code;
    socket.read_some(boost::asio::buffer(buffer), error_code);
    assert(error_code);
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor(), make_test_socket_settings());
  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer close_timer(io_context);
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  std::array<char, 1> buffer   = {};
  watchdog test_watchdog(io_context);
  rstream_socket.async_connect(endpoint, [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    std::thread initiation_thread([&] {
      rstream_socket.async_read_some(
          boost::asio::buffer(buffer),
          boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& read_error, std::size_t read) {
            completion_error = read_error;
            assert(read == 0);
            ++completion_count;
          }));
      cancellation.emit(boost::asio::cancellation_type::all);
    });
    initiation_thread.join();
    close_timer.expires_after(std::chrono::milliseconds(80));
    close_timer.async_wait([&](const boost::system::error_code& timer_error) {
      assert(!timer_error);
      assert(completion_count == 1);
      assert(completion_error == boost::asio::error::operation_aborted);
      boost::system::error_code close_error;
      rstream_socket.close(close_error);
      assert(!close_error);
    });
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(completion_count == 1);
}

static void check_socket_read_honors_pending_cancellation_without_closing_socket()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    write_message(socket, stream_response("stream-1"));
    std::array<char, 4> request = {};
    read_exact(socket, request.data(), request.size());
    assert(std::string(request.data(), request.size()) == "ping");
    boost::asio::write(socket, boost::asio::buffer("pong", 4));
    std::array<char, 1> buffer = {};
    boost::system::error_code error_code;
    socket.read_some(boost::asio::buffer(buffer), error_code);
    assert(error_code);
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor(), make_test_socket_settings());
  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer cancellation_timer(io_context);
  std::array<char, 4> cancelled_buffer = {};
  std::array<char, 4> response         = {};
  std::size_t cancellation_count       = 0;
  bool exchanged_after_cancellation    = false;
  watchdog test_watchdog(io_context);
  rstream_socket.async_connect(endpoint, [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    rstream_socket.async_read_some(
        boost::asio::buffer(cancelled_buffer),
        boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& read_error, std::size_t read) {
          assert(read_error == boost::asio::error::operation_aborted);
          assert(read == 0);
          ++cancellation_count;
          rstream_socket.async_write_some(boost::asio::buffer("ping", 4), [&](const boost::system::error_code& write_error, std::size_t written) {
            assert(!write_error);
            assert(written == 4);
            rstream_socket.async_read_some(boost::asio::buffer(response), [&](const boost::system::error_code& response_error, std::size_t response_size) {
              assert(!response_error);
              assert(response_size == response.size());
              assert(std::string(response.data(), response.size()) == "pong");
              exchanged_after_cancellation = true;
              boost::system::error_code close_error;
              rstream_socket.close(close_error);
              assert(!close_error);
            });
          });
        }));
    cancellation_timer.expires_after(std::chrono::milliseconds(20));
    cancellation_timer.async_wait([&](const boost::system::error_code& timer_error) {
      assert(!timer_error);
      cancellation.emit(boost::asio::cancellation_type::all);
    });
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(cancellation_count == 1);
  assert(exchanged_after_cancellation);
}

static void check_socket_write_honors_immediate_cancellation()
{
  fake_engine engine;
  engine.start([](tcp::socket& socket) {
    auto stream_request = read_message(socket);
    assert(stream_request.has_stream_req());
    write_message(socket, stream_response("stream-1"));
    std::array<char, 5> buffer = {};
    boost::system::error_code error_code;
    socket.read_some(boost::asio::buffer(buffer), error_code);
    assert(error_code);
  });
  boost::asio::io_context io_context;
  rstream::io_rstrm::socket rstream_socket(io_context.get_executor(), make_test_socket_settings());
  rstream::io_rstrm::endpoint endpoint;
  endpoint.m_id_name        = "api";
  endpoint.m_server_address = rstream::io::make_address(engine.address());
  boost::asio::cancellation_signal cancellation;
  boost::asio::steady_timer close_timer(io_context);
  boost::system::error_code completion_error;
  std::size_t completion_count = 0;
  const std::string buffer     = "hello";
  watchdog test_watchdog(io_context);
  rstream_socket.async_connect(endpoint, [&](const boost::system::error_code& error_code) {
    assert(!error_code);
    std::thread initiation_thread([&] {
      rstream_socket.async_write_some(
          boost::asio::buffer(buffer),
          boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& write_error, std::size_t written) {
            completion_error = write_error;
            assert(written == 0);
            ++completion_count;
          }));
      cancellation.emit(boost::asio::cancellation_type::all);
    });
    initiation_thread.join();
    close_timer.expires_after(std::chrono::milliseconds(80));
    close_timer.async_wait([&](const boost::system::error_code& timer_error) {
      assert(!timer_error);
      assert(completion_count == 1);
      assert(completion_error == boost::asio::error::operation_aborted);
      boost::system::error_code close_error;
      rstream_socket.close(close_error);
      assert(!close_error);
    });
  });
  io_context.run();
  test_watchdog.complete();
  engine.join();
  assert(!test_watchdog.timed_out());
  assert(completion_count == 1);
}

static void run_check(const char* name, void (*callback)())
{
  std::cout << "running '" << name << "'" << std::endl;
  try {
    callback();
  }
  catch (...) {
    std::cerr << "failed check: " << name << std::endl;
    throw;
  }
}

static void run_selected_check(const char* selected, const char* name, void (*callback)())
{
  if (selected && std::string(selected) != name) {
    return;
  }
  run_check(name, callback);
}

int main(int argc, char** argv)
{
  const char* selected = argc > 1 ? argv[1] : nullptr;
  run_selected_check(selected, "client_snapshots_configuration_at_construction", check_client_snapshots_configuration_at_construction);
  run_selected_check(selected, "client_rejects_operations_before_connection", check_client_rejects_operations_before_connection);
  run_selected_check(selected, "client_connect_honors_immediate_cancellation", check_client_connect_honors_immediate_cancellation);
  run_selected_check(selected, "client_create_tunnel_honors_cancellation", check_client_create_tunnel_honors_cancellation);
  run_selected_check(selected, "tunnel_accept_honors_cancellation", check_tunnel_accept_honors_cancellation);
  run_selected_check(selected, "client_can_create_and_close_tunnel", check_client_can_create_and_close_tunnel);
  run_selected_check(selected, "client_normalizes_published_tcp_tunnel", check_client_normalizes_published_tcp_tunnel);
  run_selected_check(selected, "client_rejects_invalid_operations_after_connection", check_client_rejects_invalid_operations_after_connection);
  run_selected_check(selected, "client_rejects_malformed_open_response", check_client_rejects_malformed_open_response);
  run_selected_check(selected, "client_reports_open_response_error", check_client_reports_open_response_error);
  run_selected_check(selected, "client_rejects_empty_open_response", check_client_rejects_empty_open_response);
  run_selected_check(selected, "client_rejects_malformed_tunnel_responses", check_client_rejects_malformed_tunnel_responses);
  run_selected_check(selected, "client_rejects_duplicate_active_tunnel_id", check_client_rejects_duplicate_active_tunnel_id);
  run_selected_check(selected, "client_heartbeat_and_unexpected_close_response", check_client_heartbeat_and_unexpected_close_response);
  run_selected_check(selected, "client_can_reconnect_from_disconnection_callback", check_client_can_reconnect_from_disconnection_callback);
  run_selected_check(selected, "client_drains_active_control_write_before_reconnect", check_client_drains_active_control_write_before_reconnect);
  run_selected_check(selected, "client_accepts_delayed_proxy_stream_and_rejects_max_streams", check_client_accepts_delayed_proxy_stream_and_rejects_max_streams);
  run_selected_check(selected, "client_rejects_proxy_request_for_unknown_tunnel", check_client_rejects_proxy_request_for_unknown_tunnel);
  run_selected_check(selected, "client_reports_redirected_proxy_failures", check_client_reports_redirected_proxy_failures);
  run_selected_check(selected, "socket_rejects_invalid_state_operations", check_socket_rejects_invalid_state_operations);
  run_selected_check(selected, "socket_first_use_is_thread_safe", check_socket_first_use_is_thread_safe);
  run_selected_check(selected, "acceptor_rejects_invalid_state_operations", check_acceptor_rejects_invalid_state_operations);
  run_selected_check(selected, "acceptor_opens_tunnel_and_aborts_pending_accept_on_close", check_acceptor_opens_tunnel_and_aborts_pending_accept_on_close);
  run_selected_check(selected, "acceptor_honors_pending_accept_cancellation", check_acceptor_honors_pending_accept_cancellation);
  run_selected_check(selected, "acceptor_generates_stable_domain_from_project_endpoint", check_acceptor_generates_stable_domain_from_project_endpoint);
  run_selected_check(selected, "acceptor_skips_stable_domain_for_published_tcp", check_acceptor_skips_stable_domain_for_published_tcp);
  run_selected_check(selected, "socket_stream_handshake_then_transfers_bytes", check_socket_stream_handshake_then_transfers_bytes);
  run_selected_check(selected, "socket_connect_honors_immediate_cancellation", check_socket_connect_honors_immediate_cancellation);
  run_selected_check(selected, "socket_connect_honors_pending_cancellation_and_can_reconnect", check_socket_connect_honors_pending_cancellation_and_can_reconnect);
  run_selected_check(selected, "socket_read_honors_immediate_cancellation", check_socket_read_honors_immediate_cancellation);
  run_selected_check(selected, "socket_read_honors_pending_cancellation_without_closing_socket", check_socket_read_honors_pending_cancellation_without_closing_socket);
  run_selected_check(selected, "socket_write_honors_immediate_cancellation", check_socket_write_honors_immediate_cancellation);
  return 0;
}
