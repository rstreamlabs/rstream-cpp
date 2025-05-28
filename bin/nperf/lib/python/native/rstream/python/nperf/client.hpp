// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <boost/system/error_code.hpp>
#include <boost/variant.hpp>

#include <rstream/nperf/client.hpp>

namespace rstream {
namespace python {
namespace nperf {

class client {
 public:
  using data = boost::variant<rstream::nperf::metrics, boost::system::error_code>;

  client(const rstream::nperf::client::config& config, const rstream::nperf::settings_client& settings, rstream::nperf::options options, unsigned int jobs);

  void start();

  void stop();

  data get();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace nperf
}  // namespace python
}  // namespace rstream
