// See LICENSE file in the project root for license information.

#include "stream_socket_base.hpp"

namespace rstream {
namespace io {

stream_socket_base_interface::stream_socket_base_interface(const io_object::executor_type& executor)
    : m_executor(executor)
{
}

stream_socket_base_interface::async_write_some_op<boost::asio::const_buffer>::async_write_some_op(stream_socket_base_interface& socket)
    : m_socket(socket)
{
}

stream_socket_base_interface::async_write_some_op<stream_socket_base_interface::const_buffer_sequence_type>::async_write_some_op(stream_socket_base_interface& socket)
    : m_socket(socket)
{
}

stream_socket_base_interface::async_read_some_op<boost::asio::mutable_buffer>::async_read_some_op(stream_socket_base_interface& socket)
    : m_socket(socket)
{
}

stream_socket_base_interface::async_read_some_op<stream_socket_base_interface::mutable_buffer_sequence_type>::async_read_some_op(stream_socket_base_interface& socket)
    : m_socket(socket)
{
}

}  // namespace io
}  // namespace rstream
