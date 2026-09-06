// See LICENSE file in the project root for license information.

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rstream/test/time.hpp>
#include <rstream/webtty/detail/fifo_reader.hpp>

using reader_type = rstream::webtty::detail::fifo_reader;

struct named_pipe {
  explicit named_pipe(boost::asio::io_context& context)
      : input(context),
        output(context)
  {
    std::array<char, 40> directory{};
    std::string("/tmp/rstream-webtty-fifo-XXXXXX").copy(directory.data(), directory.size() - 1);
    assert(::mkdtemp(directory.data()) != nullptr);
    const auto path = std::string(directory.data()) + "/input";
    assert(::mkfifo(path.c_str(), 0600) == 0);
    input.assign(::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    output.assign(::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
    assert(::unlink(path.c_str()) == 0);
    assert(::rmdir(directory.data()) == 0);
  }

  boost::asio::posix::stream_descriptor input;
  boost::asio::posix::stream_descriptor output;
};

static void check_transfer_with_open_writer(std::size_t buffer_size)
{
  boost::asio::io_context context;
  auto strand = boost::asio::make_strand(context);
  named_pipe pipe(context);
  reader_type reader(strand, pipe.input.native_handle());
  std::string expected(1024 * 1024, '\0');
  for (std::size_t i = 0; i < expected.size(); ++i) {
    expected[i] = static_cast<char>(i % 251);
  }
  std::vector<char> buffer(buffer_size);
  std::string received;
  bool done                       = false;
  bool timed_out                  = false;
  std::atomic<bool> stop_writer   = false;
  std::atomic<bool> writer_failed = false;
  boost::asio::steady_timer deadline(strand, rstream::test::timeout(std::chrono::seconds(5)));
  deadline.async_wait([&](auto error) {
    if (!error) {
      timed_out = true;
      reader.close();
    }
  });
  std::function<void()> read;
  read = [&] {
    reader.async_read_some(boost::asio::buffer(buffer), boost::asio::bind_executor(strand, [&](auto error, auto size) {
                             assert(strand.running_in_this_thread());
                             if (error) {
                               assert(timed_out);
                               return;
                             }
                             received.append(buffer.data(), size);
                             if (received.size() == expected.size()) {
                               done = true;
                               deadline.cancel();
                               reader.close();
                             }
                             else {
                               read();
                             }
                           }));
  };
  read();
  std::thread writer([&] {
    std::size_t offset = 0;
    while (offset < expected.size() && !stop_writer) {
      pollfd ready{pipe.output.native_handle(), POLLOUT, 0};
      const auto result = ::poll(&ready, 1, 100);
      if (result == -1 && errno == EINTR) {
        continue;
      }
      if (result < 0 || (ready.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        writer_failed = true;
        break;
      }
      if (result == 0) {
        continue;
      }
      const auto size = ::write(pipe.output.native_handle(), expected.data() + offset, expected.size() - offset);
      if (size < 0 && (errno == EINTR || errno == EAGAIN)) {
        continue;
      }
      if (size <= 0) {
        writer_failed = true;
        break;
      }
      offset += static_cast<std::size_t>(size);
    }
  });
  std::vector<std::thread> workers;
  for (int i = 0; i < 3; ++i) {
    workers.emplace_back([&] { context.run(); });
  }
  context.run();
  for (auto& worker : workers) {
    worker.join();
  }
  stop_writer = true;
  writer.join();
  assert(!writer_failed);
  assert(!timed_out);
  assert(done);
  assert(received == expected);
  assert(pipe.output.is_open());
}

static void check_cancellation_and_destruction()
{
  boost::asio::io_context context;
  auto strand = boost::asio::make_strand(context);
  named_pipe pipe(context);
  std::array<char, 128> buffer;
  int cancelled = 0;
  int rejected  = 0;
  {
    reader_type reader(strand, pipe.input.native_handle());
    reader.async_read_some(boost::asio::buffer(buffer), boost::asio::bind_executor(strand, [&](auto error, auto size) {
                             assert(strand.running_in_this_thread());
                             assert(error == boost::asio::error::operation_aborted);
                             assert(size == 0);
                             ++cancelled;
                           }));
    reader.async_read_some(boost::asio::buffer(buffer), [&](auto error, auto size) {
      assert(error == boost::asio::error::already_started);
      assert(size == 0);
      ++rejected;
    });
  }
  assert(cancelled == 0);
  assert(rejected == 0);
  context.run();
  assert(cancelled == 1);
  assert(rejected == 1);
}

static void check_eof_and_repeated_close()
{
  boost::asio::io_context context;
  named_pipe pipe(context);
  reader_type reader(context.get_executor(), pipe.input.native_handle());
  std::array<char, 128> buffer;
  pipe.output.close();
  bool eof = false;
  reader.async_read_some(boost::asio::buffer(buffer), [&](auto error, auto size) {
    assert(error == boost::asio::error::eof);
    assert(size == 0);
    eof = true;
    reader.close();
    reader.close();
  });
  context.run();
  assert(eof);
  context.restart();
  bool cancelled = false;
  reader.async_read_some(boost::asio::buffer(buffer), [&](auto error, auto size) {
    assert(error == boost::asio::error::operation_aborted);
    assert(size == 0);
    cancelled = true;
  });
  context.run();
  assert(cancelled);
}

static void check_pending_eof_and_empty_buffer()
{
  boost::asio::io_context context;
  auto strand = boost::asio::make_strand(context);
  named_pipe pipe(context);
  reader_type reader(strand, pipe.input.native_handle());
  std::array<char, 128> buffer;
  int completed = 0;
  reader.async_read_some(boost::asio::mutable_buffer(), [&](auto error, auto size) {
    assert(!error);
    assert(size == 0);
    ++completed;
    reader.async_read_some(boost::asio::buffer(buffer), [&](auto read_error, auto read_size) {
      assert(read_error == boost::asio::error::eof);
      assert(read_size == 0);
      ++completed;
      reader.close();
    });
    boost::asio::post(strand, [&] { pipe.output.close(); });
  });
  context.run();
  assert(completed == 2);
}

static void check_descriptor_limit_is_rejected()
{
  boost::asio::io_context context;
  for (int descriptor : {-1, FD_SETSIZE}) {
    bool rejected = false;
    try {
      reader_type reader(context.get_executor(), descriptor);
    }
    catch (const boost::system::system_error& error) {
      rejected = error.code() == boost::asio::error::fd_set_failure;
    }
    assert(rejected);
  }
}

int main()
{
  for (int i = 0; i < 16; ++i) {
    check_transfer_with_open_writer(i % 2 == 0 ? 4096 : 800 * 1024);
    check_cancellation_and_destruction();
    check_pending_eof_and_empty_buffer();
  }
  check_eof_and_repeated_close();
  check_descriptor_limit_is_rejected();
}
