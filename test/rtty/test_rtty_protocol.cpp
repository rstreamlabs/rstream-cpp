// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <boost/url.hpp>

#include <rstream/rtty/error.hpp>
#include <rstream/rtty/rtty.hpp>

namespace protocol = rstream::rtty::protocol;

class env_guard {
 public:
  explicit env_guard(const char* key)
      : m_key(key)
  {
    const char* value = std::getenv(key);
    if (value != nullptr) {
      m_present = true;
      m_value   = value;
    }
  }

  ~env_guard()
  {
    if (m_present) {
      set(m_value);
    }
    else {
      unset();
    }
  }

  void set(const std::string& value)
  {
#ifdef _WIN32
    _putenv_s(m_key, value.c_str());
#else
    setenv(m_key, value.c_str(), 1);
#endif
  }

  void unset()
  {
#ifdef _WIN32
    _putenv_s(m_key, "");
#else
    unsetenv(m_key);
#endif
  }

 private:
  const char* m_key;
  bool m_present = false;
  std::string m_value;
};

static boost::urls::url parse_url(const std::string& uri)
{
  auto parsed = boost::urls::parse_uri(uri);
  assert(parsed);
  return boost::urls::url(parsed.value());
}

static const protocol::environment& env_at(const protocol::env_vars& env_vars, std::size_t index)
{
  auto it = env_vars.begin();
  std::advance(it, index);
  return *it;
}

static void check_environment_parsing_and_updates()
{
  env_guard inherited("RSTREAM_CPP_TEST_INHERITED");
  env_guard missing("RSTREAM_CPP_TEST_MISSING");
  inherited.set("from-process");
  missing.unset();

  protocol::env_vars env_vars;
  protocol::parse_environment(env_vars, {
      "A=1",
      "",
      "EMPTY=",
      "COMPLEX=a=b=c",
      "RSTREAM_CPP_TEST_INHERITED",
      "RSTREAM_CPP_TEST_MISSING",
  });

  assert(env_vars.size() == 4);
  assert(env_at(env_vars, 0).m_key == "A");
  assert(env_at(env_vars, 0).m_value == "1");
  assert(env_at(env_vars, 1).m_key == "EMPTY");
  assert(env_at(env_vars, 1).m_value.empty());
  assert(env_at(env_vars, 2).m_key == "COMPLEX");
  assert(env_at(env_vars, 2).m_value == "a=b=c");
  assert(env_at(env_vars, 3).m_key == "RSTREAM_CPP_TEST_INHERITED");
  assert(env_at(env_vars, 3).m_value == "from-process");

  auto found = protocol::find_environment_variable(env_vars, "A");
  assert(found != env_vars.end());
  protocol::add_environment_variable(env_vars, "A", "ignored");
  assert(found->m_value == "1");
  protocol::add_environment_variable(env_vars, "A", "overwritten", true);
  assert(found->m_value == "overwritten");
  protocol::add_environment_variable(env_vars, "B", "2");
  assert(protocol::find_environment_variable(env_vars, "B") != env_vars.end());
}

static void check_protocol_type_parsing()
{
  protocol::type type;
  protocol::parse_type(type, "websocket");
  assert(type == protocol::type::websocket);
  protocol::parse_type(type, "plain");
  assert(type == protocol::type::plain);
  bool rejected = false;
  try {
    protocol::parse_type(type, "ssh");
  }
  catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

static void check_identifier_and_username_parsing()
{
  protocol::identifier identifier;
  protocol::parse_identifier(identifier, "42");
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == 42);

  protocol::parse_identifier(identifier, "0042");
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == 42);

  protocol::parse_identifier(identifier, "alice42");
  assert(identifier.type() == typeid(std::string));
  assert(boost::get<std::string>(identifier) == "alice42");

  protocol::parse_identifier(identifier, std::to_string(std::numeric_limits<std::uint32_t>::max()));
  assert(identifier.type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(identifier) == std::numeric_limits<std::uint32_t>::max());

  bool rejected = false;
  try {
    protocol::parse_identifier(identifier, "4294967296");
  }
  catch (const std::out_of_range&) {
    rejected = true;
  }
  assert(rejected);

  protocol::username username = protocol::identifier(std::string("stale-user"));
  protocol::parse_username(username, "");
  assert(!username);

  protocol::parse_username(username, "1000");
  assert(username);
  assert(username->type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(*username) == 1000);

  protocol::parse_username(username, "operator");
  assert(username);
  assert(username->type() == typeid(std::string));
  assert(boost::get<std::string>(*username) == "operator");
}

