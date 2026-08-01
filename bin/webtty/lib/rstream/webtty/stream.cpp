// See LICENSE file in the project root for license information.

#include "stream.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>

#include <rstream/config.hpp>

#include "error.hpp"
#include "terminal.hpp"

namespace rstream {
namespace webtty {

namespace stream {

base::base(enum backend backend)
    : m_backend(backend)
{
}

enum backend base::backend() const { return m_backend; }

ptr make_stream(const executor_type& executor, backend backend)
{
  if (backend == backend::pipe) {
    return std::make_shared<pipe>(executor);
  }
  else {
#ifdef _WIN32
    return std::make_shared<pty_windows>(executor);
#else
    return std::make_shared<pty_posix>(executor);
#endif
  }
}

pipe::pipe(const executor_type& executor)
    : base(backend::pipe),
      m_std_in(executor.context()),
      m_std_out(executor.context()),
      m_std_err(executor.context())
{
}

pipe::~pipe()
{
  close();
}

void pipe::async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler)
{
  stream(type).async_read_some(buffer, std::move(handler));
}

void pipe::async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler)
{
  boost::asio::async_write(stream(type), buffer, std::move(handler));
}

void pipe::close()
{
  close(type::std_in);
  close(type::std_out);
  close(type::std_err);
}

void pipe::close(type type)
{
  boost::system::error_code tmp;
  stream(type).close(tmp);
}

pipe::stream_type& pipe::stream(type type)
{
  if (type == type::std_in) {
    return m_std_in;
  }
  else if (type == type::std_out) {
    return m_std_out;
  }
  else {
    return m_std_err;
  }
}

#ifdef _WIN32

namespace {

void close_handle(HANDLE& handle)
{
  if (handle != nullptr) {
    ::CloseHandle(handle);
    handle = nullptr;
  }
}

void cancel_thread_io(const std::shared_ptr<std::thread>& thread)
{
  if (thread != nullptr && thread->joinable()) {
    if (!::CancelSynchronousIo(thread->native_handle()) && ::GetLastError() != ERROR_NOT_FOUND) {
      // Closing the associated pipe below remains the final cancellation path.
    }
  }
}

std::error_code operation_aborted_error()
{
  return std::error_code(ERROR_OPERATION_ABORTED, std::system_category());
}

}  // namespace

pty_windows::pty_windows(const executor_type& executor)
    : base(backend::tty),
      m_executor(executor)
{
}

pty_windows::~pty_windows()
{
  close();
}

void pty_windows::allocate(std::error_code& error_code)
{
  HANDLE in_read   = nullptr;
  HANDLE in_write  = nullptr;
  HANDLE out_read  = nullptr;
  HANDLE out_write = nullptr;
  HPCON console    = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_console_handle != nullptr || m_in_write != nullptr || m_out_read != nullptr || m_running) {
      error_code = error::code::invalid_state;
      return;
    }
  }
  if (!::CreatePipe(&in_read, &in_write, nullptr, 0)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
  if (!error_code && !::CreatePipe(&out_read, &out_write, nullptr, 0)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
  if (!error_code) {
    COORD console_size = {80, 25};
    auto result        = ::CreatePseudoConsole(console_size, in_read, out_write, 0, &console);
    if (FAILED(result)) {
      error_code = std::error_code(static_cast<int>(result), std::system_category());
    }
  }
  if (!error_code) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_console_handle != nullptr || m_in_write != nullptr || m_out_read != nullptr || m_running) {
      error_code = error::code::invalid_state;
    }
    else {
      m_console_handle = console;
      m_in_write       = in_write;
      m_out_read       = out_read;
      console          = nullptr;
      in_write         = nullptr;
      out_read         = nullptr;
    }
  }
  if (console != nullptr) {
    ::ClosePseudoConsole(console);
  }
  close_handle(in_read);
  close_handle(in_write);
  close_handle(out_read);
  close_handle(out_write);
}

void pty_windows::set_window_size(const terminal_size& terminal_size, std::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_console_handle == nullptr) {
    error_code = std::error_code(ERROR_INVALID_HANDLE, std::system_category());
  }
  else {
    COORD size;
    size.X     = static_cast<SHORT>(terminal_size.m_col);
    size.Y     = static_cast<SHORT>(terminal_size.m_row);
    HRESULT hr = ::ResizePseudoConsole(m_console_handle, size);
    if (FAILED(hr)) {
      error_code = std::error_code(hr, std::system_category());
    }
  }
}

