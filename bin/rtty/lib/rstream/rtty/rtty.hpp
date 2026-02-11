// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant.hpp>

namespace rstream {
namespace rtty {

using executor_type = boost::asio::io_context::executor_type;

namespace protocol {

enum type {
  websocket = 0,
  plain     = 1
};

struct options {
  bool m_interactive;
  bool m_allocate_tty;
  bool m_send_heartbeat;
};

struct environment {
  std::string m_key;
  std::string m_value;
};

using env_vars = std::list<environment>;

using cmd_args = std::list<std::string>;

using workdir = boost::optional<std::string>;

using identifier = boost::variant<std::uint32_t, std::string>;

using username = boost::optional<identifier>;

struct user_info {
  std::string m_name;
  std::string m_shell;
  std::string m_home;
#ifndef _WIN32
  std::uint32_t m_uid;
  std::uint32_t m_gid;
#endif
};

struct config {
  type m_protocol_type;
  options m_options;
  env_vars m_env_vars;
  cmd_args m_cmd_args;
  workdir m_workdir;
  username m_username;
};

void parse_environment(env_vars& dst, const std::vector<std::string>& src);

env_vars::iterator find_environment_variable(env_vars& dst, const std::string& key);

void add_environment_variable(std::list<environment>& dst, const std::string& key, const std::string& value, bool force = false);

void add_environment_variable(std::list<environment>& dst, const std::string& key, bool force = false);

void parse_type(type& dst, const std::string& src);

#ifdef _WIN32

void get_user_info(user_info& user_info, std::error_code& error_code);

#else

void parse_identifier(identifier& dst, const std::string& src);

void parse_username(username& dst, const std::string& src);

void get_user_info(user_info& user_info, const username& username, std::error_code& error_code);

#endif

}  // namespace protocol

// Build rstream URI for webtty tunnels with the default labels.
std::string build_webtty_uri();

struct settings {
  std::uint32_t m_mtu;
  struct {
    std::uint32_t m_open;
    std::uint32_t m_close;
    std::uint32_t m_heartbeat;
  } m_timeouts_ms;
};

struct settings_client {
  settings m_common;
  std::uint32_t m_std_in_buffer_size;
};

struct settings_server {
  settings m_common;
  std::uint32_t m_timeouts_start_ms;
  std::uint32_t m_std_out_buffer_size;
  std::uint32_t m_std_err_buffer_size;
};

struct terminal_size {
  unsigned short m_row;    /* rows, in characters        */
  unsigned short m_col;    /* columns, in characters     */
  unsigned short m_xpixel; /* horizontal size, pixels    */
  unsigned short m_ypixel; /* vertical size, pixels      */
};

}  // namespace rtty
}  // namespace rstream
