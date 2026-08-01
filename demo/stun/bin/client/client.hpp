// See LICENSE file in the project root for license information.

#include <functional>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/optional.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/error.hpp>
#include <rstream/stun/client.hpp>

class client : public std::enable_shared_from_this<client> {
 public:
  struct config {
    std::string m_host;
    std::string m_port;
    unsigned int m_timeout_ms;
    boost::optional<boost::asio::ip::address> m_address;
    bool m_inet4;
    bool m_inet6;
  };

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, const boost::asio::ip::address&)>;

  client(const boost::asio::io_context::executor_type& executor, const config& config);

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  void clean();
  void do_resolve_host();
  void on_do_resolve_host(const boost::system::error_code& error_code, const boost::asio::ip::udp::resolver::results_type results);
  void do_send_stun_request(const boost::asio::ip::udp::endpoint& endpoint);
  void on_stun_response(const boost::system::error_code& error_code, const rstream::stun::message& message);
  void setup_timeout();
  void on_timer_cb(const boost::system::error_code& error_code);
  void on_error(const boost::system::error_code& error_code);
  void on_result(const boost::asio::ip::address& address);

  const config m_config;
  bool m_complete;
  boost::asio::io_context::executor_type m_executor;
  boost::asio::ip::udp::resolver m_resolver;
  boost::asio::ip::udp::socket m_socket;
  boost::asio::steady_timer m_timer;
  rstream::stun::client<boost::asio::ip::udp::socket&> m_client;
  async_run_completion_handler m_handler;
  boost::system::error_code m_error_code;
};
