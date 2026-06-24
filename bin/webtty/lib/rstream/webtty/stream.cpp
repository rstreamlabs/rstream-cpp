// See LICENSE file in the project root for license information.

#include "stream.hpp"

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
  if (m_console_handle) {
    error_code = std::error_code(ERROR_INVALID_HANDLE, std::system_category());
  }
  else {
    // 1) Create the pipe for HPCON's STDIN.
    //    HPCON will read from in_read (the read end).
    //    The parent will write to in_write (the write end).
    if (!::CreatePipe(&m_in_read, &m_in_write, nullptr, 0)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    if (error_code) {
      return;
    }
    // 2) Create the pipe for HPCON's STDOUT.
    //    HPCON will write to out_write (the write end).
    //    The parent will read from out_read (the read end).
    if (!::CreatePipe(&m_out_read, &m_out_write, nullptr, 0)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    if (error_code) {
      return;
    }
    // 3) Create the Pseudoconsole passing HPCON's side of the pipes:
    //    - HPCON input = read end of the input pipe  (m_in_read)
    //    - HPCON output = write end of the output pipe (m_out_write)
    COORD consoleSize = {80, 25};
    HRESULT hr        = ::CreatePseudoConsole(consoleSize, m_in_read, m_out_write, 0, &m_console_handle);
    if (FAILED(hr)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
    }
    if (error_code) {
      return;
    }
    if (m_in_read) {
      ::CloseHandle(m_in_read);
      m_in_read = nullptr;
    }
    if (m_out_write) {
      ::CloseHandle(m_out_write);
      m_out_write = nullptr;
    }
  }
}

void pty_windows::set_window_size(const terminal_size& terminal_size, std::error_code& error_code)
{
  if (!m_console_handle) {
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
  std::lock_guard<std::mutex> lock(m_mutex);
  {
    if (m_running == false || m_read_op != nullptr) {
      if (handler) {
        rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
      }
      return;
    }
    m_read_op            = std::make_shared<read_op>();
    m_read_op->m_buffer  = buffer;
    m_read_op->m_handler = std::move(handler);
  }
  m_cv_read_op.notify_one();
}

void pty_windows::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  {
    if (m_running == false || m_write_op != nullptr) {
      if (handler) {
        rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
      }
      return;
    }
    m_write_op            = std::make_shared<write_op>();
    m_write_op->m_buffer  = buffer;
    m_write_op->m_handler = std::move(handler);
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
  if (m_console_handle) {
    ::ClosePseudoConsole(m_console_handle);
    m_console_handle = nullptr;
  }
}

void pty_windows::close()
{
  stop();
}

void pty_windows::start()
{
  if (m_in_write == nullptr || m_out_read == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_running) {
    return;
  }
  m_running        = true;
  m_reading_thread = std::make_shared<std::thread>(std::bind(&pty_windows::reading_thread, this));
  m_writing_thread = std::make_shared<std::thread>(std::bind(&pty_windows::writing_thread, this));
}

void pty_windows::stop()
{
  if (m_console_handle) {
    ::ClosePseudoConsole(m_console_handle);
    m_console_handle = nullptr;
  }
  std::shared_ptr<std::thread> reading_thread = nullptr;
  std::shared_ptr<std::thread> writing_thread = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running        = false;
    reading_thread   = m_reading_thread;
    writing_thread   = m_writing_thread;
    m_reading_thread = nullptr;
    m_writing_thread = nullptr;
  }
  if (m_in_write) {
    ::CloseHandle(m_in_write);
  }
  if (m_out_read) {
    ::CloseHandle(m_out_read);
  }
  m_cv_read_op.notify_one();
  m_cv_write_op.notify_one();
  if (reading_thread) {
    reading_thread->join();
    reading_thread = nullptr;
  }
  if (writing_thread) {
    writing_thread->join();
    writing_thread = nullptr;
  }
  m_in_write = nullptr;
  m_out_read = nullptr;
  if (m_in_read) {
    ::CloseHandle(m_in_read);
    m_in_read = nullptr;
  }
  if (m_out_write) {
    ::CloseHandle(m_out_write);
    m_out_write = nullptr;
  }
}

void pty_windows::reading_thread()
{
  while (true) {
    std::shared_ptr<read_op> read_op = nullptr;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv_read_op.wait(lock, [this] { return !m_running || m_read_op != nullptr; });
      read_op   = std::move(m_read_op);
      m_read_op = nullptr;
      if (!m_running) {
        if (read_op && read_op->m_handler) {
          rstream::core::invoke_completion_handler(m_executor, std::move(read_op->m_handler), std::error_code(ERROR_OPERATION_ABORTED, std::system_category()), 0);
        }
        break;
      }
    }
    DWORD bytes_read = 0;
    std::error_code error_code;
    if (!::ReadFile(m_out_read, read_op->m_buffer.data(), static_cast<DWORD>(read_op->m_buffer.size()), &bytes_read, nullptr)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
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
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv_write_op.wait(lock, [this] { return !m_running || m_write_op != nullptr; });
      write_op   = std::move(m_write_op);
      m_write_op = nullptr;
      if (!m_running) {
        if (write_op && write_op->m_handler) {
          rstream::core::invoke_completion_handler(m_executor, std::move(write_op->m_handler), std::error_code(ERROR_OPERATION_ABORTED, std::system_category()), 0);
        }
        break;
      }
    }
    DWORD bytes_written = 0;
    std::error_code error_code;
    if (!::WriteFile(m_in_write, write_op->m_buffer.data(), static_cast<DWORD>(write_op->m_buffer.size()), &bytes_written, nullptr)) {
      error_code = std::error_code(::GetLastError(), std::system_category());
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
  // 1) Open the master side of the PTY.
  m_master_fd = posix_openpt(O_RDWR | O_NOCTTY);
  if (m_master_fd == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (error_code) {
    return;
  }
  // 2) Grant access to the slave.
  if (grantpt(m_master_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (error_code) {
    return;
  }
  // 3) Unlock the slave.
  if (unlockpt(m_master_fd) == -1) {
    error_code = std::error_code(errno, std::system_category());
  }
  if (error_code) {
    return;
  }
  // 4) Open the slave side of the PTY.
  char* slave_name = ptsname(m_master_fd);
  if (slave_name == nullptr || (m_slave_fd = open(slave_name, O_RDWR | O_NOCTTY)) == -1) {
    error_code = std::error_code(errno, std::system_category());
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
  }
}

#endif

}  // namespace stream

}  // namespace webtty
}  // namespace rstream
