// See LICENSE file in the project root for license information.

#include <cstdint>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <thread>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/basic_stream.hpp>
#include <boost/make_shared.hpp>

#include <docopt.h>

#include <rstream/config.hpp>

static const char USAGE[] = R"(
rstream-benchmark-io-socket-server

example:
    rstream-benchmark-io-socket-server 127.0.0.1 4000 1024 0

usage:
    rstream-benchmark-io-socket-server <host> <service> <block_size> <thread_count>
    rstream-benchmark-io-socket-server (-h|--help)
    rstream-benchmark-io-socket-server --version

options:
    -h --help       show this screen
    --version       show version
)";

const auto version = std::string("rstream-benchmark-io-socket-server ") + RSTREAM_VERSION;

class session : public std::enable_shared_from_this<session> {
 public:
  using ptr = std::shared_ptr<session>;

  session(const boost::asio::io_context::executor_type& executor, std::size_t block_size)
      : m_strand(executor),
        m_stream(executor),
        m_block_size(block_size),
        m_read_data_length(0),
        m_unsent_count(0)

  {
    m_write_data = boost::make_shared<std::uint8_t[]>(block_size);
    m_read_data  = boost::make_shared<std::uint8_t[]>(block_size);
  }
  boost::asio::ip::tcp::socket& socket()
  {
    return m_stream.socket();
  }
  void start()
  {
    boost::system::error_code set_option_err;
    boost::asio::ip::tcp::no_delay no_delay(true);
    m_stream.socket().set_option(no_delay, set_option_err);
    if (!set_option_err) {
      auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
      m_stream.async_read_some(boost::asio::buffer(m_read_data.get(), m_block_size), handler);
    }
  }

 private:
  void on_async_read(const boost::system::error_code& error_code, std::size_t length)
  {
    if (!error_code) {
      m_read_data_length = length;
      ++m_unsent_count;
      if (m_unsent_count == 1) {
        std::swap(m_read_data, m_write_data);
        {
          auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
          boost::asio::async_write(m_stream, boost::asio::buffer(m_write_data.get(), m_read_data_length), handler);
        }
        {
          auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
          m_stream.async_read_some(boost::asio::buffer(m_read_data.get(), m_block_size), handler);
        }
      }
    }
  }
  void on_async_write(const boost::system::error_code& error_code, std::size_t length)
  {
    if (!error_code && length > 0) {
      --m_unsent_count;
      if (m_unsent_count == 1) {
        std::swap(m_read_data, m_write_data);
        {
          auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
          boost::asio::async_write(m_stream, boost::asio::buffer(m_write_data.get(), m_read_data_length), handler);
        }
        {
          auto handler = boost::asio::bind_executor(m_strand, std::bind(&session::on_async_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
          m_stream.async_read_some(boost::asio::buffer(m_read_data.get(), m_block_size), handler);
        }
      }
    }
  }
  boost::asio::strand<boost::asio::io_context::executor_type> m_strand;
  boost::beast::basic_stream<boost::asio::ip::tcp, boost::asio::any_io_executor, boost::beast::simple_rate_policy> m_stream;
  const std::size_t m_block_size;
  std::size_t m_read_data_length;
  int m_unsent_count;
  boost::shared_ptr<std::uint8_t[]> m_write_data;
  boost::shared_ptr<std::uint8_t[]> m_read_data;
};

class server {
 public:
  server(const boost::asio::io_context::executor_type& executor,
         const boost::asio::ip::tcp::resolver::results_type endpoint_iterator,
         std::size_t block_size)
      : m_executor(executor),
        m_acceptor(executor),
        m_block_size(block_size)
  {
    auto endpoint = endpoint_iterator.begin()->endpoint();
    m_acceptor.open(endpoint.protocol());
    m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    m_acceptor.bind(endpoint);
    m_acceptor.listen();
    do_accept();
  }

 private:
  void do_accept()
  {
    auto session = std::make_shared<class session>(m_executor, m_block_size);
    m_acceptor.async_accept(session->socket(), std::bind(&server::on_async_accept, this, std::placeholders::_1, session));
  }
  void on_async_accept(const boost::system::error_code& error_code, session::ptr session)
  {
    if (!error_code) {
      session->start();
      do_accept();
    }
  }
  const boost::asio::io_context::executor_type& m_executor;
  boost::asio::ip::tcp::acceptor m_acceptor;
  const std::size_t m_block_size;
};

int main(int argc, char** argv)
{
  auto args = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  struct config {
    std::string m_host;
    std::string m_service;
    std::size_t m_block_size;
    std::size_t m_thread_count;
  };
  struct config config = {
      .m_host         = args.at("<host>").asString(),
      .m_service      = args.at("<service>").asString(),
      .m_block_size   = static_cast<size_t>(args.at("<block_size>").asLong()),
      .m_thread_count = static_cast<size_t>(args.at("<thread_count>").asLong()),
  };
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::resolver resolver(io_context);
  auto endpoint_iterator = resolver.resolve(config.m_host, config.m_service);
  class server server(io_context.get_executor(), endpoint_iterator, config.m_block_size);
  std::list<std::thread> threads;
  for (std::size_t i = 0; i < config.m_thread_count; ++i) {
    threads.push_back(std::thread(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context)));
  }
  io_context.run();
  for (auto it = threads.begin(); it != threads.end();) {
    it->join();
    it = threads.erase(it);
  }
  return 0;
}
