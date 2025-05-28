// See LICENSE file in the project root for license information.

#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>

class task : public std::enable_shared_from_this<task> {
 public:
  task(const boost::asio::any_io_executor& executor)
      : m_strand(executor)
  {
  }

  virtual ~task() = default;

  void async_run()
  {
    boost::asio::dispatch(m_strand, std::bind_front(&task::async_run_internal, shared_from_this()));
  }

 private:
  void async_run_internal()
  {
    std::size_t timer_count = 100;
    m_timers.reserve(timer_count);
    using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code& error_code)>;
    auto run                           = [this](async_run_completion_handler&& handler) {
      auto& timer = m_timers.emplace_back(std::make_shared<boost::asio::deadline_timer>(m_strand.get_inner_executor()));
      timer->expires_from_now(boost::posix_time::milliseconds(10));
      timer->async_wait(std::move(handler));
    };
    for (std::size_t i = 0; i < timer_count; ++i) {
      auto completion_handler = std::bind(&task::on_timer, shared_from_this(), std::placeholders::_1);
      run(boost::asio::bind_executor(m_strand, completion_handler));
    }
  }

  void on_timer(const boost::system::error_code& error_code)
  {
    (void)error_code;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto id = std::this_thread::get_id();
      assert(m_threads_id.empty());
      m_threads_id.insert(id);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto id = std::this_thread::get_id();
      m_threads_id.erase(id);
    }
  }

  boost::asio::strand<boost::asio::any_io_executor> m_strand;

  std::vector<std::shared_ptr<boost::asio::deadline_timer>> m_timers;

  std::mutex m_mutex;

  std::set<std::thread::id> m_threads_id;
};

int run()
{
  auto jobs = std::thread::hardware_concurrency();
  boost::asio::io_context io_context(jobs);
  auto ptr = std::make_shared<task>(io_context.get_executor());
  ptr->async_run();
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  return 0;
}

int main()
{
  std::exception_ptr error;
  try {
    return run();
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::cerr << "a fatal error occurred: " << rstream::core::throwable::message(error) << std::endl;
  }
  return error ? -1 : 0;
}