void pty_windows::async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler)
{
  (void)type;
  async_read_some_completion_handler rejected_handler;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || m_read_op != nullptr || m_read_active) {
      rejected_handler = std::move(handler);
    }
    else {
      m_read_op            = std::make_shared<read_op>();
      m_read_op->m_buffer  = buffer;
      m_read_op->m_handler = std::move(handler);
    }
  }
  if (rejected_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(rejected_handler), error::code::invalid_state, 0);
    return;
  }
  m_cv_read_op.notify_one();
}

void pty_windows::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  async_write_some_completion_handler rejected_handler;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || m_write_op != nullptr || m_write_active) {
      rejected_handler = std::move(handler);
    }
    else {
      m_write_op            = std::make_shared<write_op>();
      m_write_op->m_buffer  = buffer;
      m_write_op->m_handler = std::move(handler);
    }
  }
  if (rejected_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(rejected_handler), error::code::invalid_state, 0);
    return;
  }
  m_cv_write_op.notify_one();
}

void pty_windows::async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler)
{
  (void)type;
  boost::asio::async_write(*this, buffer, std::move(handler));
}

void pty_windows::cancel()
{
  HPCON console = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    console          = m_console_handle;
    m_console_handle = nullptr;
  }
  if (console != nullptr) {
    ::ClosePseudoConsole(console);
  }
}

void pty_windows::close()
{
  stop();
}

void pty_windows::start()
{
  std::shared_ptr<std::thread> reading_thread;
  std::shared_ptr<std::thread> writing_thread;
  std::exception_ptr exception;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_in_write == nullptr || m_out_read == nullptr || m_running) {
      return;
    }
    m_running = true;
    try {
      reading_thread = std::make_shared<std::thread>([this] { this->reading_thread(); });
      writing_thread = std::make_shared<std::thread>([this] { this->writing_thread(); });
    }
    catch (...) {
      m_running = false;
      exception = std::current_exception();
    }
    if (!exception) {
      m_reading_thread = std::move(reading_thread);
      m_writing_thread = std::move(writing_thread);
    }
  }
  if (exception) {
    m_cv_read_op.notify_all();
    m_cv_write_op.notify_all();
    if (reading_thread != nullptr && reading_thread->joinable()) {
      reading_thread->join();
    }
    if (writing_thread != nullptr && writing_thread->joinable()) {
      writing_thread->join();
    }
    std::rethrow_exception(exception);
  }
}

void pty_windows::stop()
{
  HPCON console   = nullptr;
  HANDLE in_write = nullptr;
  HANDLE out_read = nullptr;
  std::shared_ptr<std::thread> reading_thread;
  std::shared_ptr<std::thread> writing_thread;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running        = false;
    console          = m_console_handle;
    in_write         = m_in_write;
    out_read         = m_out_read;
    reading_thread   = std::move(m_reading_thread);
    writing_thread   = std::move(m_writing_thread);
    m_console_handle = nullptr;
    m_in_write       = nullptr;
    m_out_read       = nullptr;
  }
  m_cv_read_op.notify_one();
  m_cv_write_op.notify_one();
  cancel_thread_io(reading_thread);
  cancel_thread_io(writing_thread);
  close_handle(in_write);
  close_handle(out_read);
  if (console != nullptr) {
    ::ClosePseudoConsole(console);
  }
  if (reading_thread != nullptr && reading_thread->joinable()) {
    reading_thread->join();
  }
  if (writing_thread != nullptr && writing_thread->joinable()) {
    writing_thread->join();
  }
}

void pty_windows::reading_thread()
{
  while (true) {
    std::shared_ptr<read_op> read_op = nullptr;
    HANDLE out_read                  = nullptr;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv_read_op.wait(lock, [this] { return !m_running || m_read_op != nullptr; });
      read_op   = std::move(m_read_op);
      m_read_op = nullptr;
      if (!m_running) {
        if (read_op && read_op->m_handler) {
          rstream::core::invoke_completion_handler(m_executor, std::move(read_op->m_handler), operation_aborted_error(), 0);
        }
        break;
      }
      m_read_active = true;
      out_read      = m_out_read;
    }
    DWORD bytes_read = 0;
    std::error_code error_code;
    auto size = static_cast<DWORD>(std::min<std::size_t>(read_op->m_buffer.size(), std::numeric_limits<DWORD>::max()));
    if (!::ReadFile(out_read, read_op->m_buffer.data(), size, &bytes_read, nullptr)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_read_active = false;
      if (!m_running) {
        error_code = operation_aborted_error();
      }
    }
    if (read_op->m_handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(read_op->m_handler), error_code, error_code ? 0 : bytes_read);
    }
  }
}

