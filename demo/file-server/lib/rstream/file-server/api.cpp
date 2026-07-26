// See LICENSE file in the project root for license information.

#include "api.hpp"

#include <sstream>
#include <vector>

#include <boost/format.hpp>
#include <boost/range/iterator_range.hpp>
#include <boost/url/pct_string_view.hpp>

namespace rstream {
namespace file_server {

static const std::string default_page = R"(
<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd"><html> <head> <style>h1{font-size: 80px; font-weight: 800; text-align: center; font-family: "Roboto", sans-serif;}h2{font-size: 25px; text-align: center; font-family: "Roboto", sans-serif; margin-top: -40px;}p{text-align: center; font-family: "Roboto", sans-serif; font-size: 14px;}.container{width: 300px; margin: 0 auto; margin-top: 15%%;}li{font-family: "Roboto", sans-serif; font-size: 14px;}</style> </head> <body> %1% </body></html>
)";

static const std::string default_body_error = R"(
<div class="container"> <h1>%1%</h1> <h2>%2%</h2> <p>The page you are looking for does not exist or an other error occurred. <a href="/">home page</a></p><footer><p>&copy; rstream</p></footer> </div>
)";

static const std::string default_body_www = R"(
<div> %1% <p>&copy; rstream</p></footer> </div>
)";

boost::beast::string_view get_mime_type(const boost::beast::string_view& path)
{
  auto const ext = [&path] {
    auto const pos = path.rfind(".");
    if (pos == boost::beast::string_view::npos) {
      return boost::beast::string_view();
    }
    return path.substr(pos);
  }();
  if (boost::beast::iequals(ext, ".htm")) {
    return "text/html";
  }
  if (boost::beast::iequals(ext, ".html")) {
    return "text/html";
  }
  if (boost::beast::iequals(ext, ".php")) {
    return "text/html";
  }
  if (boost::beast::iequals(ext, ".css")) {
    return "text/css";
  }
  if (boost::beast::iequals(ext, ".txt")) {
    return "text/plain";
  }
  if (boost::beast::iequals(ext, ".js")) {
    return "application/javascript";
  }
  if (boost::beast::iequals(ext, ".json")) {
    return "application/json";
  }
  if (boost::beast::iequals(ext, ".xml")) {
    return "application/xml";
  }
  if (boost::beast::iequals(ext, ".swf")) {
    return "application/x-shockwave-flash";
  }
  if (boost::beast::iequals(ext, ".flv")) {
    return "video/x-flv";
  }
  if (boost::beast::iequals(ext, ".png")) {
    return "image/png";
  }
  if (boost::beast::iequals(ext, ".jpe")) {
    return "image/jpeg";
  }
  if (boost::beast::iequals(ext, ".jpeg")) {
    return "image/jpeg";
  }
  if (boost::beast::iequals(ext, ".jpg")) {
    return "image/jpeg";
  }
  if (boost::beast::iequals(ext, ".gif")) {
    return "image/gif";
  }
  if (boost::beast::iequals(ext, ".bmp")) {
    return "image/bmp";
  }
  if (boost::beast::iequals(ext, ".ico")) {
    return "image/vnd.microsoft.icon";
  }
  if (boost::beast::iequals(ext, ".tiff")) {
    return "image/tiff";
  }
  if (boost::beast::iequals(ext, ".tif")) {
    return "image/tiff";
  }
  if (boost::beast::iequals(ext, ".svg")) {
    return "image/svg+xml";
  }
  if (boost::beast::iequals(ext, ".svgz")) {
    return "image/svg+xml";
  }
  return "application/text";
}

boost::filesystem::path parse_path(const std::string& src, const std::string& prefix, const boost::filesystem::path& workdir)
{
  auto subdir = boost::urls::pct_string_view(src).decode();
  subdir.erase(0, prefix.length());
  return boost::filesystem::canonical(subdir, workdir);
}

void error_response(const context&, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::beast::http::status& status, const std::string& error_message)
{
  response.result(status);
  response.set(boost::beast::http::field::content_type, "text/html");
  response.body() = (boost::format(default_page) % (boost::format(default_body_error) % response.result_int() % error_message)).str();
  response.prepare_payload();
}

void error_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::beast::http::status& status)
{
  std::stringstream str;
  str << status;
  error_response(ctx, response, status, str.str());
}

void redirect(const context&, boost::beast::http::response<boost::beast::http::string_body>& response, const std::string& location)
{
  response.result(boost::beast::http::status::moved_permanently);
  response.set(boost::beast::http::field::location, location);
}

void api_www_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const std::string& target)
{
  auto path = parse_path(target, "/www/", ctx.m_workdir);
  if (boost::filesystem::relative(path, ctx.m_workdir).string().rfind("..", 0) == 0) {
    return error_response(ctx, response, boost::beast::http::status::not_found);
  }
  if (!boost::filesystem::is_directory(path)) {
    return error_response(ctx, response, boost::beast::http::status::not_found);
  }
  api_www_response(ctx, response, path);
}

void api_www_response(const context& ctx, boost::beast::http::response<boost::beast::http::string_body>& response, const boost::filesystem::path& path)
{
  std::stringstream body;
  std::vector<boost::filesystem::path> files;
  std::copy(boost::filesystem::directory_iterator(path), boost::filesystem::directory_iterator(), std::back_inserter(files));
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    auto skip = [&file]() {
      switch (boost::filesystem::detail::status(file).type()) {
        case boost::filesystem::regular_file:
        case boost::filesystem::directory_file:
        case boost::filesystem::symlink_file:
          return false;
        default:
          return true;
      }
    };
    if (skip()) {
      continue;
    }
    std::string linkname;
    auto relative = boost::filesystem::relative(file, ctx.m_workdir);
    if (relative.string().rfind("..", 0) == 0) {
      continue;
    }
    if (boost::filesystem::is_directory(file)) {
      linkname = (boost::filesystem::path("/www") / relative).string();
    }
    else {
      linkname = (boost::filesystem::path("/api/file") / relative).string();
    }
    auto displayname = file.filename().string();
    if (boost::filesystem::is_directory(file)) {
      displayname += "/";
    }
    else if (boost::filesystem::is_symlink(file)) {
      displayname += "@";
    }
    body << (boost::format("<li><a href=\"%1%\">%2%</a></li>") % linkname % displayname).str();
  }
  response.result(boost::beast::http::status::ok);
  response.set(boost::beast::http::field::content_type, (boost::format("text/html; charset=%1%") % "utf-8").str());
  response.body() = (boost::format(default_page) % (boost::format(default_body_www) % body.str())).str();
  response.prepare_payload();
}

}  // namespace file_server
}  // namespace rstream
