// See LICENSE file in the project root for license information.

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/basic_stream.hpp>
#include <boost/make_shared.hpp>

#include <docopt.h>

#include <rstream/config.hpp>

static const char USAGE[] = R"(
rstream-benchmark-io-socket-client

example:
    rstream-benchmark-io-socket-client 127.0.0.1 4000 1024 1 0 10000

usage:
    rstream-benchmark-io-socket-client <host> <service> <block_size> <session_count> <thread_count> <timeout_ms>
    rstream-benchmark-io-socket-client (-h|--help)
    rstream-benchmark-io-socket-client --version

options:
    -h --help       show this screen
    --version       show version
)";

const auto version = std::string("rstream-benchmark-io-socket-client ") + RSTREAM_VERSION;

class stats {
 public:
  stats(unsigned int timeout_ms)
      : m_total_bytes_written(0),
        m_total_bytes_read(0),
        m_timeout_ms(timeout_ms)
  {
  }
  void add(std::size_t bytes_written, std::size_t bytes_read)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_total_bytes_written += bytes_written;
    m_total_bytes_read += bytes_read;
  }
  void print()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << m_total_bytes_written << " byte(s) written" << std::endl;
    std::cout << m_total_bytes_read << " total byte(s) read" << std::endl;
    std::cout << static_cast<double>(m_total_bytes_read) / (m_timeout_ms * 1024) << " MiB/s throughput\n";
  }

 private:
  std::mutex m_mutex;
  std::size_t m_total_bytes_written;
  std::size_t m_total_bytes_read;
  const unsigned int m_timeout_ms;
};

class session : public std::enable_shared_from_this<session> {
 public:
  using ptr = std::shared_ptr<session>;

  session(const boost::asio::io_context::executor_type& executor, std::size_t block_size, stats& stats)
      : m_strand(executor),
        m_stream(executor),
        m_stats(stats),
        m_block_size(block_size),
        m_read_data_length(0),
        m_bytes_written(0),
        m_bytes_read(0),
        m_unwritten_count(0)
  {
    // m_stream.rate_policy().read_limit(8000000);
    // m_stream.rate_policy().write_limit(800000);
    m_write_data = boost::make_shared<std::uint8_t[]>(block_size);
    m_read_data  = boost::make_shared<std::uint8_t[]>(block_size);
    for (std::size_t i = 0; i < m_block_size; ++i) {
      m_write_data[i] = static_cast<char>(i % 128);
    }
  }
  virtual ~session()
  {
    m_stats.add(m_bytes_written, m_bytes_read);
  }
  void start(const boost::asio::ip::tcp::resolver::results_type endpoint_iterator)
  {
    auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_connect, shared_from_this(), std::placeholders::_1));
    m_stream.async_connect(endpoint_iterator, handler);
  }
  void stop()
  {
    boost::asio::post(m_strand, std::bind_front(&session::close, shared_from_this()));
  }

 private:
  void on_async_connect(const boost::system::error_code& error_code)
  {
    if (!error_code) {
      boost::system::error_code set_option_err;
      boost::asio::ip::tcp::no_delay no_delay(true);
      m_stream.socket().set_option(no_delay, set_option_err);
      if (!set_option_err) {
        ++m_unwritten_count;
        do_write_data(m_block_size);
      }
    }
  }
  void on_async_read(const boost::system::error_code& error_code, std::size_t length)
  {
    if (!error_code) {
      m_bytes_read += length;
      m_read_data_length = length;
      ++m_unwritten_count;
      if (m_unwritten_count == 1) {
        std::swap(m_read_data, m_write_data);
        do_write_data(m_read_data_length);
      }
    }
  }
  void on_async_write(const boost::system::error_code& error_code, std::size_t length)
  {
    if (!error_code && length > 0) {
      m_bytes_written += length;
      --m_unwritten_count;
      if (m_unwritten_count == 1) {
        std::swap(m_read_data, m_write_data);
        do_write_data(m_read_data_length);
      }
    }
  }
  void do_write_data(std::size_t length)
  {
    {
      auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
      boost::asio::async_write(m_stream, boost::asio::buffer(m_write_data.get(), length), handler);
    }
    {
      auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
      boost::asio::async_read(m_stream, boost::asio::buffer(m_read_data.get(), m_block_size), handler);
    }
  }
  void close()
  {
    m_stream.close();
  }
  boost::asio::strand<boost::asio::io_context::executor_type> m_strand;
  boost::beast::basic_stream<boost::asio::ip::tcp, boost::asio::any_io_executor, boost::beast::simple_rate_policy> m_stream;
  stats& m_stats;
  const std::size_t m_block_size;
  std::size_t m_read_data_length;
  std::size_t m_bytes_written;
  std::size_t m_bytes_read;
  int m_unwritten_count;
  boost::shared_ptr<std::uint8_t[]> m_write_data;
  boost::shared_ptr<std::uint8_t[]> m_read_data;
};

class client {
 public:
  client(const boost::asio::io_context::executor_type& executor,
         const boost::asio::ip::tcp::resolver::results_type endpoint_iterator,
         std::size_t block_size, std::size_t session_count, unsigned int timeout_ms)
      : m_stop_timer(executor),
        m_stats(timeout_ms)
  {
    m_stop_timer.expires_after(std::chrono::milliseconds(timeout_ms));
    m_stop_timer.async_wait(std::bind(&client::on_timer, this, std::placeholders::_1));
    for (std::size_t i = 0; i < session_count; ++i) {
      auto session = std::make_shared<class session>(executor, block_size, m_stats);
      session->start(endpoint_iterator);
      m_sessions.push_back(session);
    }
  }
  void print_stats()
  {
    m_stats.print();
  }

 private:
  void on_timer(const boost::system::error_code& error_code)
  {
    if (error_code) {
      return;
    }
    std::for_each(m_sessions.begin(), m_sessions.end(), std::mem_fn(&session::stop));
    m_sessions.clear();
  }
  boost::asio::steady_timer m_stop_timer;
  std::list<session::ptr> m_sessions;
  stats m_stats;
};

int main(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  struct config {
    std::string m_host;
    std::string m_service;
    std::size_t m_block_size;
    std::size_t m_session_count;
    std::size_t m_thread_count;
    unsigned int m_timeout_ms;
  };
  struct config config = {
      .m_host          = args.at("<host>").asString(),
      .m_service       = args.at("<service>").asString(),
      .m_block_size    = static_cast<size_t>(args.at("<block_size>").asLong()),
      .m_session_count = static_cast<size_t>(args.at("<session_count>").asLong()),
      .m_thread_count  = static_cast<size_t>(args.at("<thread_count>").asLong()),
      .m_timeout_ms    = static_cast<unsigned int>(args.at("<timeout_ms>").asLong()),
  };
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::resolver resolver(io_context);
  auto endpoint_iterator = resolver.resolve(config.m_host, config.m_service);
  class client client(io_context.get_executor(), endpoint_iterator, config.m_block_size, config.m_session_count, config.m_timeout_ms);
  std::list<std::thread> threads;
  for (std::size_t i = 0; i < config.m_thread_count; ++i) {
    threads.push_back(std::thread(std::bind((boost::asio::io_context::count_type (boost::asio::io_context::*)())&boost::asio::io_context::run, &io_context)));
  }
  io_context.run();
  for (auto it = threads.begin(); it != threads.end();) {
    it->join();
    it = threads.erase(it);
  }
  client.print_stats();
  return 0;
}
