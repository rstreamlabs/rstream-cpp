// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>

#include <rstream/core/metrics.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/detail/metrics/error.hpp>
#include <rstream/io/metrics.hpp>

using tcp = boost::asio::ip::tcp;

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static boost::beast::http::response<boost::beast::http::string_body> request(unsigned short port, boost::beast::http::verb verb, const std::string& target)
{
  boost::asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  boost::asio::connect(socket, resolver.resolve("127.0.0.1", std::to_string(port)));

  boost::beast::http::request<boost::beast::http::string_body> req{verb, target, 11};
  req.set(boost::beast::http::field::host, "127.0.0.1");
  req.set(boost::beast::http::field::connection, "close");
  boost::beast::http::write(socket, req);

  boost::beast::flat_buffer buffer;
  boost::beast::http::response<boost::beast::http::string_body> res;
  boost::beast::http::read(socket, buffer, res);
  return res;
}

static boost::beast::http::response<boost::beast::http::string_body> request_with_retry(unsigned short port, boost::beast::http::verb verb, const std::string& target)
{
  std::exception_ptr last_exception;
  for (int i = 0; i < 250; ++i) {
    try {
      return request(port, verb, target);
    }
    catch (...) {
      last_exception = std::current_exception();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  std::rethrow_exception(last_exception);
}

static void assert_metrics_error(const boost::system::error_code& actual, rstream::io::detail::metrics::error::code expected)
{
  assert(actual.category() == rstream::io::detail::metrics::error::rstream_io_detail_metrics_error_category());
  assert(actual.value() == static_cast<int>(expected));
}

static void check_metrics_exposer_http_paths()
{
  boost::asio::io_context io_context;
  const auto port = unused_tcp_port();

  auto registry = std::make_shared<rstream::core::metrics::registry>();
  auto counter  = rstream::core::metrics::counter("rstream_exposer_test_total", "exposer test counter", {}, registry);
  counter.increment(3.0);

  rstream::io::metrics::exposer::config config;
  config.m_address = rstream::io::make_address("127.0.0.1:" + std::to_string(port));
  rstream::io::metrics::settings_exposer settings{.m_timeouts_start_ms = 10000};
  rstream::io::metrics::exposer exposer(io_context.get_executor(), config, settings);
  exposer.add_collectable(registry, "/custom");

  boost::system::error_code run_error;
  bool stopped = false;
  exposer.async_run([&](const boost::system::error_code& error) {
    run_error = error;
    stopped   = true;
  });
  std::thread thread([&] { io_context.run(); });

  auto ok = request_with_retry(port, boost::beast::http::verb::get, "/custom");
  assert(ok.result() == boost::beast::http::status::ok);
  assert(ok[boost::beast::http::field::content_type] == "text/plain");
  assert(ok.body().find("rstream_exposer_test_total") != std::string::npos);

  auto missing = request_with_retry(port, boost::beast::http::verb::get, "/missing");
  assert(missing.result() == boost::beast::http::status::not_found);
  assert(missing.body().find("Not Found") != std::string::npos);

  auto bad_method = request_with_retry(port, boost::beast::http::verb::post, "/custom");
  assert(bad_method.result() == boost::beast::http::status::bad_request);
  assert(bad_method.body().find("Bad Request") != std::string::npos);

  auto after_internal_metrics = request_with_retry(port, boost::beast::http::verb::get, "/metrics");
  assert(after_internal_metrics.result() == boost::beast::http::status::ok);
  assert(after_internal_metrics.body().find("metrics_http_requests_total") != std::string::npos);
  assert(after_internal_metrics.body().find("code=\"200\"") != std::string::npos);
  assert(after_internal_metrics.body().find("code=\"404\"") != std::string::npos);
  assert(after_internal_metrics.body().find("code=\"400\"") != std::string::npos);

  exposer.cancel();
  thread.join();
  assert(stopped);
  assert(!run_error);
}

static void check_metrics_exposer_rejects_double_start()
{
  boost::asio::io_context io_context;
  const auto port = unused_tcp_port();

  rstream::io::metrics::exposer::config config;
  config.m_address = rstream::io::make_address("127.0.0.1:" + std::to_string(port));
  rstream::io::metrics::settings_exposer settings{.m_timeouts_start_ms = 10000};
  rstream::io::metrics::exposer exposer(io_context.get_executor(), config, settings);

  boost::system::error_code second_start;
  bool saw_second_start = false;
  exposer.async_run([](const boost::system::error_code&) {});
  exposer.async_run([&](const boost::system::error_code& error) {
    second_start     = error;
    saw_second_start = true;
  });
  io_context.run_for(std::chrono::milliseconds(50));
  exposer.cancel();
  io_context.run();
  assert(saw_second_start);
  assert_metrics_error(second_start, rstream::io::detail::metrics::error::code::invalid_state);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_metrics_exposer_http_paths();
  check_metrics_exposer_rejects_double_start();
  return 0;
}
