// See LICENSE file in the project root for license information.

#include "convert.hpp"

namespace rstream {
namespace rtty {
namespace detail {

template <typename X, typename Y>
void convert(std::list<Y>& dst, const google::protobuf::RepeatedPtrField<X>& src);

template <typename X, typename Y>
void convert(google::protobuf::RepeatedPtrField<X>& dst, const std::list<Y>& src);

static void convert(protocol::environment& dst, const rstream::rtty::protobuf::Environment& src);

static void convert(rstream::rtty::protobuf::Environment& dst, const protocol::environment& src);

static void convert(protocol::identifier& dst, const rstream::rtty::protobuf::Username& src);

static void convert(rstream::rtty::protobuf::Username& dst, const protocol::identifier& src);

static void convert(std::string& dst, const std::string& src);

void convert(protocol::options& dst, const rstream::rtty::protobuf::Options& src)
{
  dst = (protocol::options){
      .m_interactive    = src.interactive(),
      .m_allocate_tty   = src.allocate_tty(),
      .m_send_heartbeat = src.send_heartbeat(),
  };
}

void convert(rstream::rtty::protobuf::Options& dst, const protocol::options& src)
{
  dst.set_interactive(src.m_interactive);
  dst.set_allocate_tty(src.m_allocate_tty);
  dst.set_send_heartbeat(src.m_send_heartbeat);
}

void convert(protocol::environment& dst, const rstream::rtty::protobuf::Environment& src)
{
  dst = (protocol::environment){
      .m_key   = src.key(),
      .m_value = src.value(),
  };
}

void convert(rstream::rtty::protobuf::Environment& dst, const protocol::environment& src)
{
  dst.set_key(src.m_key);
  dst.set_value(src.m_value);
}

void convert(protocol::identifier& dst, const rstream::rtty::protobuf::Username& src)
{
  auto type = src.payload_case();
  if (type == rstream::rtty::protobuf::Username::PayloadCase::kName) {
    dst = src.name();
  }
  else if (type == rstream::rtty::protobuf::Username::PayloadCase::kId) {
    dst = src.id();
  }
}

void convert(rstream::rtty::protobuf::Username& dst, const protocol::identifier& src)
{
  if (src.type() == typeid(std::string)) {
    dst.set_name(boost::get<std::string>(src));
  }
  else if (src.type() == typeid(std::uint32_t)) {
    dst.set_id(boost::get<std::uint32_t>(src));
  }
}

void convert(protocol::config& dst, const rstream::rtty::protobuf::Config& src)
{
  convert(dst.m_options, src.options());
  convert(dst.m_cmd_args, src.cmd_args());
  convert(dst.m_env_vars, src.env_vars());
  if (src.has_workdir()) {
    dst.m_workdir = src.workdir().value();
  }
  if (src.has_username()) {
    protocol::identifier identifier;
    convert(identifier, src.username());
    dst.m_username = identifier;
  }
}

void convert(rstream::rtty::protobuf::Config& dst, const protocol::config& src)
{
  convert(*dst.mutable_options(), src.m_options);
  convert(*dst.mutable_cmd_args(), src.m_cmd_args);
  convert(*dst.mutable_env_vars(), src.m_env_vars);
  if (src.m_workdir) {
    dst.mutable_workdir()->set_value(src.m_workdir.get());
  }
  if (src.m_username) {
    convert(*dst.mutable_username(), src.m_username.get());
  }
}

void convert(terminal_size& dst, const rstream::rtty::protobuf::TerminalSize& src)
{
  dst = (terminal_size){
      .m_row    = static_cast<unsigned short>(src.row()),
      .m_col    = static_cast<unsigned short>(src.col()),
      .m_xpixel = static_cast<unsigned short>(src.xpixel()),
      .m_ypixel = static_cast<unsigned short>(src.ypixel()),
  };
}

void convert(rstream::rtty::protobuf::TerminalSize& dst, const terminal_size& src)
{
  dst.set_row(src.m_row);
  dst.set_col(src.m_col);
  dst.set_xpixel(src.m_xpixel);
  dst.set_ypixel(src.m_ypixel);
}

void convert(std::string& dst, const std::string& src)
{
  dst = src;
}

template <typename X, typename Y>
void convert(std::list<Y>& dst, const google::protobuf::RepeatedPtrField<X>& src)
{
  dst.clear();
  for (const auto& value : src) {
    Y tmp;
    convert(tmp, value);
    dst.push_back(tmp);
  }
}

template <typename X, typename Y>
void convert(google::protobuf::RepeatedPtrField<X>& dst, const std::list<Y>& src)
{
  dst.Clear();
  for (const auto& value : src) {
    X tmp;
    convert(tmp, value);
    *dst.Add() = tmp;
  }
}

}  // namespace detail
}  // namespace rtty
}  // namespace rstream
