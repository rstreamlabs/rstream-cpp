// See LICENSE file in the project root for license information.

#include "client.hpp"

namespace rstream {
namespace ncat {

client::client(const executor_type& executor, const config& config, const settings_client& settings)
    : io::io_object(executor)
{
  throw std::runtime_error("this feature is not implemented yet");
}

client::~client()
{
  // TODO : Implement
}

void client::async_run(async_run_completion_handler&& handler)
{
  // TODO : Implement
}

void client::cancel()
{
  // TODO : Implement
}

}  // namespace ncat
}  // namespace rstream
