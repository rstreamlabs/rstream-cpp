// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/variant/get.hpp>

#include <rstream/nperf/client.hpp>
#include <rstream/nperf/error.hpp>
#include <rstream/nperf/server.hpp>

namespace nperf = rstream::nperf;
using tcp       = boost::asio::ip::tcp;

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static void wait_until_tcp_accepting(unsigned short port)
{
  boost::asio::io_context io_context;
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  boost::system::error_code error_code;
  do {
    tcp::socket socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      socket.close(error_code);
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect to nperf server: " << error_code.message() << std::endl;
  assert(false);
}

struct observed_metrics {
  unsigned int m_connection_samples = 0;
  unsigned int m_handshake_samples  = 0;
  unsigned int m_ping_samples       = 0;
  unsigned int m_speed_samples      = 0;
  unsigned int m_error_samples      = 0;
  unsigned int m_final_samples      = 0;
  unsigned int m_interim_samples    = 0;
  std::vector<nperf::options> m_final_options;
  std::uint64_t m_measured_bytes = 0;

  void observe(const nperf::metrics& metrics)
  {
    if (metrics.m_final) {
      ++m_final_samples;
      m_final_options.push_back(metrics.m_options);
    }
    else {
      ++m_interim_samples;
    }
    if (const auto* sample = boost::get<nperf::sample>(&metrics.m_data)) {
      if (sample->m_type == nperf::sample::type::connection) {
        ++m_connection_samples;
      }
      else if (sample->m_type == nperf::sample::type::handshake) {
        ++m_handshake_samples;
      }
      else if (sample->m_type == nperf::sample::type::ping) {
        ++m_ping_samples;
        assert(sample->m_size > 0);
      }
    }
    else if (const auto* speed = boost::get<nperf::speed>(&metrics.m_data)) {
      ++m_speed_samples;
      m_measured_bytes += speed->m_measured_bytes;
    }
    else if (boost::get<boost::system::error_code>(&metrics.m_data) != nullptr) {
      ++m_error_samples;
    }
  }

  bool saw_final_option(nperf::options option) const
  {
    return std::find(m_final_options.begin(), m_final_options.end(), option) != m_final_options.end();
  }
};

class nperf_server_fixture {
 public:
  explicit nperf_server_fixture(nperf::protocol protocol)
      : m_port(unused_tcp_port()),
        m_settings({
            .m_common = {
                .m_buffer_size            = 16 * 1024,
                .m_timeouts_max_time_ms   = 5000,
                .m_timeouts_open_close_ms = 5000,
                .m_protocol               = protocol,
            },
            .m_timeouts_start_ms = 5000,
        }),
        m_server(std::make_shared<nperf::server>(m_io_context.get_executor(), config(), m_settings))
  {
  }

  ~nperf_server_fixture()
  {
    stop();
  }

  nperf::server::config config() const
  {
    return {
        .m_address = rstream::io::address(std::string("127.0.0.1:") + std::to_string(m_port)),
    };
  }

  unsigned short port() const
  {
    return m_port;
  }

  void start()
  {
    m_server->async_run([this](const boost::system::error_code& error_code) {
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
    wait_until_tcp_accepting(m_port);
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
  nperf::settings_server m_settings;
  boost::asio::io_context m_io_context;
  std::shared_ptr<nperf::server> m_server;
  std::thread m_thread;
  std::exception_ptr m_exception;
  bool m_done = false;
  boost::system::error_code m_result;
};

static nperf::settings_client client_settings(nperf::protocol protocol)
{
  return {
      .m_common = {
          .m_buffer_size            = 16 * 1024,
          .m_timeouts_max_time_ms   = 5000,
          .m_timeouts_open_close_ms = 5000,
          .m_protocol               = protocol,
      },
      .m_execution_count   = 1,
      .m_max_ping          = 4,
      .m_period_metrics_ms = 0,
      .m_period_ms         = 0,
      .m_ping_buffer_size  = 32,
      .m_sessions          = 2,
      .m_max_data_bytes    = 64 * 1024,
      .m_retry             = false,
  };
}

static observed_metrics run_client(unsigned short port, nperf::protocol protocol, nperf::options options)
{
  boost::asio::io_context io_context;
  nperf::client::config config = {
      .m_address = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
  };
  auto settings = client_settings(protocol);
  nperf::client client(io_context.get_executor(), config, settings);
  observed_metrics observed;
  boost::system::error_code result;
  bool done      = false;
  bool timed_out = false;

  boost::asio::steady_timer deadline(io_context);
  deadline.expires_after(std::chrono::seconds(10));
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code && !done) {
      timed_out = true;
      client.cancel();
    }
  });

  nperf::client::callbacks callbacks = {
      .m_on_metrics_cb = [&](const nperf::metrics& metrics) {
        observed.observe(metrics);
      },
  };
  client.async_run(options, callbacks, [&](const boost::system::error_code& error_code) {
    result = error_code;
    done   = true;
    deadline.cancel();
  });
  io_context.run();
  assert(done);
  assert(!timed_out);
  assert(!result);
  return observed;
}

static void assert_all_modes_were_measured(const observed_metrics& observed)
{
  assert(observed.m_error_samples == 0);
  assert(observed.m_connection_samples == 3);
  assert(observed.m_handshake_samples == 3);
  assert(observed.m_ping_samples == 1);
  assert(observed.m_speed_samples == 2);
  assert(observed.m_measured_bytes >= 64 * 1024 * 2);
  assert(observed.saw_final_option(nperf::option::ping));
  assert(observed.saw_final_option(nperf::option::download));
  assert(observed.saw_final_option(nperf::option::upload));
}

static void check_plain_client_server_all_modes()
{
  nperf_server_fixture server(nperf::protocol::plain);
  server.start();
  auto observed = run_client(server.port(), nperf::protocol::plain, nperf::option::ping | nperf::option::download | nperf::option::upload);
  assert_all_modes_were_measured(observed);
}

static void check_websocket_client_server_all_modes()
{
  nperf_server_fixture server(nperf::protocol::websocket);
  server.start();
  auto observed = run_client(server.port(), nperf::protocol::websocket, nperf::option::ping | nperf::option::download | nperf::option::upload);
  assert_all_modes_were_measured(observed);
}

static void check_client_rejects_empty_options()
{
  boost::asio::io_context io_context;
  nperf::client::config config = {
      .m_address = rstream::io::address(std::string("127.0.0.1:") + std::to_string(unused_tcp_port())),
  };
  auto settings = client_settings(nperf::protocol::plain);
  nperf::client client(io_context.get_executor(), config, settings);
  boost::system::error_code result;
  bool done = false;
  client.async_run(0, {}, [&](const boost::system::error_code& error_code) {
    result = error_code;
    done   = true;
  });
  io_context.run();
  assert(done);
  assert(result == nperf::error::make_error_code(nperf::error::code::invalid_argument));
}

static void check_client_cancel_stops_running_measurement()
{
  nperf_server_fixture server(nperf::protocol::plain);
  server.start();

  boost::asio::io_context io_context;
  nperf::client::config config = {
      .m_address = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
  };
  auto settings             = client_settings(nperf::protocol::plain);
  settings.m_sessions       = 1;
  settings.m_max_data_bytes = 4UL * 1024UL * 1024UL * 1024UL;
  nperf::client client(io_context.get_executor(), config, settings);

  boost::system::error_code result;
  bool done                          = false;
  bool timed_out                     = false;
  unsigned int metrics_count         = 0;
  nperf::client::callbacks callbacks = {
      .m_on_metrics_cb = [&](const nperf::metrics&) {
        ++metrics_count;
      },
  };

  boost::asio::steady_timer cancel_timer(io_context);
  cancel_timer.expires_after(std::chrono::milliseconds(20));
  cancel_timer.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code) {
      client.cancel();
    }
  });

  boost::asio::steady_timer deadline(io_context);
  deadline.expires_after(std::chrono::seconds(5));
  deadline.async_wait([&](const boost::system::error_code& error_code) {
    if (!error_code && !done) {
      timed_out = true;
      client.cancel();
    }
  });

  client.async_run(nperf::option::download, callbacks, [&](const boost::system::error_code& error_code) {
    result = error_code;
    done   = true;
    deadline.cancel();
  });
  io_context.run();
  assert(done);
  assert(!timed_out);
  assert(result == nperf::error::make_error_code(nperf::error::code::operation_aborted));
}

static void check_websocket_server_rejects_unknown_target()
{
  nperf_server_fixture server(nperf::protocol::websocket);
  server.start();

  boost::asio::io_context io_context;
  tcp::socket socket(io_context);
  socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), server.port()));
  const std::string request =
      "GET /not-a-mode HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  boost::asio::write(socket, boost::asio::buffer(request));
  std::array<char, 256> buffer{};
  boost::system::error_code error_code;
  auto size = socket.read_some(boost::asio::buffer(buffer), error_code);
  assert(!error_code || error_code == boost::asio::error::eof);
  const std::string response(buffer.data(), size);
  assert(response.find("400 Bad Request") != std::string::npos);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_client_rejects_empty_options();
  check_client_cancel_stops_running_measurement();
  check_plain_client_server_all_modes();
  check_websocket_client_server_all_modes();
  check_websocket_server_rejects_unknown_target();
  return 0;
}
