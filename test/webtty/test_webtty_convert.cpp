// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdint>
#include <string>

#include <rstream/webtty/detail/convert.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>

namespace detail   = rstream::webtty::detail;
namespace protocol = rstream::webtty::protocol;
namespace protobuf = rstream::webtty::protobuf;

static void check_config_roundtrip_preserves_payload()
{
  protocol::config src = {
      .m_protocol_type = protocol::type::plain,
      .m_options       = {
                .m_interactive    = true,
                .m_allocate_tty   = false,
                .m_send_heartbeat = true,
      },
      .m_env_vars = {
          {.m_key = "A", .m_value = "1"},
          {.m_key = "B", .m_value = "2"},
      },
      .m_cmd_args = {"/bin/sh", "-c", "printf ok"},
      .m_workdir  = std::string("/tmp"),
      .m_username = protocol::identifier(std::string("operator")),
  };

  protobuf::Config encoded;
  detail::convert(encoded, src);
  assert(encoded.options().interactive());
  assert(!encoded.options().allocate_tty());
  assert(encoded.options().send_heartbeat());
  assert(encoded.env_vars_size() == 2);
  assert(encoded.env_vars(0).key() == "A");
  assert(encoded.env_vars(0).value() == "1");
  assert(encoded.cmd_args_size() == 3);
  assert(encoded.cmd_args(2) == "printf ok");
  assert(encoded.has_workdir());
  assert(encoded.workdir().value() == "/tmp");
  assert(encoded.has_username());
  assert(encoded.username().payload_case() == protobuf::Username::PayloadCase::kName);
  assert(encoded.username().name() == "operator");

  protocol::config decoded;
  decoded.m_workdir  = std::string("stale-workdir");
  decoded.m_username = protocol::identifier(std::string("stale-user"));
  detail::convert(decoded, encoded);
  assert(decoded.m_options.m_interactive);
  assert(!decoded.m_options.m_allocate_tty);
  assert(decoded.m_options.m_send_heartbeat);
  assert(decoded.m_env_vars.size() == 2);
  assert(decoded.m_cmd_args.size() == 3);
  assert(decoded.m_workdir);
  assert(decoded.m_workdir.value() == "/tmp");
  assert(decoded.m_username);
  assert(decoded.m_username->type() == typeid(std::string));
  assert(boost::get<std::string>(*decoded.m_username) == "operator");
}

static void check_numeric_username_roundtrip()
{
  protocol::config src;
  src.m_options = {
      .m_interactive    = false,
      .m_allocate_tty   = true,
      .m_send_heartbeat = false,
  };
  src.m_username = protocol::identifier(static_cast<std::uint32_t>(501));

  protobuf::Config encoded;
  detail::convert(encoded, src);
  assert(encoded.has_username());
  assert(encoded.username().payload_case() == protobuf::Username::PayloadCase::kId);
  assert(encoded.username().id() == 501);

  protocol::config decoded;
  detail::convert(decoded, encoded);
  assert(decoded.m_username);
  assert(decoded.m_username->type() == typeid(std::uint32_t));
  assert(boost::get<std::uint32_t>(*decoded.m_username) == 501);
}

static void check_optional_fields_are_cleared_when_reusing_config_objects()
{
  protobuf::Config encoded_with_optionals;
  encoded_with_optionals.mutable_workdir()->set_value("/tmp");
  encoded_with_optionals.mutable_username()->set_name("operator");

  protocol::config decoded;
  detail::convert(decoded, encoded_with_optionals);
  assert(decoded.m_workdir);
  assert(decoded.m_username);

  protobuf::Config encoded_without_optionals;
  detail::convert(decoded, encoded_without_optionals);
  assert(!decoded.m_workdir);
  assert(!decoded.m_username);

  protocol::config src_without_optionals;
  src_without_optionals.m_options = {
      .m_interactive    = false,
      .m_allocate_tty   = false,
      .m_send_heartbeat = false,
  };

  protobuf::Config reused_encoded;
  reused_encoded.mutable_workdir()->set_value("stale-workdir");
  reused_encoded.mutable_username()->set_name("stale-user");
  detail::convert(reused_encoded, src_without_optionals);
  assert(!reused_encoded.has_workdir());
  assert(!reused_encoded.has_username());
}

static void check_terminal_size_roundtrip()
{
  rstream::webtty::terminal_size src = {
      .m_row    = 24,
      .m_col    = 132,
      .m_xpixel = 800,
      .m_ypixel = 600,
  };
  protobuf::TerminalSize encoded;
  detail::convert(encoded, src);
  assert(encoded.row() == 24);
  assert(encoded.col() == 132);
  assert(encoded.xpixel() == 800);
  assert(encoded.ypixel() == 600);

  rstream::webtty::terminal_size decoded = {};
  detail::convert(decoded, encoded);
  assert(decoded.m_row == src.m_row);
  assert(decoded.m_col == src.m_col);
  assert(decoded.m_xpixel == src.m_xpixel);
  assert(decoded.m_ypixel == src.m_ypixel);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_config_roundtrip_preserves_payload();
  check_numeric_username_roundtrip();
  check_optional_fields_are_cleared_when_reusing_config_objects();
  check_terminal_size_roundtrip();
  return 0;
}
