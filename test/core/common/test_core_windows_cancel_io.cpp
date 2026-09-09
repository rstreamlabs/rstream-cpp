// See LICENSE file in the project root for license information.

#ifdef _WIN32

#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>

#include <rstream/core/windows/detail/cancel_io.hpp>

static void check_cancellation_before_system_call(bool writing)
{
  HANDLE input  = nullptr;
  HANDLE output = nullptr;
  assert(::CreatePipe(&input, &output, nullptr, 4096));
  const auto gate = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
  assert(gate != nullptr);

  std::array<char, 16384> buffer{};
  DWORD error = ERROR_SUCCESS;
  std::promise<void> ready;
  auto worker_ready = ready.get_future();
  std::thread worker([&] {
    ready.set_value();
    assert(::WaitForSingleObject(gate, INFINITE) == WAIT_OBJECT_0);
    DWORD transferred  = 0;
    const auto success = writing
                             ? ::WriteFile(output, buffer.data(), static_cast<DWORD>(buffer.size()), &transferred, nullptr)
                             : ::ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &transferred, nullptr);
    error              = success ? ERROR_SUCCESS : ::GetLastError();
  });
  worker_ready.wait();
  std::size_t cancellations = 0;
  auto completed            = std::async(std::launch::async, [&] {
    rstream::core::windows::detail::cancel_and_join(worker, [&](HANDLE thread) {
      const auto cancelled    = ::CancelSynchronousIo(thread);
      const auto cancel_error = cancelled ? ERROR_SUCCESS : ::GetLastError();
      if (++cancellations == 1) {
        // The first cancellation necessarily misses: release the worker only
        // after it returns, so the system call begins during shutdown.
        assert(!cancelled && cancel_error == ERROR_NOT_FOUND);
        assert(::SetEvent(gate));
      }
    });
  });
  const auto stopped        = completed.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  if (!stopped) {
    // Unblock a regressed single-cancellation implementation before failing.
    auto& peer = writing ? input : output;
    ::CloseHandle(peer);
    peer = nullptr;
  }
  completed.get();
  assert(stopped);
  assert(!worker.joinable());
  assert(cancellations >= 2);
  assert(error == ERROR_OPERATION_ABORTED);
  DWORD flags = 0;
  assert(::GetHandleInformation(input, &flags));
  assert(::GetHandleInformation(output, &flags));
  ::CloseHandle(input);
  ::CloseHandle(output);
  ::CloseHandle(gate);
}

int main()
{
  std::thread empty;
  rstream::core::windows::detail::cancel_and_join(empty, [](HANDLE) { assert(false); });
  check_cancellation_before_system_call(false);
  check_cancellation_before_system_call(true);
  return 0;
}

#endif
