// See LICENSE file in the project root for license information.

#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/asio/connect.hpp>

#include <rstream/io/payloader.hpp>

static const std::string g_host = "localhost";

static const unsigned short g_port = 1234;

static const std::size_t g_mtu = 1500;

int main(int argc, char** argv)
{
  boost::asio::io_context io_context;
  auto executor_work_guard = std::make_shared<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(io_context.get_executor());
  std::thread thread(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
  try {
    // connect to remote
    using socket_type    = boost::asio::ip::tcp::socket;
    using payloader_type = rstream::io::payloader<socket_type&>;
    socket_type socket(io_context);
    payloader_type payloader(socket);
    boost::asio::ip::tcp::resolver resolver(io_context);
    boost::asio::connect(payloader.next_layer(), resolver.resolve(g_host, std::to_string(g_port)));
    // allocate memory
    auto memory = rstream::core::make_memory_allocated(g_mtu);
    // prepare and send request
    {
      std::cout << "enter request: ";
      rstream::core::buffer buffer;
      auto request = memory;
      std::cin.getline((char*)request.get_data(), request.get_size());
      request.set_size(std::strlen((const char*)request.get_const_data()));
      buffer.append(request);
      payloader.async_send(buffer, boost::asio::use_future).get();
    }
    // wait for reply
    {
      rstream::core::buffer buffer;
      buffer.append(memory);
      payloader.async_recv(buffer, boost::asio::use_future).get();
      std::cout << "received response: ";
      auto reply = buffer.map(rstream::core::buffer::map_mode::read);
      std::cout.write((const char*)reply.get_const_data(), reply.get_size());
      std::cout << "\n";
    }
  }
  catch (const std::exception& exception) {
    std::cerr << "exception: " << exception.what() << "\n";
  }
  executor_work_guard = nullptr;
  thread.join();
  return 0;
}
