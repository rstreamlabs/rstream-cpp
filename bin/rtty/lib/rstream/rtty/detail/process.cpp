// See LICENSE file in the project root for license information.

#include "process.hpp"

namespace rstream {
namespace rtty {
namespace detail {

namespace process {

namespace pty {
handler::handler(std::shared_ptr<stream::pty> stream_ptr)
    : m_stream_ptr(stream_ptr)
{
}
}  // namespace pty

#ifndef _WIN32

namespace uid {
handler::handler(const protocol::user_info& user_info)
    : m_user_info(user_info)
{
}
}  // namespace uid

#endif

}  // namespace process

}  // namespace detail
}  // namespace rtty
}  // namespace rstream
