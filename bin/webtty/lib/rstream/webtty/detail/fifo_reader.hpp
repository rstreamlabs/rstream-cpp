// See LICENSE file in the project root for license information.

#pragma once

#ifdef __APPLE__

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <rstream/core/completion_handler.hpp>

namespace rstream {
namespace webtty {
namespace detail {

// Darwin FIFO readiness is not reliably reported through kqueue. Only stdin
// FIFOs use this single-request select worker; network IO keeps its reactor.
// poll() also misses EOF for an empty Darwin FIFO, so it is not equivalent here.
// Calls are serialized by the client strand. The borrowed descriptor must be
// nonblocking and remain open until close() has returned.
class fifo_reader {
 public:
  using executor_type      = boost::asio::any_io_executor;
  using completion_handler = core::completion_handler<void(const boost::system::error_code&, std::size_t)>;

  fifo_reader(const executor_type& executor, int descriptor)
      : m_executor(executor),
        m_descriptor(descriptor),
        m_wakeup_read(executor),
        m_wakeup_write(executor)
  {
    if (descriptor < 0 || descriptor >= FD_SETSIZE) {
      throw boost::system::system_error(boost::asio::error::fd_set_failure);
    }
    int wakeup[2];
    if (::pipe(wakeup) != 0) {
      throw boost::system::system_error(system_error());
    }
    // Owning descriptors close both ends if subsequent initialization fails.
    boost::system::error_code error;
    m_wakeup_read.assign(wakeup[0], error);
    if (error) {
      ::close(wakeup[0]);
      ::close(wakeup[1]);
      throw boost::system::system_error(error);
    }
    m_wakeup_write.assign(wakeup[1], error);
    if (error) {
      ::close(wakeup[1]);
      throw boost::system::system_error(error);
    }
    for (int fd : wakeup) {
      if (fd >= FD_SETSIZE) {
        throw boost::system::system_error(boost::asio::error::fd_set_failure);
      }
      if (::fcntl(fd, F_SETFD, FD_CLOEXEC) == -1 || ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        throw boost::system::system_error(system_error());
      }
    }
    m_thread = std::thread([this] { run(); });
  }

  ~fifo_reader()
  {
    close();
  }

  fifo_reader(const fifo_reader&)            = delete;
  fifo_reader& operator=(const fifo_reader&) = delete;

  void async_read_some(const boost::asio::mutable_buffer& buffer, completion_handler&& handler)
  {
    boost::system::error_code error;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_stopped) {
        error = boost::asio::error::operation_aborted;
      }
      else if (m_pending || m_active) {
        error = boost::asio::error::already_started;
      }
      else {
        m_pending = std::make_unique<operation>(m_executor, buffer, std::move(handler));
      }
    }
    if (error) {
      core::invoke_completion_handler(m_executor, std::move(handler), error, std::size_t(0));
    }
    else {
      m_ready.notify_one();
    }
  }

  void close()
  {
    if (!m_thread.joinable()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stopped = true;
    }
    m_ready.notify_one();
    const char wakeup = 0;
    while (::write(m_wakeup_write.native_handle(), &wakeup, 1) == -1 && errno == EINTR) {
    }
    m_thread.join();
  }

 private:
  struct operation {
    operation(const executor_type& executor, const boost::asio::mutable_buffer& buffer, completion_handler&& handler)
        : m_buffer(buffer),
          m_work(boost::asio::make_work_guard(boost::asio::get_associated_executor(handler, executor))),
          m_handler(std::move(handler))
    {
    }

    boost::asio::mutable_buffer m_buffer;
    boost::asio::executor_work_guard<boost::asio::any_completion_executor> m_work;
    completion_handler m_handler;
  };

  static boost::system::error_code system_error()
  {
    return {errno, boost::system::system_category()};
  }

  boost::system::error_code read(const boost::asio::mutable_buffer& buffer, std::size_t& size)
  {
    if (buffer.size() == 0) {
      return {};
    }
    const auto wakeup = m_wakeup_read.native_handle();
    const auto count  = std::max(m_descriptor, wakeup) + 1;
    for (;;) {
      const auto transferred = ::read(m_descriptor, buffer.data(), buffer.size());
      if (transferred > 0) {
        size = static_cast<std::size_t>(transferred);
        return {};
      }
      if (transferred == 0) {
        return boost::asio::error::eof;
      }
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        return system_error();
      }
      fd_set descriptors;
      FD_ZERO(&descriptors);
      FD_SET(m_descriptor, &descriptors);
      FD_SET(wakeup, &descriptors);
      if (::select(count, &descriptors, nullptr, nullptr, nullptr) == -1) {
        if (errno == EINTR) {
          continue;
        }
        return system_error();
      }
      if (FD_ISSET(wakeup, &descriptors)) {
        return boost::asio::error::operation_aborted;
      }
    }
  }

  void run()
  {
    for (;;) {
      std::unique_ptr<operation> op;
      bool stopped;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_ready.wait(lock, [this] { return m_stopped || m_pending; });
        stopped  = m_stopped;
        op       = std::move(m_pending);
        m_active = op != nullptr;
      }
      if (!op) {
        return;
      }
      std::size_t size                = 0;
      boost::system::error_code error = stopped ? boost::asio::error::operation_aborted : read(op->m_buffer, size);
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active = false;
        if (m_stopped) {
          error = boost::asio::error::operation_aborted;
          size  = 0;
        }
      }
      core::invoke_completion_handler(m_executor, std::move(op->m_handler), error, size);
    }
  }

  executor_type m_executor;
  int m_descriptor;
  boost::asio::posix::stream_descriptor m_wakeup_read;
  boost::asio::posix::stream_descriptor m_wakeup_write;
  std::mutex m_mutex;
  std::condition_variable m_ready;
  std::thread m_thread;
  std::unique_ptr<operation> m_pending;
  bool m_active  = false;
  bool m_stopped = false;
};

}  // namespace detail
}  // namespace webtty
}  // namespace rstream

#endif
