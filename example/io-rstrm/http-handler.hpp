// See LICENSE file in the project root for license information.

#pragma once

#include <iostream>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace example_http {

// The same handler is instantiated for an ordinary TCP socket and a direct
// rstream socket. Neither tunnel setup nor agent credentials belong here.
template <typename AsyncStream>
boost::asio::awaitable<void> serve(AsyncStream socket)
{
  namespace http = boost::beast::http;
  try {
    http::request<http::string_body> request;
    boost::beast::flat_buffer buffer;
    co_await http::async_read(socket, buffer, request, boost::asio::use_awaitable);
    http::response<http::string_body> response{http::status::ok, request.version()};
    response.set(http::field::content_type, "text/plain");
    response.keep_alive(false);
    response.body() = "Hello from the shared Beast handler";
    response.prepare_payload();
    co_await http::async_write(socket, response, boost::asio::use_awaitable);
  }
  catch (const std::exception& error) {
    std::cerr << "HTTP session error: " << error.what() << std::endl;
  }
  boost::system::error_code ignored;
  socket.close(ignored);
}

}  // namespace example_http
