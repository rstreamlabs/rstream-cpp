// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/io-rstrm/acceptor.hpp>
#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/socket.hpp>
#include <rstream/io/detail/stream/acceptor_impl.hpp>

#include "stream_socket.hpp"

namespace rstream {
namespace plugin {
namespace io_rstrm {

using acceptor = rstream::io::detail::stream::acceptor_impl<rstream::io_rstrm::acceptor, rstream::io_rstrm::socket, rstream::io_rstrm::endpoint>;

}
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_rstrm::acceptor::open_internal(const rstream::io_rstrm::endpoint& endpoint, boost::system::error_code& error_code);

template <>
void rstream::plugin::io_rstrm::acceptor::configure_internal(const rstream::io_rstrm::endpoint& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);
