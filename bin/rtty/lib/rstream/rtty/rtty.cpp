// See LICENSE file in the project root for license information.

#include "rtty.hpp"

#ifdef _WIN32
#include <lmcons.h>
#include <windows.h>
#else
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#endif

#include <cstdlib>

#include <rstream/core/log.hpp>

#include "error.hpp"

static const rstream::core::logger g_logger({"rstream", "rtty", "core"});

namespace rstream {
namespace rtty {

namespace protocol {

void parse_environment(env_vars& dst, const std::vector<std::string>& src)
{
  static const std::string delimiter = "=";
  dst.clear();
  for (const auto& str : src) {
    if (str.empty()) {
      continue;
    }
    std::string key, value;
    auto pos = str.find(delimiter);
    if (pos != std::string::npos) {
      key   = str.substr(0, pos);
      value = str.substr(pos + delimiter.size());
    }
    else {
      key      = str;
      auto env = std::getenv(key.c_str());
      if (env != nullptr) {
        value = env;
      }
      else {
        continue;
      }
    }
    dst.push_back((environment){.m_key = key, .m_value = value});
  }
}

env_vars::iterator find_environment_variable(env_vars& dst, const std::string& key)
{
  env_vars::iterator it;
  for (it = dst.begin(); it != dst.end(); ++it) {
    if (it->m_key == key) {
      break;
    }
  }
  return it;
}

void add_environment_variable(std::list<environment>& dst, const std::string& key, const std::string& value, bool force)
{
  auto it = find_environment_variable(dst, key);
  if (force || it == dst.end()) {
    environment environment = {
        .m_key   = key,
        .m_value = value,
    };
    if (it != dst.end()) {
      *it = environment;
    }
    else {
      dst.push_back(environment);
    }
  }
}

void add_environment_variable(env_vars& dst, const std::string& key, bool force)
{
  auto value = std::getenv(key.c_str());
  if (value != nullptr) {
    add_environment_variable(dst, key, value, force);
  }
}

void parse_type(type& dst, const std::string& src)
{
  if (src == "websocket") {
    dst = type::websocket;
  }
  else if (src == "plain") {
    dst = type::plain;
  }
  else {
    throw std::runtime_error("invalid protocol '" + src + "'");
  }
}

#ifdef _WIN32

void get_user_info(user_info& user_info, std::error_code& error_code)
{
  char username[UNLEN + 1];
  DWORD username_len = UNLEN + 1;
  if (!::GetUserNameA(username, &username_len)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
  if (error_code) {
    return;
  }
  const char* userprofile = getenv("USERPROFILE");
  if (!userprofile) {
#ifdef DEBUG_BUILD
    g_logger->warn("USERPROFILE environment variable is not set");
#endif
    error_code = error::code::server_error;
  }
  if (error_code) {
    return;
  }
  const char* comspec = std::getenv("ComSpec");
  if (!comspec) {
#ifdef DEBUG_BUILD
    g_logger->warn("ComSpec environment variable is not set");
#endif
    error_code = error::code::server_error;
  }
  user_info = {
      .m_name  = username,
      .m_shell = comspec,
      .m_home  = userprofile,
  };
}

#else

void parse_identifier(identifier& dst, const std::string& src)
{
  auto is_str_id = [](const std::string& str) {
    auto it = str.begin();
    while (it != str.end() && std::isdigit(*it)) {
      ++it;
    }
    return !str.empty() && it == str.end();
  };
  if (is_str_id(src)) {
    dst = std::stoul(src);
  }
  else {
    dst = src;
  }
}

void parse_username(username& dst, const std::string& src)
{
  if (!src.empty()) {
    identifier identifier;
    parse_identifier(identifier, src);
    dst = identifier;
  }
}

void get_user_info(user_info& user_info, const username& username, std::error_code& error_code)
{
  identifier user = username ? username.get() : getuid();
  struct passwd* pw;
  if (user.type() == typeid(std::string)) {
    pw = getpwnam(boost::get<std::string>(user).c_str());
  }
  else if (user.type() == typeid(std::uint32_t)) {
    pw = getpwuid(boost::get<std::uint32_t>(user));
  }
  if (!pw) {
    error_code = std::error_code(errno, std::system_category());
  }
  else {
    user_info = {
        .m_name  = pw->pw_name,
        .m_shell = pw->pw_shell,
        .m_home  = pw->pw_dir,
        .m_uid   = pw->pw_uid,
        .m_gid   = pw->pw_gid,
    };
  }
}

#endif

}  // namespace protocol

}  // namespace rtty
}  // namespace rstream
