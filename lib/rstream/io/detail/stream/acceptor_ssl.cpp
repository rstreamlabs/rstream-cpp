// See LICENSE file in the project root for license information.

#include "acceptor_ssl.hpp"

#include <map>
#include <memory>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>

#include <rstream/config.hpp>
#include <rstream/core/allocator.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/object_id.hpp>

#include "error.hpp"
#include "object_base.hpp"
#include "stream_socket_ssl.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

static const unsigned long g_max_ongoing_upstream_ops = 10;

class RSTREAM_GNUC_INTERNAL acceptor_ssl::impl : public std::enable_shared_from_this<impl> {
 public:
  using ptr = std::shared_ptr<impl>;

  impl(acceptor_ptr next_layer, const ssl::config& config, core::allocator::ptr allocator);

  virtual ~impl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  void close(boost::system::error_code& error_code);

  void bind(const endpoint& endpoint, boost::system::error_code& error_code);

  void listen(int backlog, boost::system::error_code& error_code);

  endpoint local_endpoint(boost::system::error_code& error_code);

  void async_accept(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  acceptor_ptr next_layer() const;

  acceptor_ptr next_layer();

 private:
  struct async_accept_downstream_op_type {
    using ptr = std::shared_ptr<async_accept_downstream_op_type>;
    async_accept_downstream_op_type(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);
    stream_socket& m_peer;
    endpoint& m_endpoint;
    async_accept_completion_handler m_handler;
  };

  struct async_accept_upstream_op_type {
    using ptr = std::shared_ptr<async_accept_upstream_op_type>;
    using id  = std::string;
    async_accept_upstream_op_type(const executor_type& executor, core::allocator::ptr allocator);
    bool m_connected;
    stream_socket m_peer;
    endpoint m_endpoint;
  };

  using async_accept_upstream_ops_type = std::map<async_accept_upstream_op_type::id, async_accept_upstream_op_type::ptr>;

  void async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  void do_accept();

  void do_accept(async_accept_upstream_ops_type::iterator& it);

  void on_accept(const async_accept_upstream_op_type::id& id, const boost::system::error_code& error_code);

  void do_handshake(const async_accept_upstream_op_type::id& id);

  void on_handshake(const async_accept_upstream_op_type::id& id, const boost::system::error_code& error_code);

  void close_internal();

  void on_error(const boost::system::error_code& error_code);

  static std::string generate_id();

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const ssl::config m_config;

  core::allocator::ptr m_allocator;

  rstream::core::logger m_logger;

  acceptor_ptr m_next_layer;

  bool m_ongoing_accept_op;

  bool m_closed;

  async_accept_downstream_op_type::ptr m_async_accept_downstream_op;

  async_accept_upstream_ops_type m_async_accept_upstream_ops;
};

acceptor_ssl::acceptor_ssl(acceptor_ptr next_layer, const ssl::config& config)
    : acceptor_base(next_layer->get_executor()),
      m_impl(std::make_shared<impl>(next_layer, config, nullptr))
{
}

void acceptor_ssl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->open(endpoint, error_code);
}

void acceptor_ssl::close(boost::system::error_code& error_code)
{
  m_impl->close(error_code);
}

void acceptor_ssl::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->bind(endpoint, error_code);
}

void acceptor_ssl::listen(int backlog, boost::system::error_code& error_code)
{
  m_impl->listen(backlog, error_code);
}

endpoint acceptor_ssl::local_endpoint(boost::system::error_code& error_code)
{
  return m_impl->local_endpoint(error_code);
}

acceptor_ptr acceptor_ssl::next_layer() const
{
  return m_impl->next_layer();
}

acceptor_ptr acceptor_ssl::next_layer()
{
  return m_impl->next_layer();
}

void acceptor_ssl::async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  m_impl->async_accept(peer, endpoint, std::move(handler));
}

acceptor_ssl::impl::async_accept_downstream_op_type::async_accept_downstream_op_type(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
    : m_peer(peer),
      m_endpoint(endpoint),
      m_handler(std::move(handler))
{
}

acceptor_ssl::impl::async_accept_upstream_op_type::async_accept_upstream_op_type(const executor_type& executor, core::allocator::ptr allocator)
    : m_connected(false),
      m_peer(executor)
{
}

acceptor_ssl::impl::impl(acceptor_ptr next_layer, const ssl::config& config, core::allocator::ptr allocator)
    : m_executor(next_layer->get_executor()),
      m_strand(m_executor),
      m_config(config),
      m_allocator(allocator),
      m_logger({"rstream", "io", "stream", "acceptor", fmt::format("#{}", fmt::ptr(this))}),
      m_next_layer(next_layer),
      m_ongoing_accept_op(false),
      m_closed(false)
{
}

void acceptor_ssl::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_next_layer->open(endpoint, error_code);
}

void acceptor_ssl::impl::close(boost::system::error_code& error_code)
{
  (void)error_code;
  boost::asio::dispatch(m_strand, std::bind_front(&impl::close_internal, shared_from_this()));
}

void acceptor_ssl::impl::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_next_layer->bind(endpoint, error_code);
}

void acceptor_ssl::impl::listen(int backlog, boost::system::error_code& error_code)
{
  m_next_layer->listen(backlog, error_code);
}

endpoint acceptor_ssl::impl::local_endpoint(boost::system::error_code& error_code)
{
  return m_next_layer->local_endpoint(error_code);
}

void acceptor_ssl::impl::async_accept(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_accept_internal, shared_from_this(), std::ref(peer), std::ref(endpoint), std::move(handler)));
}

