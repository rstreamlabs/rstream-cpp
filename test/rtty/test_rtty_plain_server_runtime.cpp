// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <arpa/inet.h>
#include <sys/socket.h>

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

static void set_receive_timeout(tcp::socket& socket)
{
  timeval timeout = {
      .tv_sec  = 5,
      .tv_usec = 0,
  };
  auto rc = setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  assert(rc == 0);
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

static protobuf::Message read_message(tcp::socket& socket)
{
  std::uint32_t frame_size = 0;
  read_exact(socket, &frame_size, sizeof(frame_size));
  const auto size = ntohl(frame_size);
  assert(size <= 1024 * 1024);
  std::vector<char> payload(size);
  if (!payload.empty()) {
    read_exact(socket, payload.data(), payload.size());
  }
  protobuf::Message message;
  const auto parsed = message.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
  assert(parsed);
  return message;
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

static protobuf::Message data_message(protobuf::Data::Type type, const std::string& data)
{
  protobuf::Message message;
  auto* payload = message.mutable_data();
  payload->set_type(type);
  payload->set_data(data);
  return message;
}

static protobuf::Message eos_message(protobuf::Data::Type type)
{
  protobuf::Message message;
  auto* payload = message.mutable_data();
  payload->set_type(type);
  payload->mutable_eos();
  return message;
}

class plain_rtty_server {
 public:
  plain_rtty_server()
      : m_port(unused_tcp_port()),
        m_config({
            .m_address       = rstream::io::address(std::string("127.0.0.1:") + std::to_string(m_port)),
            .m_protocol_type = rstream::rtty::protocol::type::plain,
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

  ~plain_rtty_server()
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

static tcp::socket connect_with_retry(boost::asio::io_context& io_context, unsigned short port)
{
  tcp::socket socket(io_context);
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  boost::system::error_code error_code;
  do {
    socket.close();
    socket = tcp::socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      set_receive_timeout(socket);
      return socket;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect to rtty server: " << error_code.message() << std::endl;
  assert(false);
  return socket;
}

static void check_invalid_command_returns_protocol_error_message(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({"definitely-not-a-rstream-test-command"}));
  auto message = read_message(socket);
  assert(message.payload_case() == protobuf::Message::PayloadCase::kError);
  assert(!message.error().msg().empty());
}

static void check_plain_server_runs_child_and_streams_stdout_stderr(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({
                            "/bin/sh",
                            "-c",
                            "printf 'stdout-data'; printf 'stderr-data' >&2; exit 7",
                        }));

  bool saw_ack        = false;
  bool saw_stdout     = false;
  bool saw_stderr     = false;
  bool saw_stdout_eos = false;
  bool saw_stderr_eos = false;
  bool saw_close      = false;

  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kAck:
        saw_ack = true;
        break;
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT) {
          if (message.data().has_eos()) {
            saw_stdout_eos = true;
          }
          else if (message.data().data().find("stdout-data") != std::string::npos) {
            saw_stdout = true;
          }
        }
        else if (message.data().type() == protobuf::Data::TYPE_STDERR) {
          if (message.data().has_eos()) {
            saw_stderr_eos = true;
          }
          else if (message.data().data().find("stderr-data") != std::string::npos) {
            saw_stderr = true;
          }
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 7);
        saw_close = true;
        break;
      default:
        std::cerr << "unexpected rtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_ack);
  assert(saw_stdout);
  assert(saw_stderr);
  assert(saw_stdout_eos);
  assert(saw_stderr_eos);
}

static void check_plain_server_forwards_stdin_to_child_process(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({"/bin/cat"}));

  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);

  write_message(socket, data_message(protobuf::Data::TYPE_STDIN, "stdin-through-rtty\n"));
  write_message(socket, eos_message(protobuf::Data::TYPE_STDIN));

  bool saw_stdout     = false;
  bool saw_stdout_eos = false;
  bool saw_stderr_eos = false;
  bool saw_close      = false;

  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT) {
          if (message.data().has_eos()) {
            saw_stdout_eos = true;
          }
          else if (message.data().data().find("stdin-through-rtty") != std::string::npos) {
            saw_stdout = true;
          }
        }
        else if (message.data().type() == protobuf::Data::TYPE_STDERR && message.data().has_eos()) {
          saw_stderr_eos = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 0);
        saw_close = true;
        break;
      default:
        std::cerr << "unexpected rtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_stdout);
  assert(saw_stdout_eos);
  assert(saw_stderr_eos);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  plain_rtty_server server;
  server.start();
  check_invalid_command_returns_protocol_error_message(server.port());
  check_plain_server_runs_child_and_streams_stdout_stderr(server.port());
  check_plain_server_forwards_stdin_to_child_process(server.port());
  return 0;
}
