// See LICENSE file in the project root for license information.

#pragma once

#define BOOST_PROCESS_VERSION 1

#include <functional>
#include <memory>

#ifdef _WIN32
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include <boost/asio/write.hpp>
#ifndef _WIN32
#include <boost/asio/posix/stream_descriptor.hpp>
#endif
#if __has_include(<boost/process/v1/async_pipe.hpp>)
#include <boost/process/v1/async_pipe.hpp>
#else
#include <boost/process/async_pipe.hpp>
#endif

#if __has_include(<boost/process/v1/extend.hpp>)
#include <boost/process/v1/extend.hpp>
#else
#include <boost/process/extend.hpp>
#endif

#include <rstream/core/completion_handler.hpp>

#include "error.hpp"
#include "webtty.hpp"

// clang-format off
// To be included after boost headers
#if _WIN32
#include <wincon.h>
#include <windows.h>
#endif
// clang-format on

namespace rstream {
namespace webtty {

namespace stream {

enum class type { std_out,
                  std_err,
                  std_in };

enum class backend { pipe,
                     tty };

class base {
 public:
  using executor_type = rstream::webtty::executor_type;
  virtual ~base()     = default;

  enum backend backend() const;

  using async_read_some_completion_handler = rstream::core::completion_handler<void(const std::error_code&, std::size_t)>;

  virtual void async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler) = 0;

  using async_write_completion_handler = rstream::core::completion_handler<void(const std::error_code&, std::size_t)>;

  virtual void async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler) = 0;

  virtual void close() = 0;

 protected:
  base(enum backend backend);

 private:
  enum backend m_backend;
};

using ptr = std::shared_ptr<base>;

ptr make_stream(const executor_type& executor, backend backend);

class pty {
 public:
  virtual void allocate(std::error_code& error_code) = 0;

  virtual void set_window_size(const terminal_size& terminal_size, std::error_code& error_code) = 0;
};

class pipe : public base {
 public:
  using stream_type = boost::process::async_pipe;

  pipe(const executor_type& executor);

  virtual ~pipe();

  stream_type& stream(type type);

  void async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler) override;

  void async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler) override;

  void close() override;

  void close(type type);

 private:
  stream_type m_std_in;

  stream_type m_std_out;

  stream_type m_std_err;
};

#ifdef _WIN32

class pty_windows : public pty, public base {
 public:
  pty_windows(const executor_type& executor);

  virtual ~pty_windows();

  void allocate(std::error_code& error_code) override;

  void set_window_size(const terminal_size& terminal_size, std::error_code& error_code) override;

  void async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler) override;

  using async_write_some_completion_handler = async_write_completion_handler;

  void async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler) override;

  void cancel();

  void close() override;

  void start();

  void stop();

  template <typename Char, typename Sequence>
  void on_setup(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code);

  template <typename Char, typename Sequence>
  void on_error(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code);

  template <typename Char, typename Sequence>
  void on_success(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code);

 private:
  void reading_thread();

  void writing_thread();

  struct read_op {
    boost::asio::mutable_buffer m_buffer;
    async_read_some_completion_handler m_handler;
  };

  struct write_op {
    boost::asio::const_buffer m_buffer;
    async_write_completion_handler m_handler;
  };

  executor_type m_executor;

  HANDLE m_in_read = nullptr;  // HPCON's STDIN

  HANDLE m_out_write = nullptr;  // HPCON's STDOUT

  HANDLE m_in_write = nullptr;  // parent writes → HPCON

  HANDLE m_out_read = nullptr;  // parent reads ← HPCON

  HPCON m_console_handle = nullptr;

  bool m_running = false;

  std::mutex m_mutex;

  std::shared_ptr<std::thread> m_reading_thread;

  std::shared_ptr<read_op> m_read_op;

  std::condition_variable m_cv_read_op;

  std::shared_ptr<std::thread> m_writing_thread;

  std::shared_ptr<write_op> m_write_op;

  std::condition_variable m_cv_write_op;
};

#else

class pty_posix : public pty, public base {
 public:
  using stream_type = boost::asio::posix::stream_descriptor;

  pty_posix(const executor_type& executor);

  virtual ~pty_posix();

  void allocate(std::error_code& error_code) override;

  void set_window_size(const terminal_size& terminal_size, std::error_code& error_code) override;

  void async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler) override;

  void async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler) override;

  void close() override;

  void on_exec_setup(std::error_code& error_code);

  void on_success(std::error_code& error_code);

 private:
  stream_type m_std_in_out;

  int m_master_fd = -1;

  int m_slave_fd = -1;
};

#endif

#ifdef _WIN32

template <typename Char, typename Sequence>
void pty_windows::on_setup(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_console_handle != nullptr);
#endif
  executor.set_startup_info_ex();
  auto& si = executor.startup_info_ex;
#ifdef DEBUG_BUILD
  assert(m_console_handle != nullptr);
  assert(si.lpAttributeList == nullptr);
  assert(si.StartupInfo.cb == sizeof(::boost::winapi::STARTUPINFOEXA_));
  assert(executor.creation_flags & ::boost::winapi::EXTENDED_STARTUPINFO_PRESENT_);
#endif
  if (m_console_handle == nullptr || si.lpAttributeList != nullptr) {
    error_code = error::code::server_error;
  }
  else {
    SIZE_T attrListSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    auto attrlist = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::HeapAlloc(::GetProcessHeap(), 0, attrListSize));
    if (attrlist == nullptr) {
      error_code = std::error_code(E_OUTOFMEMORY, std::system_category());
    }
    if (error_code) {
      return;
    }
    if (!::InitializeProcThreadAttributeList(attrlist, 1, 0, &attrListSize)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    if (error_code) {
      ::HeapFree(::GetProcessHeap(), 0, attrlist);
    }
    else {
      if (!::UpdateProcThreadAttribute(attrlist, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_console_handle, sizeof(HPCON), nullptr, nullptr)) {
        error_code = std::error_code(::GetLastError(), std::system_category());
      }
      if (error_code) {
        ::DeleteProcThreadAttributeList(attrlist);
        ::HeapFree(::GetProcessHeap(), 0, attrlist);
      }
      else {
        executor.inherit_handles  = false;
        si.lpAttributeList        = attrlist;
        si.StartupInfo.hStdError  = nullptr;
        si.StartupInfo.hStdInput  = nullptr;
        si.StartupInfo.hStdOutput = nullptr;
      }
    }
  }
}

template <typename Char, typename Sequence>
void pty_windows::on_error(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code)
{
  auto& si = executor.startup_info_ex;
#ifdef DEBUG_BUILD
  assert(si.lpAttributeList != nullptr);
#endif
  if (si.lpAttributeList != nullptr) {
    ::DeleteProcThreadAttributeList(si.lpAttributeList);
    ::HeapFree(::GetProcessHeap(), 0, si.lpAttributeList);
    si.lpAttributeList = nullptr;
  }
}

template <typename Char, typename Sequence>
void pty_windows::on_success(boost::process::extend::windows_executor<Char, Sequence>& executor, std::error_code& error_code)
{
  {
    auto& si = executor.startup_info_ex;
#ifdef DEBUG_BUILD
    assert(si.lpAttributeList != nullptr);
#endif
    if (si.lpAttributeList != nullptr) {
      ::DeleteProcThreadAttributeList(si.lpAttributeList);
      ::HeapFree(::GetProcessHeap(), 0, si.lpAttributeList);
      si.lpAttributeList = nullptr;
    }
  }
  start();
}

#endif

}  // namespace stream

}  // namespace webtty
}  // namespace rstream
