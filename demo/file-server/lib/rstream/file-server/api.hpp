// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/filesystem.hpp>

#include "file-server.hpp"

namespace rstream {
namespace file_server {

boost::beast::string_view get_mime_type(const boost::beast::string_view& path);

boost::filesystem::path parse_path(const std::string& src, const std::string& prefix, const boost::filesystem::path& workdir);

void error_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::beast::http::status& status, const std::string& error_message);

void error_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::beast::http::status& status);

void redirect(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const std::string& location);

void api_www_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const std::string& target);

void api_www_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::filesystem::path& path);

template <class request, class response>
void prepare_response(request&& req, response&& res)
{
  res.version(req.version());
  res.keep_alive(req.keep_alive());
}

template <class body, class allocator, class send_func>
void send_error(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const boost::beast::http::status& status, const std::string& error_message)
{
  boost::beast::http::response<boost::beast::http::string_body> response;
  prepare_response(request, response);
  error_response(ctx, response, status, error_message);
  return func(std::move(response));
}

template <class body, class allocator, class send_func>
void send_error(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const boost::beast::http::status& status)
{
  boost::beast::http::response<boost::beast::http::string_body> response;
  prepare_response(request, response);
  error_response(ctx, response, status);
  return func(std::move(response));
}

template <class body, class allocator, class send_func>
void bad_request(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const std::string& error_message)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::bad_request, error_message);
}

template <class body, class allocator, class send_func>
void bad_request(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::bad_request);
}

template <class body, class allocator, class send_func>
void forbidden(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const std::string& error_message)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::forbidden, error_message);
}

template <class body, class allocator, class send_func>
void forbidden(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::forbidden);
}

template <class body, class allocator, class send_func>
void not_found(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const std::string& error_message)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::not_found, error_message);
}

template <class body, class allocator, class send_func>
void not_found(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::not_found);
}

template <class body, class allocator, class send_func>
void internal_server_error(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func, const std::string& error_message)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::internal_server_error, error_message);
}

template <class body, class allocator, class send_func>
void internal_server_error(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  return send_error(ctx, std::move(request), func, boost::beast::http::status::internal_server_error);
}

template <class body, class allocator, class send_func>
void api_redirect_www(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  boost::beast::http::response<boost::beast::http::string_body> response;
  prepare_response(request, response);
  redirect(ctx, response, "www");
  return func(std::move(response));
}

template <class body, class allocator, class send_func>
void api_www(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  boost::beast::http::response<boost::beast::http::string_body> response;
  prepare_response(request, response);
  api_www_response(ctx, response, std::string(request.target()));
  return func(std::move(response));
}

template <class body, class allocator, class send_func>
void api_get_file(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&& request, send_func&& func)
{
  auto path = parse_path(std::string(request.target()), "/api/file/", ctx.m_workdir);
  if (boost::filesystem::relative(path, ctx.m_workdir).string().rfind("..", 0) == 0) {
    not_found(ctx, std::move(request), func);
  }
  else {
    auto skip = [&path]() {
      switch (boost::filesystem::detail::status(path).type()) {
        case boost::filesystem::regular_file:
        case boost::filesystem::symlink_file:
          return false;
        default:
          return true;
      }
    };
    if (skip()) {
      forbidden(ctx, std::move(request), func);
    }
    else {
      auto filename = path.string();
      boost::beast::error_code error_code;
      boost::beast::http::file_body::value_type file;
      file.open(filename.c_str(), boost::beast::file_mode::scan, error_code);
      if (error_code == boost::beast::errc::no_such_file_or_directory) {
        not_found(ctx, std::move(request), func);
      }
      else if (error_code == boost::beast::errc::permission_denied) {
        forbidden(ctx, std::move(request), func);
      }
      else if (error_code) {
        internal_server_error(ctx, std::move(request), func, error_code.message());
      }
      else {
        auto const size = file.size();
        if (request.method() == boost::beast::http::verb::head) {
          boost::beast::http::response<boost::beast::http::empty_body> response;
          prepare_response(request, response);
          response.result(boost::beast::http::status::ok);
          response.set(boost::beast::http::field::content_type, get_mime_type(filename));
          response.content_length(size);
          return func(std::move(response));
        }
        else {
          boost::beast::http::response<boost::beast::http::file_body> response(std::piecewise_construct, std::make_tuple(std::move(file)));
          prepare_response(request, response);
          response.result(boost::beast::http::status::ok);
          response.set(boost::beast::http::field::content_type, get_mime_type(filename));
          response.content_length(size);
          return func(std::move(response));
        }
      }
    }
  }
}

}  // namespace file_server
}  // namespace rstream