void pty_windows::writing_thread()
{
  while (true) {
    std::shared_ptr<write_op> write_op = nullptr;
    HANDLE in_write                    = nullptr;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv_write_op.wait(lock, [this] { return !m_running || m_write_op != nullptr; });
      write_op   = std::move(m_write_op);
      m_write_op = nullptr;
      if (!m_running) {
        if (write_op && write_op->m_handler) {
          rstream::core::invoke_completion_handler(m_executor, std::move(write_op->m_handler), operation_aborted_error(), 0);
        }
        break;
      }
      m_write_active = true;
      in_write       = m_in_write;
    }
    DWORD bytes_written = 0;
    std::error_code error_code;
    auto size = static_cast<DWORD>(std::min<std::size_t>(write_op->m_buffer.size(), std::numeric_limits<DWORD>::max()));
    if (!::WriteFile(in_write, write_op->m_buffer.data(), size, &bytes_written, nullptr)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_write_active = false;
      if (!m_running) {
        error_code = operation_aborted_error();
      }
    }
    if (write_op->m_handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(write_op->m_handler), error_code, error_code ? 0 : bytes_written);
    }
  }
}

#else

pty_posix::pty_posix(const executor_type& executor)
    : base(backend::tty),
      m_std_in_out(executor)
{
}

pty_posix::~pty_posix()
{
  close();
}

void pty_posix::allocate(std::error_code& error_code)
{
  if (m_master_fd != -1 || m_slave_fd != -1 || m_std_in_out.is_open()) {
    error_code = error::code::invalid_state;
    return;
  }
  int master_fd = -1;
  int slave_fd  = -1;
  int flags     = O_RDWR | O_NOCTTY;
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  master_fd = posix_openpt(flags);
  if (master_fd == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (!error_code && grantpt(master_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (!error_code && unlockpt(master_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  std::array<char, 1024> slave_name{};
  if (!error_code) {
    auto result = ptsname_r(master_fd, slave_name.data(), slave_name.size());
    if (result != 0) {
      error_code = std::error_code(result, std::generic_category());
    }
  }
  if (!error_code) {
    slave_fd = open(slave_name.data(), flags);
    if (slave_fd == -1) {
      error_code = std::error_code(errno, std::system_category());
    }
  }
  if (error_code) {
    if (master_fd != -1) {
      ::close(master_fd);
    }
    if (slave_fd != -1) {
      ::close(slave_fd);
    }
  }
  else {
    m_master_fd = master_fd;
    m_slave_fd  = slave_fd;
  }
}

void pty_posix::set_window_size(const terminal_size& terminal_size, std::error_code& error_code)
{
  try {
    terminal(m_master_fd).resize(terminal_size);
  }
  catch (const std::system_error& system_error) {
    error_code = system_error.code();
  }
}

void pty_posix::async_read_some(const boost::asio::mutable_buffer& buffer, type type, async_read_some_completion_handler&& handler)
{
  (void)type;
  m_std_in_out.async_read_some(buffer, std::move(handler));
}

void pty_posix::async_write(const boost::asio::const_buffer& buffer, type type, async_write_completion_handler&& handler)
{
  (void)type;
  boost::asio::async_write(m_std_in_out, buffer, std::move(handler));
}

void pty_posix::close()
{
  {
    boost::system::error_code tmp;
    m_std_in_out.close(tmp);
  }
  if (m_master_fd != -1) {
    ::close(m_master_fd);
    m_master_fd = -1;
  }
  if (m_slave_fd != -1) {
    ::close(m_slave_fd);
    m_slave_fd = -1;
  }
}

void pty_posix::on_exec_setup(std::error_code& error_code)
{
  if (::close(m_master_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  else {
    m_master_fd = -1;
  }
  if (error_code) {
    return;
  }
  if (setsid() == -1 || ioctl(m_slave_fd, TIOCSCTTY, NULL) == -1
      || dup2(m_slave_fd, STDOUT_FILENO) == -1 || dup2(m_slave_fd, STDERR_FILENO) == -1
      || dup2(m_slave_fd, STDIN_FILENO) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (error_code) {
    return;
  }
  if (::close(m_slave_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  else {
    m_slave_fd = -1;
  }
}

void pty_posix::on_success(std::error_code& error_code)
{
  if (m_slave_fd != -1) {
    if (::close(m_slave_fd) == -1) {
      error_code = std::error_code(errno, std::system_category());
    }
    else {
      m_slave_fd = -1;
    }
  }
  if (error_code) {
    return;
  }
  {
    boost::system::error_code tmp;
    m_std_in_out.assign(m_master_fd, tmp);
    error_code = tmp;
    if (!error_code) {
      m_master_fd = -1;
    }
  }
}

#endif

}  // namespace stream

}  // namespace webtty
}  // namespace rstream
