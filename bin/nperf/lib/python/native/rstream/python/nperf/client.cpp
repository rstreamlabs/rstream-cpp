// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <boost/asio/thread_pool.hpp>

#include <rstream/config.hpp>

namespace rstream {
namespace python {
namespace nperf {

class RSTREAM_GNUC_INTERNAL client::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const rstream::nperf::client::config& config, const rstream::nperf::settings_client& settings, rstream::nperf::options options, unsigned int jobs);

  virtual ~impl();

  void start();

  void stop();

  data get();

 private:
  void on_metrics(const rstream::nperf::metrics& metrics);

  void on_complete(const boost::system::error_code& error_code);

  enum class state { null,
                     running,
                     completed };

  const rstream::nperf::options m_options;

  boost::asio::thread_pool m_thread_pool;

  rstream::nperf::client m_impl;

  state m_state;

  std::mutex m_mutex;

  boost::system::error_code m_error_code;

  std::deque<data> m_queue;

  std::condition_variable m_cv;

  std::shared_ptr<void> m_executor_work_guard;
};

client::client(const rstream::nperf::client::config& config, const rstream::nperf::settings_client& settings, rstream::nperf::options options, unsigned int jobs)
{
  m_impl = std::make_shared<impl>(config, settings, options, jobs);
}

void client::start()
{
  m_impl->start();
}

void client::stop()
{
  m_impl->stop();
}

client::data client::get()
{
  return m_impl->get();
}

client::impl::impl(const rstream::nperf::client::config& config, const rstream::nperf::settings_client& settings, rstream::nperf::options options, unsigned int jobs)
    : m_options(options),
      m_thread_pool(jobs != 0 ? jobs : std::thread::hardware_concurrency()),
      m_impl(m_thread_pool.get_executor(), config, settings),
      m_state(state::null)
{
}

client::impl::~impl()
{
  m_thread_pool.stop();
}

void client::impl::start()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_state != state::null) {
    throw std::logic_error("client was previously started");
  }
  m_state                                     = state::running;
  m_executor_work_guard                       = std::make_shared<boost::asio::executor_work_guard<boost::asio::thread_pool::executor_type>>(m_thread_pool.get_executor());
  rstream::nperf::client::callbacks callbacks = {
      .m_on_metrics_cb = std::bind(&impl::on_metrics, shared_from_this(), std::placeholders::_1),
  };
  m_impl.async_run(m_options, callbacks, std::bind(&impl::on_complete, shared_from_this(), std::placeholders::_1));
}

void client::impl::stop()
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == state::null) {
      m_state = state::completed;
    }
    else if (m_state == state::running) {
      m_impl.cancel();
    }
  }
}

client::data client::impl::get()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_state == state::null) {
    throw std::logic_error("client not started");
  }
  else {
    while (m_queue.empty()) {
      if (m_state == state::completed) {
        return m_error_code;
      }
      m_cv.wait(lock);
    }
    auto metrics = m_queue.front();
    m_queue.pop_front();
    return metrics;
  }
}

void client::impl::on_metrics(const rstream::nperf::metrics& metrics)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_state != state::running) {
    return;
  }
  m_queue.push_back(metrics);
  m_cv.notify_one();
}

void client::impl::on_complete(const boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_state != state::running) {
    return;
  }
  m_error_code          = error_code;
  m_state               = state::completed;
  m_executor_work_guard = nullptr;
  m_cv.notify_one();
}

}  // namespace nperf
}  // namespace python
}  // namespace rstream
