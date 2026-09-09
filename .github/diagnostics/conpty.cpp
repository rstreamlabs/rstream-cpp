// See LICENSE file in the project root for license information.

#define BOOST_PROCESS_VERSION 1

#include <cassert>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#if __has_include(<boost/process/v1/args.hpp>)
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/exe.hpp>
#else
#include <boost/process/args.hpp>
#include <boost/process/exe.hpp>
#endif

#include <rstream/webtty/detail/process.hpp>
#include <rstream/webtty/error.hpp>
#include <rstream/webtty/stream.hpp>
#include <rstream/webtty/terminal.hpp>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace stream = rstream::webtty::stream;

#ifdef _WIN32
static FILE* trace_file()
{
  static FILE* file = [] {
    char path[128];
    std::snprintf(path, sizeof(path), "conpty-diagnostic/trace-%lu.log", ::GetCurrentProcessId());
    FILE* result = nullptr;
    ::fopen_s(&result, path, "w");
    return result ? result : stderr;
  }();
  return file;
}

static int windows_pty_console_child()
{
  for (const auto channel : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
    DWORD mode = 0;
    if (!::GetConsoleMode(::GetStdHandle(channel), &mode)) {
      return 10;
    }
  }
  DWORD count        = 0;
  const char ready[] = "CONPTY_READY\n";
  if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ready, sizeof(ready) - 1, &count, nullptr)) {
    return 11;
  }
  char input[128] = {};
  if (!::ReadFile(::GetStdHandle(STD_INPUT_HANDLE), input, sizeof(input), &count, nullptr)
      || std::string(input, count) != "conpty-input\r\n") {
    return 12;
  }
  const char output[] = "CONPTY_STDIN_OK\n";
  const char error[]  = "CONPTY_STDERR_OK\n";
  if (!::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), output, sizeof(output) - 1, &count, nullptr)
      || !::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), error, sizeof(error) - 1, &count, nullptr)) {
    return 13;
  }
  return 0;
}

static void check_windows_pty_console_io(const char* executable)
{
  boost::asio::io_context io_context;
  auto work       = boost::asio::make_work_guard(io_context);
  auto stream_ptr = stream::make_stream(io_context.get_executor(), stream::backend::tty);
  std::fprintf(trace_file(), "PROBE make_child pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  auto child      = rstream::webtty::detail::process::make_child(
      stream_ptr,
      boost::process::exe(executable),
      boost::process::args(std::vector<std::string>{"--conpty-console-child"}));
  std::fprintf(trace_file(), "PROBE child_created pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  char buffer[4096]  = {};
  const char input[] = "conpty-input\r";
  std::string output;
  std::promise<void> output_complete;
  auto output_ready    = output_complete.get_future();
  bool output_reported = false;
  bool input_sent      = false;
  bool input_written   = false;
  std::function<void()> read;
  read = [&] {
    stream::base::async_read_some_completion_handler handler =
        [&](const std::error_code& error_code, std::size_t count) {
          if (error_code || output.size() + count > 16384) {
            return;
          }
          std::fprintf(trace_file(), "PROBE read count=%zu error=%d\n", count, error_code.value()); std::fflush(trace_file());
          output.append(buffer, count);
          if (!input_sent && output.find("CONPTY_READY") != std::string::npos) {
            input_sent = true;
            stream::base::async_write_completion_handler write_handler =
                [&](const std::error_code& write_error, std::size_t written) {
                  input_written = !write_error && written == sizeof(input) - 1;
                };
            stream_ptr->async_write(boost::asio::buffer(input, sizeof(input) - 1), stream::type::std_in, std::move(write_handler));
          }
          if (!output_reported && output.find("CONPTY_STDIN_OK") != std::string::npos
              && output.find("CONPTY_STDERR_OK") != std::string::npos) {
            output_reported = true;
            output_complete.set_value();
          }
          read();
        };
    stream_ptr->async_read_some(boost::asio::buffer(buffer), stream::type::std_out, std::move(handler));
  };
  std::fprintf(trace_file(), "PROBE start_reader pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  read();
  std::thread runner([&] { io_context.run(); });
  std::fprintf(trace_file(), "PROBE wait_child pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  const auto exited = ::WaitForSingleObject(child->native_handle(), 10000) == WAIT_OBJECT_0;
  boost::system::error_code ignored;
  if (!exited) {
    child->terminate(ignored);
  }
  std::fprintf(trace_file(), "PROBE reap_child pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  child->wait(ignored);
  std::fprintf(trace_file(), "PROBE drain_output pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  const auto drained = exited && child->exit_code() == 0
                       && output_ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  std::fprintf(trace_file(), "PROBE close_stream pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  stream_ptr->close();
  std::fprintf(trace_file(), "PROBE reset_work pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  work.reset();
  std::fprintf(trace_file(), "PROBE join_runner pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  runner.join();
  std::fprintf(trace_file(), "PROBE verify_exit pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  assert(exited);
  assert(child->exit_code() == 0);
  assert(drained);
  assert(input_written);
  assert(output.find("CONPTY_STDIN_OK") != std::string::npos);
  assert(output.find("CONPTY_STDERR_OK") != std::string::npos);
}

static void check_windows_pty_with_redirected_parent(const char* executable)
{
  boost::process::child parent(
      boost::process::exe(executable),
      boost::process::args(std::vector<std::string>{"--conpty-redirected-parent"}),
      boost::process::std_in<boost::process::null,
                             boost::process::std_out>
          boost::process::null);
  const auto exited = ::WaitForSingleObject(parent.native_handle(), 20000) == WAIT_OBJECT_0;
  boost::system::error_code ignored;
  if (!exited) {
    HANDLE dump = ::CreateFileA("conpty-diagnostic/parent.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
      ::MiniDumpWriteDump(parent.native_handle(), parent.id(), dump, static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), nullptr, nullptr, nullptr);
      ::CloseHandle(dump);
    }
    parent.terminate(ignored);
  }
  parent.wait(ignored);
  std::fprintf(trace_file(), "PROBE verify_exit pid=%lu\n", ::GetCurrentProcessId()); std::fflush(trace_file());
  assert(exited);
  assert(parent.exit_code() == 0);
}


#endif
int main(int argc, char** argv) {
#ifdef _WIN32
::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
std::fprintf(trace_file(),"PROBE main argc=%d arg=%s\n",argc, argc>1?argv[1]:"none");std::fflush(trace_file());
if(argc==2 && std::strcmp(argv[1], "--conpty-console-child")==0) return windows_pty_console_child();
if(argc==2 && std::strcmp(argv[1], "--conpty-redirected-parent")==0) {check_windows_pty_console_io(argv[0]); return 0;}
check_windows_pty_with_redirected_parent(argv[0]);
#endif
return 0;
}
