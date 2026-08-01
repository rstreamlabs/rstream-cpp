// See LICENSE file in the project root for license information.

#ifdef _WIN32

#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

#include <rstream/core/windows/blocking_handle.hpp>

// clang-format off
// To be included after boost headers.
#include <windows.h>
// clang-format on

class handle_guard {
 public:
  explicit handle_guard(HANDLE handle = nullptr)
      : m_handle(handle)
  {
  }

  ~handle_guard()
  {
    reset();
  }

  HANDLE get() const
  {
    return m_handle;
  }

  HANDLE release()
  {
    auto handle = m_handle;
    m_handle    = nullptr;
    return handle;
  }

  void reset(HANDLE handle = nullptr)
  {
    if (m_handle != nullptr) {
      ::CloseHandle(m_handle);
    }
    m_handle = handle;
  }

 private:
  HANDLE m_handle;
};

static void check_read_from_non_overlapped_pipe()
{
  HANDLE read_handle  = nullptr;
  HANDLE write_handle = nullptr;
  assert(::CreatePipe(&read_handle, &write_handle, nullptr, 0));
  handle_guard read(read_handle);
  handle_guard write(write_handle);

  boost::asio::io_context io_context;
  rstream::core::windows::blocking_handle stream(io_context.get_executor());
  boost::system::error_code error_code = boost::asio::error::operation_aborted;
  stream.open(read.get(), rstream::core::windows::blocking_handle::access::read, error_code);
  assert(!error_code);

  std::array<char, 16> buffer{};
  std::size_t transferred = 0;
  stream.async_read_some(boost::asio::buffer(buffer), [&](const boost::system::error_code& error, std::size_t size) {
    error_code  = error;
    transferred = size;
  });
  std::promise<void> started;
  auto started_future = started.get_future();
  boost::asio::post(io_context, [&] { started.set_value(); });
  std::thread io_thread([&] { io_context.run(); });
  started_future.wait();

  const std::string expected = "stdin-data";
  DWORD written              = 0;
  assert(::WriteFile(write.get(), expected.data(), static_cast<DWORD>(expected.size()), &written, nullptr));
  assert(written == expected.size());
  io_thread.join();
  assert(!error_code);
  assert(transferred == expected.size());
  assert(std::string(buffer.data(), transferred) == expected);
}

static void check_write_to_non_overlapped_pipe()
{
  HANDLE read_handle  = nullptr;
  HANDLE write_handle = nullptr;
  assert(::CreatePipe(&read_handle, &write_handle, nullptr, 0));
  handle_guard read(read_handle);
  handle_guard write(write_handle);

  boost::asio::io_context io_context;
  rstream::core::windows::blocking_handle stream(io_context.get_executor());
  boost::system::error_code error_code = boost::asio::error::operation_aborted;
  stream.open(write.get(), rstream::core::windows::blocking_handle::access::write, error_code);
  assert(!error_code);

  const std::string expected = "stdout-data";
  std::size_t transferred    = 0;
  stream.async_write(boost::asio::buffer(expected), [&](const boost::system::error_code& error, std::size_t size) {
    error_code  = error;
    transferred = size;
  });
  io_context.run();
  assert(!error_code);
  assert(transferred == expected.size());

  std::array<char, 16> buffer{};
  DWORD read_size = 0;
  assert(::ReadFile(read.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read_size, nullptr));
  assert(std::string(buffer.data(), read_size) == expected);
}

static void check_cancellation_completes_once()
{
  HANDLE read_handle  = nullptr;
  HANDLE write_handle = nullptr;
  assert(::CreatePipe(&read_handle, &write_handle, nullptr, 0));
  handle_guard read(read_handle);
  handle_guard write(write_handle);

  boost::asio::io_context io_context;
  rstream::core::windows::blocking_handle stream(io_context.get_executor());
  boost::system::error_code error_code = boost::asio::error::operation_aborted;
  stream.open(read.get(), rstream::core::windows::blocking_handle::access::read, error_code);
  assert(!error_code);

  boost::asio::cancellation_signal cancellation;
  std::array<char, 16> buffer{};
  std::size_t calls = 0;
  stream.async_read_some(
      boost::asio::buffer(buffer),
      boost::asio::bind_cancellation_slot(cancellation.slot(), [&](const boost::system::error_code& error, std::size_t size) {
        assert(error == boost::asio::error::operation_aborted);
        assert(size == 0);
        ++calls;
      }));
  boost::asio::steady_timer timer(io_context);
  timer.expires_after(std::chrono::milliseconds(50));
  timer.async_wait([&](const boost::system::error_code& error) {
    assert(!error);
    cancellation.emit(boost::asio::cancellation_type::terminal);
  });
  io_context.run();
  assert(calls == 1);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_read_from_non_overlapped_pipe();
  check_write_to_non_overlapped_pipe();
  check_cancellation_completes_once();
  return 0;
}

#endif
