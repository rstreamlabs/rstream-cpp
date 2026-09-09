// See LICENSE file in the project root for license information.

#pragma once

#ifdef _WIN32

#include <thread>

#include <windows.h>

namespace rstream {
namespace core {
namespace windows {
namespace detail {

struct cancel_synchronous_io {
  void operator()(HANDLE thread) const noexcept
  {
    // ERROR_NOT_FOUND is expected when the worker has not entered its I/O yet.
    ::CancelSynchronousIo(thread);
  }
};

// The caller must stop admission and wake the dedicated worker first. Keep its
// I/O handles open until this returns: cancellation can precede the system call,
// and closing a synchronous handle with a pending read can itself block.
template <typename Cancel = cancel_synchronous_io>
void cancel_and_join(std::thread& thread, Cancel cancel = {})
{
  if (!thread.joinable()) {
    return;
  }
  const auto handle = thread.native_handle();
  do {
    cancel(handle);
    // Thread completion wakes this immediately. The timeout only retries a
    // cancellation that raced with the worker entering ReadFile or WriteFile.
  } while (::WaitForSingleObject(handle, 10) == WAIT_TIMEOUT);
  thread.join();
}

}  // namespace detail
}  // namespace windows
}  // namespace core
}  // namespace rstream

#endif
