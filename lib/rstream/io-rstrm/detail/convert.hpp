// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/ip/address.hpp>

#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/protobuf/messages.pb.h>

namespace rstream {
namespace io_rstrm {
namespace detail {

void convert(protobuf::IpAddress& dst, const boost::asio::ip::address& src);
void convert(boost::asio::ip::address& dst, const protobuf::IpAddress& src);

void convert(protobuf::ClientDetails& dst, const client_details& src);
void convert(client_details& dst, const protobuf::ClientDetails& src);

void convert(protobuf::TunnelProperties& dst, const tunnel_properties& src);
void convert(tunnel_properties& dst, const protobuf::TunnelProperties& src);

}  // namespace detail
}  // namespace io_rstrm
}  // namespace rstream
