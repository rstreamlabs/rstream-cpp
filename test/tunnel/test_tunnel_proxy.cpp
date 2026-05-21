// See LICENSE file in the project root for license information.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <arpa/inet.h>

#include <rstream/io-rstrm/protobuf/messages.pb.h>
#include <rstream/tunnel/error.hpp>
#include <rstream/tunnel/proxy.hpp>

namespace protobuf = rstream::io_rstrm::protobuf;
using tcp          = boost::asio::ip::tcp;

static constexpr unsigned int kTimeoutSeconds = 30;

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

static protobuf::Message read_message(tcp::socket& socket)
{
  std::uint32_t network_size = 0;
  boost::asio::read(socket, boost::asio::buffer(&network_size, sizeof(network_size)));
  const auto size = ntohl(network_size);
  check(size <= 1024 * 1024, "framed protobuf message is too large");
  std::vector<char> payload(size);
  if (size > 0) {
    boost::asio::read(socket, boost::asio::buffer(payload));
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
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
  if (!payload.empty()) {
    boost::asio::write(socket, boost::asio::buffer(payload));
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

static protobuf::Message private_tunnel_response(const protobuf::OpenTunnelReq& request)
{
  protobuf::Message response;
  auto* payload = response.mutable_open_tunnel_rsp();
  payload->set_request_id(request.request_id());
  auto* properties = payload->mutable_tunnel_properties();
  properties->mutable_id()->set_value("private-tunnel-1");
  properties->mutable_name()->set_value(request.tunnel_properties().name().value());
  properties->mutable_type()->set_value("bytestream");
  properties->mutable_publish()->set_value(false);
  return response;
}

static protobuf::Message proxy_connection_request()
{
  protobuf::Message request;
  auto* payload = request.mutable_proxy_conn_req();
  payload->set_tunnel_id("tunnel-1");
  payload->set_stream_id("stream-1");
  payload->mutable_source_ip()->set_v4(boost::asio::ip::make_address_v4("203.0.113.10").to_uint());
  return request;
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

static protobuf::Message close_control_response()
{
  protobuf::Message response;
  response.mutable_close_control_channel_rsp();
  return response;
}

template <class Predicate>
static void run_until(boost::asio::io_context& io_context, Predicate&& predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSeconds);
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    io_context.run_one_for(std::chrono::milliseconds(50));
  }
  io_context.restart();
  check(predicate(), "asynchronous test timed out");
}

class tcp_thread_server {
 public:
  using script_type = std::function<void(tcp::socket&)>;

  explicit tcp_thread_server(script_type script)
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
        m_script(std::move(script))
  {
  }

  ~tcp_thread_server()
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

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        m_script(socket);
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
  script_type m_script;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_engine {
 public:
  explicit fake_engine(std::atomic_bool& stream_exchanged)
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0)),
        m_stream_exchanged(stream_exchanged)
  {
  }

  ~fake_engine()
  {
    join();
  }

  std::string address() const
  {
    return std::string("127.0.0.1:") + std::to_string(m_acceptor.local_endpoint().port());
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket control(m_io_context);
        m_acceptor.accept(control);
        auto open_request = read_message(control);
        check(open_request.has_open_control_channel_req(), "expected open control request");
        check(!open_request.open_control_channel_req().client_details().has_token(), "test control channel must not send a token");
        write_message(control, open_control_response());

        auto tunnel_request = read_message(control);
        check(tunnel_request.has_open_tunnel_req(), "expected open tunnel request");
        check(tunnel_request.open_tunnel_req().tunnel_properties().name().value() == "api", "unexpected tunnel name");
        check(tunnel_request.open_tunnel_req().tunnel_properties().publish().value(), "proxy tunnel must be published");
        write_message(control, open_tunnel_response(tunnel_request.open_tunnel_req()));

        write_message(control, proxy_connection_request());

        tcp::socket stream(m_io_context);
        m_acceptor.accept(stream);
        auto handshake = read_message(stream);
        check(handshake.has_proxy_req(), "expected proxy stream handshake");
        check(handshake.proxy_req().stream_id() == "stream-1", "unexpected proxy stream id");
        check(!handshake.proxy_req().client_details().has_token(), "test proxy stream must not send a token");
        write_proxy_response_if_needed(stream, handshake);

        auto proxy_ack = read_message(control);
        check(proxy_ack.has_proxy_conn_rsp(), "expected proxy connection response");
        check(proxy_ack.proxy_conn_rsp().stream_id() == "stream-1", "unexpected proxy response stream id");
        check(!proxy_ack.proxy_conn_rsp().has_error(), "proxy connection was rejected");

        const std::string downstream_payload = "hello";
        boost::asio::write(stream, boost::asio::buffer(downstream_payload));
        std::array<char, 5> upstream_reply{};
        boost::asio::read(stream, boost::asio::buffer(upstream_reply));
        check(std::string(upstream_reply.data(), upstream_reply.size()) == "world", "unexpected proxied reply");
        m_stream_exchanged = true;

        auto close_request = read_message(control);
        check(close_request.has_close_control_channel_req(), "expected close control request");
        write_message(control, close_control_response());
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
  std::atomic_bool& m_stream_exchanged;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

static void check_proxy_forwards_engine_stream_to_upstream_and_back()
{
  std::atomic_bool upstream_served = false;
  tcp_thread_server upstream([&](tcp::socket& socket) {
    std::array<char, 5> request{};
    boost::asio::read(socket, boost::asio::buffer(request));
    check(std::string(request.data(), request.size()) == "hello", "unexpected upstream request");
    const std::string response = "world";
    boost::asio::write(socket, boost::asio::buffer(response));
    upstream_served = true;
  });
  upstream.start();

  std::atomic_bool stream_exchanged = false;
  fake_engine engine(stream_exchanged);
  engine.start();

  boost::asio::io_context io_context;
  rstream::tunnel::proxy::config config;
  config.m_local_endpoint.m_id_name                           = "api";
  config.m_local_endpoint.m_server_address                    = rstream::io::make_address(engine.address());
  config.m_target_address                                     = rstream::io::make_address("tcp://" + upstream.address());
  config.m_settings_acceptor.m_config.m_no_token              = true;
  config.m_settings_acceptor.m_config.m_hearbeat              = false;
  config.m_settings_acceptor.m_config.m_connection_timeout_ms = 10000;
  config.m_settings_acceptor.m_auto_reconnect                 = false;
  config.m_settings_acceptor.m_auto_recreate_tunnel           = false;
  config.m_settings_acceptor.m_tunnel_properties.m_type       = "bytestream";
  config.m_settings_acceptor.m_tunnel_properties.m_publish    = true;
  config.m_settings_acceptor.m_tunnel_properties.m_protocol   = "http";

  rstream::tunnel::settings_proxy settings = {
      .m_read_downstream_buffer_size_bytes = 1024,
      .m_read_upstream_buffer_size_bytes   = 1024,
      .m_timeouts_ms                       = {.m_open = 10000},
  };

  rstream::tunnel::proxy proxy(io_context.get_executor(), config, settings);
  bool saw_online_status = false;
  bool saw_connection    = false;
  bool proxy_stopped     = false;
  rstream::tunnel::proxy::callbacks callbacks;
  callbacks.m_on_status_cb = [&](const rstream::tunnel::status_proxy& status) {
    if (status.m_status && status.m_status.value() == "online") {
      saw_online_status = true;
      check(status.m_tunnel_id && status.m_tunnel_id.value() == "tunnel-1", "unexpected status tunnel id");
      check(status.m_forwarding && status.m_forwarding.value() == "https://api.t.localhost.rstream.test", "unexpected forwarding address");
      check(static_cast<bool>(status.m_forwarded), "expected forwarded target to be reported");
    }
  };
  callbacks.m_on_new_connection_cb = [&](const rstream::io_rstrm::endpoint& endpoint) {
    saw_connection = true;
    check(endpoint.m_id_name && endpoint.m_id_name.value() == "stream-1", "unexpected incoming stream endpoint");
    check(endpoint.m_source_ip && endpoint.m_source_ip->to_string() == "203.0.113.10", "source IP was not propagated");
  };

  proxy.async_run(callbacks, [&](const boost::system::error_code& error_code) {
    check(!error_code, std::string("proxy stopped with error: ") + error_code.message());
    proxy_stopped = true;
  });

  run_until(io_context, [&] { return stream_exchanged.load(); });
  proxy.cancel();
  run_until(io_context, [&] { return proxy_stopped; });

  engine.join();
  upstream.join();

  check(saw_online_status, "proxy did not report online status");
  check(saw_connection, "proxy did not report the incoming connection");
  check(upstream_served.load(), "upstream did not receive the proxied request");
}

static void check_proxy_rejects_second_run_while_active()
{
  std::atomic_bool stream_exchanged = false;
  fake_engine engine(stream_exchanged);
  engine.start();

  tcp_thread_server upstream([](tcp::socket& socket) {
    std::array<char, 5> request{};
    boost::asio::read(socket, boost::asio::buffer(request));
    const std::string response = "world";
    boost::asio::write(socket, boost::asio::buffer(response));
  });
  upstream.start();

  boost::asio::io_context io_context;
  rstream::tunnel::proxy::config config;
  config.m_local_endpoint.m_id_name                           = "api";
  config.m_local_endpoint.m_server_address                    = rstream::io::make_address(engine.address());
  config.m_target_address                                     = rstream::io::make_address("tcp://" + upstream.address());
  config.m_settings_acceptor.m_config.m_no_token              = true;
  config.m_settings_acceptor.m_config.m_hearbeat              = false;
  config.m_settings_acceptor.m_config.m_connection_timeout_ms = 10000;
  config.m_settings_acceptor.m_auto_reconnect                 = false;
  config.m_settings_acceptor.m_auto_recreate_tunnel           = false;
  config.m_settings_acceptor.m_tunnel_properties.m_type       = "bytestream";
  config.m_settings_acceptor.m_tunnel_properties.m_publish    = true;
  config.m_settings_acceptor.m_tunnel_properties.m_protocol   = "http";

  rstream::tunnel::settings_proxy settings = {
      .m_read_downstream_buffer_size_bytes = 1024,
      .m_read_upstream_buffer_size_bytes   = 1024,
      .m_timeouts_ms                       = {.m_open = 10000},
  };

  rstream::tunnel::proxy proxy(io_context.get_executor(), config, settings);
  bool rejected_second_run = false;
  bool proxy_stopped       = false;
  proxy.async_run({}, [&](const boost::system::error_code& error_code) {
    check(!error_code, std::string("proxy stopped with error: ") + error_code.message());
    proxy_stopped = true;
  });
  run_until(io_context, [&] { return stream_exchanged.load(); });
  proxy.async_run({}, [&](const boost::system::error_code& error_code) {
    check(error_code == rstream::tunnel::error::code::invalid_state, "second proxy run must fail with invalid state");
    rejected_second_run = true;
  });
  run_until(io_context, [&] { return rejected_second_run; });
  proxy.cancel();
  run_until(io_context, [&] { return proxy_stopped; });

  engine.join();
  upstream.join();
}

static void check_proxy_default_tunnel_request_leaves_public_policy_to_server()
{
  std::atomic_bool saw_default_request = false;
  tcp_thread_server engine([&](tcp::socket& control) {
    auto open_request = read_message(control);
    check(open_request.has_open_control_channel_req(), "expected open control request");
    write_message(control, open_control_response());

    auto tunnel_request = read_message(control);
    check(tunnel_request.has_open_tunnel_req(), "expected open tunnel request");
    const auto& properties = tunnel_request.open_tunnel_req().tunnel_properties();
    check(properties.name().value() == "api", "unexpected tunnel name");
    check(!properties.has_publish(), "default proxy tunnel request must not force publish=true");
    check(!properties.has_hostname(), "default proxy tunnel request must not force a stable domain");
    check(!properties.has_protocol(), "default proxy tunnel request must not force a public protocol");
    write_message(control, private_tunnel_response(tunnel_request.open_tunnel_req()));
    saw_default_request = true;

    auto close_request = read_message(control);
    check(close_request.has_close_control_channel_req(), "expected close control request");
    write_message(control, close_control_response());
  });
  engine.start();

  boost::asio::io_context io_context;
  rstream::tunnel::proxy::config config;
  config.m_local_endpoint.m_id_name                           = "api";
  config.m_local_endpoint.m_server_address                    = rstream::io::make_address(engine.address());
  config.m_target_address                                     = rstream::io::make_address("tcp://127.0.0.1:9");
  config.m_settings_acceptor.m_config.m_no_token              = true;
  config.m_settings_acceptor.m_config.m_hearbeat              = false;
  config.m_settings_acceptor.m_config.m_connection_timeout_ms = 10000;
  config.m_settings_acceptor.m_auto_reconnect                 = false;
  config.m_settings_acceptor.m_auto_recreate_tunnel           = false;

  rstream::tunnel::settings_proxy settings = {
      .m_read_downstream_buffer_size_bytes = 1024,
      .m_read_upstream_buffer_size_bytes   = 1024,
      .m_timeouts_ms                       = {.m_open = 10000},
  };

  rstream::tunnel::proxy proxy(io_context.get_executor(), config, settings);
  bool saw_private_status = false;
  bool proxy_stopped      = false;
  rstream::tunnel::proxy::callbacks callbacks;
  callbacks.m_on_status_cb = [&](const rstream::tunnel::status_proxy& status) {
    if (status.m_tunnel_id && status.m_tunnel_id.value() == "private-tunnel-1") {
      saw_private_status = true;
      proxy.cancel();
    }
  };
  proxy.async_run(callbacks, [&](const boost::system::error_code& error_code) {
    check(!error_code, std::string("proxy stopped with error: ") + error_code.message());
    proxy_stopped = true;
  });

  run_until(io_context, [&] { return proxy_stopped; });
  engine.join();

  check(saw_default_request.load(), "fake engine did not observe the default tunnel request");
  check(saw_private_status, "proxy did not accept private fallback tunnel properties");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_proxy_forwards_engine_stream_to_upstream_and_back();
  check_proxy_rejects_second_run_while_active();
  check_proxy_default_tunnel_request_leaves_public_policy_to_server();
  return 0;
}
