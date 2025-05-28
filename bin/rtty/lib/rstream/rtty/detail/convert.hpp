// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/rtty/protobuf/messages.pb.h>
#include <rstream/rtty/rtty.hpp>
#include <rstream/rtty/terminal.hpp>

namespace rstream {
namespace rtty {
namespace detail {

void convert(protocol::options& dst, const rstream::rtty::protobuf::Options& src);
void convert(rstream::rtty::protobuf::Options& dst, const protocol::options& src);
void convert(protocol::config& dst, const rstream::rtty::protobuf::Config& src);
void convert(rstream::rtty::protobuf::Config& dst, const protocol::config& src);
void convert(terminal_size& dst, const rstream::rtty::protobuf::TerminalSize& src);
void convert(rstream::rtty::protobuf::TerminalSize& dst, const terminal_size& src);

}  // namespace detail
}  // namespace rtty
}  // namespace rstream
