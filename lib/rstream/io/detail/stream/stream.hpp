// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/io/acceptor_base.hpp>
#include <rstream/io/resolver_base.hpp>
#include <rstream/io/stream_socket_base.hpp>

#include "endpoint_base.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

enum class socket_mode { client,
                         server };

class element;

class endpoint;

class stream_socket;

using element_interface = element;

using element_ptr = std::shared_ptr<element_interface>;

using element_const_ptr = std::shared_ptr<const element_interface>;

using endpoint_interface = endpoint_base;

using endpoint_ptr = std::shared_ptr<endpoint_interface>;

using endpoint_const_ptr = std::shared_ptr<const endpoint_interface>;

using resolver_interface = resolver_base<endpoint>;

using resolver_ptr = std::shared_ptr<resolver_interface>;

using resolver_const_ptr = std::shared_ptr<const resolver_interface>;

using stream_socket_interface = stream_socket_base<endpoint>;

using stream_socket_ptr = std::shared_ptr<stream_socket_interface>;

using stream_socket_const_ptr = std::shared_ptr<const stream_socket_interface>;

using acceptor_interface = acceptor_base<endpoint, stream_socket>;

using acceptor_ptr = std::shared_ptr<acceptor_interface>;

using acceptor_const_ptr = std::shared_ptr<const acceptor_interface>;

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
