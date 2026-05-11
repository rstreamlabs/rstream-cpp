// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <array>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <rstream/rtty/protobuf/messages.pb.h>
#include <rstream/rtty/rtty.hpp>
#include <rstream/rtty/server.hpp>

namespace protobuf = rstream::rtty::protobuf;
using tcp          = boost::asio::ip::tcp;

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static protobuf::Message open_message(const std::vector<std::string>& cmd_args)
{
  protobuf::Message message;
  auto* config = message.mutable_open()->mutable_config();
  config->mutable_options()->set_interactive(false);
  config->mutable_options()->set_allocate_tty(false);
  config->mutable_options()->set_send_heartbeat(false);
  for (const auto& arg : cmd_args) {
    config->add_cmd_args(arg);
  }
  return message;
}

class websocket_rtty_server {
 public:
  websocket_rtty_server()
      : m_port(unused_tcp_port()),
        m_config({
            .m_address       = rstream::io::address(std::string("127.0.0.1:") + std::to_string(m_port)),
            .m_protocol_type = rstream::rtty::protocol::type::websocket,
        }),
        m_settings({
            .m_common = {
                .m_mtu         = 1024 * 1024,
                .m_timeouts_ms = {
                    .m_open      = 5000,
                    .m_close     = 5000,
                    .m_heartbeat = 0,
                },
            },
            .m_timeouts_start_ms   = 5000,
            .m_std_out_buffer_size = 64 * 1024,
            .m_std_err_buffer_size = 64 * 1024,
        }),
        m_server(std::make_shared<rstream::rtty::server>(m_io_context.get_executor(), m_config, m_settings))
  {
  }

  ~websocket_rtty_server()
  {
    stop();
  }

  unsigned short port() const
  {
    return m_port;
  }

  void start()
  {
    m_server->async_run([this](const std::error_code& error_code) {
      m_result = error_code;
      m_done   = true;
    });
    m_thread = std::thread([this] {
      try {
        m_io_context.run();
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void stop()
  {
    if (!m_thread.joinable()) {
      return;
    }
    m_server->cancel();
    m_thread.join();
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
    assert(m_done);
    assert(!m_result);
  }

 private:
  unsigned short m_port;
  rstream::rtty::server::config m_config;
  rstream::rtty::settings_server m_settings;
  boost::asio::io_context m_io_context;
  std::shared_ptr<rstream::rtty::server> m_server;
  std::thread m_thread;
  std::exception_ptr m_exception;
  bool m_done = false;
  std::error_code m_result;
};

static void read_exact(tcp::socket& socket, void* data, std::size_t size)
{
  std::size_t offset = 0;
  while (offset < size) {
    boost::system::error_code error_code;
    auto bytes = socket.read_some(boost::asio::buffer(static_cast<char*>(data) + offset, size - offset), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read");
    }
    offset += bytes;
  }
}

static void write_all(tcp::socket& socket, const void* data, std::size_t size)
{
  boost::asio::write(socket, boost::asio::buffer(data, size));
}

static void websocket_handshake(tcp::socket& socket)
{
  const std::string request =
      "GET /api/websocket HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  boost::asio::write(socket, boost::asio::buffer(request));

  std::string response;
  std::array<char, 256> buffer{};
  while (response.find("\r\n\r\n") == std::string::npos) {
    boost::system::error_code error_code;
    auto bytes = socket.read_some(boost::asio::buffer(buffer), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read websocket handshake");
    }
    response.append(buffer.data(), bytes);
    assert(response.size() <= 8192);
  }
  assert(response.find(" 101 ") != std::string::npos);
}

static tcp::socket connect_with_retry(boost::asio::io_context& io_context, unsigned short port)
{
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  boost::system::error_code error_code;
  do {
    tcp::socket socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      try {
        websocket_handshake(socket);
        return socket;
      }
      catch (const boost::system::system_error& error) {
        error_code = error.code();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect websocket rtty client: " << error_code.message() << std::endl;
  assert(false);
  return tcp::socket(io_context);
}

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  std::vector<char> payload(message.ByteSizeLong());
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));

  std::vector<unsigned char> frame;
  frame.push_back(0x82);
  if (payload.size() < 126) {
    frame.push_back(static_cast<unsigned char>(0x80 | payload.size()));
  }
  else {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xff));
    frame.push_back(static_cast<unsigned char>(payload.size() & 0xff));
  }
  const std::array<unsigned char, 4> mask = {0x11, 0x22, 0x33, 0x44};
  frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<unsigned char>(payload[i]) ^ mask[i % mask.size()]);
  }
  write_all(socket, frame.data(), frame.size());
}

static std::optional<protobuf::Message> read_message(tcp::socket& socket)
{
  while (true) {
    std::array<unsigned char, 2> header{};
    read_exact(socket, header.data(), header.size());
    const auto opcode = header[0] & 0x0f;
    bool masked       = (header[1] & 0x80) != 0;
    std::uint64_t size = header[1] & 0x7f;
    if (size == 126) {
      std::array<unsigned char, 2> extended{};
      read_exact(socket, extended.data(), extended.size());
      size = (static_cast<std::uint64_t>(extended[0]) << 8) | extended[1];
    }
    else if (size == 127) {
      std::array<unsigned char, 8> extended{};
      read_exact(socket, extended.data(), extended.size());
      size = 0;
      for (auto byte : extended) {
        size = (size << 8) | byte;
      }
    }
    std::array<unsigned char, 4> mask{};
    if (masked) {
      read_exact(socket, mask.data(), mask.size());
    }
    assert(size <= 1024 * 1024);
    std::vector<char> payload(size);
    if (!payload.empty()) {
      read_exact(socket, payload.data(), payload.size());
    }
    if (masked) {
      for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % mask.size()]);
      }
    }
    if (opcode == 0x8) {
      return std::nullopt;
    }
    if (opcode != 0x2) {
      continue;
    }
    protobuf::Message message;
    assert(message.ParseFromArray(payload.data(), static_cast<int>(payload.size())));
    return message;
  }
}

static void check_websocket_server_runs_child_and_closes_normally(unsigned short port)
{
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, port);
  write_message(websocket, open_message({
                               "/bin/sh",
                               "-c",
                               "printf 'websocket-stdout'; exit 3",
                           }));

  bool saw_ack    = false;
  bool saw_stdout = false;
  bool saw_close  = false;
  while (!saw_close) {
    auto maybe_message = read_message(websocket);
    if (!maybe_message) {
      std::cerr << "websocket closed before protocol close; saw_ack=" << saw_ack << " saw_stdout=" << saw_stdout << std::endl;
      assert(maybe_message);
    }
    const auto& message = maybe_message.value();
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kAck:
        saw_ack = true;
        break;
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT && !message.data().has_eos() && message.data().data().find("websocket-stdout") != std::string::npos) {
          saw_stdout = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 3);
        saw_close = true;
        break;
      default:
        std::cerr << "unexpected websocket rtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }
  assert(saw_ack);
  assert(saw_stdout);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  websocket_rtty_server server;
  server.start();
  check_websocket_server_runs_child_and_closes_normally(server.port());
  return 0;
}
