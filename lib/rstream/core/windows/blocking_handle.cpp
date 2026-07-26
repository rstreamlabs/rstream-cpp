// See LICENSE file in the project root for license information.

#include "blocking_handle.hpp"

#ifdef _WIN32

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/error.hpp>

// clang-format off
// To be included after boost headers.
#include <windows.h>
// clang-format on

namespace rstream {
namespace core {
namespace windows {

namespace {

boost::system::error_code windows_error(DWORD error)
{
  if (error == ERROR_OPERATION_ABORTED) {
    return boost::asio::error::operation_aborted;
  }
  return {static_cast<int>(error), boost::system::system_category()};
}

boost::system::error_code operation_aborted_error()
{
  return boost::asio::error::operation_aborted;
}

}  // namespace

class blocking_handle::impl : public std::enable_shared_from_this<impl> {
 public:
  explicit impl(const executor_type& executor)
      : m_executor(executor)
  {
  }

  ~impl()
  {
    close();
  }

  void open(native_handle_type handle, access access, boost::system::error_code& error_code)
  {
    HANDLE duplicate = nullptr;
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
      error_code = boost::asio::error::bad_descriptor;
      return;
    }
    if (!::DuplicateHandle(::GetCurrentProcess(), handle, ::GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      error_code = windows_error(::GetLastError());
      return;
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_running || m_handle != nullptr) {
        ::CloseHandle(duplicate);
        error_code = boost::asio::error::already_open;
        return;
      }
      m_access  = access;
      m_handle  = duplicate;
      m_running = true;
      try {
        m_thread = std::thread([this] { run(); });
      }
      catch (...) {
        m_running = false;
        m_handle  = nullptr;
        ::CloseHandle(duplicate);
        throw;
      }
    }
  }

  bool is_open() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running && m_handle != nullptr;
  }

  void async_read_some(const boost::asio::mutable_buffer& buffer, completion_handler&& handler)
  {
    auto operation_allocator = boost::asio::get_associated_allocator(handler);
    submit(std::allocate_shared<operation>(operation_allocator, operation::type::read, buffer, std::move(handler)));
  }

  void async_write(const boost::asio::const_buffer& buffer, completion_handler&& handler)
  {
    auto operation_allocator = boost::asio::get_associated_allocator(handler);
    submit(std::allocate_shared<operation>(operation_allocator, operation::type::write, buffer, std::move(handler)));
  }

  void cancel()
  {
    close();
  }

  void close()
  {
    std::lock_guard<std::mutex> close_lock(m_close_mutex);
    HANDLE handle = nullptr;
    std::shared_ptr<operation> pending;
    bool cancel_active = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_running && m_handle == nullptr && !m_thread.joinable()) {
        return;
      }
      m_running     = false;
      handle        = m_handle;
      m_handle      = nullptr;
      pending       = std::move(m_pending);
      cancel_active = m_active != nullptr;
    }
    m_cv.notify_one();
    if (cancel_active && m_thread.joinable()) {
      ::CancelSynchronousIo(m_thread.native_handle());
    }
    if (handle != nullptr) {
      ::CloseHandle(handle);
    }
    complete(pending, operation_aborted_error(), 0);
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

 private:
  struct operation {
    enum class type {
      read,
      write
    };

    operation(type type, const boost::asio::mutable_buffer& buffer, completion_handler&& handler)
        : m_type(type),
          m_mutable_buffer(buffer),
          m_handler(std::move(handler))
    {
    }

    operation(type type, const boost::asio::const_buffer& buffer, completion_handler&& handler)
        : m_type(type),
          m_const_buffer(buffer),
          m_handler(std::move(handler))
    {
    }

    type m_type;
    boost::asio::mutable_buffer m_mutable_buffer;
    boost::asio::const_buffer m_const_buffer;
    completion_handler m_handler;
  };