#ifndef _WIN32
static void check_user_info_error_paths_do_not_report_success()
{
  protocol::user_info user_info;
  std::error_code error_code;
  protocol::get_user_info(user_info, boost::none, error_code);
  assert(!error_code);
  assert(!user_info.m_name.empty());
  assert(!user_info.m_home.empty());

  protocol::username missing_by_name = protocol::identifier(std::string("rstream-cpp-user-that-should-not-exist"));
  protocol::get_user_info(user_info, missing_by_name, error_code);
  assert(error_code);

  protocol::username missing_by_id = protocol::identifier(std::numeric_limits<std::uint32_t>::max());
  protocol::get_user_info(user_info, missing_by_id, error_code);
  assert(error_code);
}
#endif

static void check_webtty_uri_is_publishable_and_labelled()
{
  const auto uri = rstream::rtty::build_webtty_uri();
  const auto url = parse_url(uri);
  assert(url.scheme() == "rstrm");

  auto params = url.params();
  assert(params.find("rstrm.publish") != params.end());
  assert((*params.find("rstrm.publish")).value == "true");
  assert((*params.find("rstrm.protocol")).value == "http");
  assert((*params.find("rstrm.token_auth")).value == "true");

  std::set<std::string> labels;
  for (const auto& param : params) {
    if (param.key == "rstrm.labels") {
      labels.insert(std::string(param.value));
    }
  }
  assert(labels.count("application-protocol=rstream.webtty") == 1);
  assert(labels.count("rstream.webtty.capabilities=exec") == 1);
  assert(labels.count("rstream.webtty.exec.path=/") == 1);
  bool has_os_family = false;
  for (const auto& label : labels) {
    if (label.find("rstream.webtty.os_family=") == 0 && label.size() > std::string("rstream.webtty.os_family=").size()) {
      has_os_family = true;
      break;
    }
  }
  assert(has_os_family);
}

static void check_error_category_and_messages()
{
  assert(std::string(rstream::rtty::error::rstream_rtty_error_category().name()) == "rstream::rtty::error::category");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::success) == "success");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::client_error) == "client error");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::invalid_state) == "invalid state");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::not_a_tty) == "terminal is not a TTY");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::operation_aborted) == "operation aborted");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::operation_timeout) == "operation timeout");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::protocol_error) == "protocol error");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::server_error) == "server error");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::unexpected_message) == "unexpected message");
  assert(rstream::rtty::to_string(rstream::rtty::error::code::unknown_undefined_error) == "error is unknonw / undefined");
  assert(rstream::rtty::to_string(static_cast<rstream::rtty::error::code>(9999)) == "unknown error");

  auto code = rstream::rtty::error::make_error_code(static_cast<int>(rstream::rtty::error::code::server_error));
  assert(code);
  assert(code.message() == "server error");
  assert(code.category() == rstream::rtty::error::rstream_rtty_error_category());
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_environment_parsing_and_updates();
  check_protocol_type_parsing();
  check_identifier_and_username_parsing();
#ifndef _WIN32
  check_user_info_error_paths_do_not_report_success();
#endif
  check_webtty_uri_is_publishable_and_labelled();
  check_error_category_and_messages();
  return 0;
}
