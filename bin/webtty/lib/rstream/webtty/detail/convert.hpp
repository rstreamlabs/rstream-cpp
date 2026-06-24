// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/terminal.hpp>
#include <rstream/webtty/webtty.hpp>

namespace rstream {
namespace webtty {
namespace detail {

void convert(protocol::options& dst, const rstream::webtty::protobuf::Options& src);
void convert(rstream::webtty::protobuf::Options& dst, const protocol::options& src);
void convert(protocol::config& dst, const rstream::webtty::protobuf::Config& src);
void convert(rstream::webtty::protobuf::Config& dst, const protocol::config& src);
void convert(terminal_size& dst, const rstream::webtty::protobuf::TerminalSize& src);
void convert(rstream::webtty::protobuf::TerminalSize& dst, const terminal_size& src);

}  // namespace detail
}  // namespace webtty
}  // namespace rstream