  void submit(const std::shared_ptr<operation>& op)
  {
    boost::system::error_code error_code;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      const auto expected = op->m_type == operation::type::read ? access::read : access::write;
      if (!m_running || m_handle == nullptr) {
        error_code = boost::asio::error::bad_descriptor;
      }
      else if (m_access != expected) {
        error_code = boost::asio::error::operation_not_supported;
      }
      else if (m_pending != nullptr || m_active != nullptr) {
        error_code = boost::asio::error::already_started;
      }
      else {
        auto cancellation_slot = boost::asio::get_associated_cancellation_slot(op->m_handler);
        if (cancellation_slot.is_connected()) {
          std::weak_ptr<impl> weak_self    = shared_from_this();
          std::weak_ptr<operation> weak_op = op;
          cancellation_slot.assign([weak_self, weak_op](boost::asio::cancellation_type type) {
            if (type == boost::asio::cancellation_type::none) {
              return;
            }
            auto self      = weak_self.lock();
            auto locked_op = weak_op.lock();
            if (self != nullptr && locked_op != nullptr) {
              self->cancel(locked_op);
            }
          });
        }
        m_pending = op;
      }
    }
    if (error_code) {
      complete(op, error_code, 0);
    }
    else {
      m_cv.notify_one();
    }
  }

  void cancel(const std::shared_ptr<operation>& op)
  {
    bool cancel = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      cancel = m_pending == op || m_active == op;
    }
    if (cancel) {
      close();
    }
  }

  void run()
  {
    while (true) {
      std::shared_ptr<operation> op;
      HANDLE handle = nullptr;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_running || m_pending != nullptr; });
        if (!m_running) {
          break;
        }
        m_active = std::move(m_pending);
        op       = m_active;
        handle   = m_handle;
      }
      boost::system::error_code error_code;
      std::size_t transferred = 0;
      if (op->m_type == operation::type::read) {
        DWORD read      = 0;
        const auto size = static_cast<DWORD>(std::min<std::size_t>(op->m_mutable_buffer.size(), std::numeric_limits<DWORD>::max()));
        if (!::ReadFile(handle, op->m_mutable_buffer.data(), size, &read, nullptr)) {
          error_code = windows_error(::GetLastError());
        }
        else if (read == 0) {
          error_code = boost::asio::error::eof;
        }
        else {
          transferred = read;
        }
      }
      else {
        auto data      = static_cast<const unsigned char*>(op->m_const_buffer.data());
        auto remaining = op->m_const_buffer.size();
        while (remaining != 0 && !error_code) {
          DWORD written   = 0;
          const auto size = static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max()));
          if (!::WriteFile(handle, data + transferred, size, &written, nullptr)) {
            error_code = windows_error(::GetLastError());
          }
          else if (written == 0) {
            error_code = boost::asio::error::broken_pipe;
          }
          else {
            transferred += written;
            remaining -= written;
          }
        }
      }
      {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active.reset();
        if (!m_running) {
          error_code  = operation_aborted_error();
          transferred = 0;
        }
      }
      complete(op, error_code, transferred);
    }
  }

  void complete(const std::shared_ptr<operation>& op, const boost::system::error_code& error_code, std::size_t transferred)
  {
    if (op == nullptr || !op->m_handler) {
      return;
    }
    rstream::core::invoke_completion_handler(m_executor, std::move(op->m_handler), error_code, transferred);
  }

  executor_type m_executor;
  std::mutex m_close_mutex;
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::thread m_thread;
  HANDLE m_handle = nullptr;
  access m_access = access::read;
  bool m_running  = false;
  std::shared_ptr<operation> m_pending;
  std::shared_ptr<operation> m_active;
};

blocking_handle::blocking_handle(const executor_type& executor)
    : m_impl(std::make_shared<impl>(executor))
{
}

blocking_handle::~blocking_handle()
{
  m_impl->close();
}

void blocking_handle::open(native_handle_type handle, access access, boost::system::error_code& error_code)
{
  m_impl->open(handle, access, error_code);
}

bool blocking_handle::is_open() const
{
  return m_impl->is_open();
}

void blocking_handle::async_read_some(const boost::asio::mutable_buffer& buffer, completion_handler&& handler)
{
  m_impl->async_read_some(buffer, std::move(handler));
}

void blocking_handle::async_write(const boost::asio::const_buffer& buffer, completion_handler&& handler)
{
  m_impl->async_write(buffer, std::move(handler));
}

void blocking_handle::cancel()
{
  m_impl->cancel();
}

void blocking_handle::close()
{
  m_impl->close();
}

}  // namespace windows
}  // namespace core
}  // namespace rstream

#endif