acceptor_ptr acceptor_ssl::impl::next_layer() const
{
  return m_next_layer;
}

acceptor_ptr acceptor_ssl::impl::next_layer()
{
  return m_next_layer;
}

void acceptor_ssl::impl::async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  boost::system::error_code error_code;
  if (m_closed) {
    error_code = error::code::operation_aborted;
  }
  if (m_async_accept_downstream_op) {
    error_code = error::code::operation_in_progress;
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code);
  }
  else {
    async_accept_upstream_ops_type::iterator it = m_async_accept_upstream_ops.end();
    for (it = m_async_accept_upstream_ops.begin(); it != m_async_accept_upstream_ops.end(); ++it) {
      if (it->second->m_connected) {
        break;
      }
    }
    if (it != m_async_accept_upstream_ops.end()) {
      peer     = std::move(it->second->m_peer);  // TODO : Properly rebind executor
      endpoint = std::move(it->second->m_endpoint);
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), boost::system::error_code());
      it = m_async_accept_upstream_ops.erase(it);
    }
    else {
      m_async_accept_downstream_op = std::allocate_shared<async_accept_downstream_op_type>(core::allocator::wrapper<impl>(m_allocator), peer, endpoint, std::move(handler));
      do_accept();
    }
  }
}

void acceptor_ssl::impl::do_accept()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_ongoing_accept_op) {
    return;
  }
  const auto max_ongoing_upstream_ops = m_config.m_max_ongoing_upstream_ops.value_or(g_max_ongoing_upstream_ops);
  if (max_ongoing_upstream_ops != 0 && m_async_accept_upstream_ops.size() >= max_ongoing_upstream_ops) {
    return;
  }
  m_ongoing_accept_op           = true;
  auto async_accept_upstream_op = std::allocate_shared<async_accept_upstream_op_type>(core::allocator::wrapper<impl>(m_allocator), m_executor, m_allocator);
  auto it                       = m_async_accept_upstream_ops.insert(std::make_pair(generate_id(), async_accept_upstream_op)).first;
  do_accept(it);
}

void acceptor_ssl::impl::do_accept(async_accept_upstream_ops_type::iterator& it)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_accept, shared_from_this(), it->first, std::placeholders::_1);
  m_next_layer->async_accept(it->second->m_peer, it->second->m_endpoint, boost::asio::bind_executor(m_strand, completion_handler));
}

void acceptor_ssl::impl::on_accept(const async_accept_upstream_op_type::id& id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_closed) {
    return;
  }
  m_ongoing_accept_op = false;
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_handshake(id);
  }
}

void acceptor_ssl::impl::do_handshake(const async_accept_upstream_op_type::id& id)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  async_accept_upstream_ops_type::iterator it = m_async_accept_upstream_ops.find(id);
  if (it == m_async_accept_upstream_ops.end()) {
    return;
  }
  boost::system::error_code error_code;
  std::shared_ptr<stream_socket_ssl> stream_socket_ssl_ptr;
  try {
    stream_socket_ssl_ptr = stream_socket_ssl::wrap(it->second->m_peer, m_config, stream_socket_ssl::type::server);
  }
  catch (boost::system::system_error& system_error) {
    error_code = system_error.code();
  }
  catch (...) {
    error_code = error::code::generic_error;
  }
  auto completion_handler = std::bind(&impl::on_handshake, shared_from_this(), it->first, std::placeholders::_1);
  if (error_code) {
    completion_handler(error_code);
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->trace("handshaking with SSL peer...");
#endif
    stream_socket_ssl_ptr->async_handshake(boost::asio::bind_executor(m_strand, completion_handler));
  }
  do_accept();
}

void acceptor_ssl::impl::on_handshake(const async_accept_upstream_op_type::id& id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_closed) {
    return;
  }
  async_accept_upstream_ops_type::iterator it = m_async_accept_upstream_ops.find(id);
  if (it == m_async_accept_upstream_ops.end()) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("handshake with SSL peer completed [error_code: {}]", (error_code ? error_code.message() : "none"));
#endif
  if (!error_code) {
    if (m_async_accept_downstream_op) {
      m_async_accept_downstream_op->m_peer     = std::move(it->second->m_peer);  // TODO : Properly rebind executor
      m_async_accept_downstream_op->m_endpoint = std::move(it->second->m_endpoint);
      rstream::core::invoke_completion_handler(m_executor, std::move(m_async_accept_downstream_op->m_handler), error_code);
      it                           = m_async_accept_upstream_ops.erase(it);
      m_async_accept_downstream_op = nullptr;
    }
    else {
      it->second->m_connected = true;
    }
  }
  else {
    it = m_async_accept_upstream_ops.erase(it);
  }
  do_accept();
}

void acceptor_ssl::impl::close_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  on_error(error::code::operation_aborted);
}

void acceptor_ssl::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_closed) {
    return;
  }
  m_ongoing_accept_op = false;
  m_closed            = true;
  {
    boost::system::error_code tmp;
    m_next_layer->close(tmp);
  }
  for (auto it = m_async_accept_upstream_ops.begin(); it != m_async_accept_upstream_ops.end();) {
    {
      boost::system::error_code tmp;
      it->second->m_peer.close(tmp);
    }
    it = m_async_accept_upstream_ops.erase(it);
  }
  if (m_async_accept_downstream_op) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_async_accept_downstream_op->m_handler), error_code);
    m_async_accept_downstream_op = nullptr;
  }
}

std::string acceptor_ssl::impl::generate_id()
{
  return rstream::core::object_id();
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
