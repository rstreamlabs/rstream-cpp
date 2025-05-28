// See LICENSE file in the project root for license information.

#include <iostream>

#include <docopt.h>

#include <rstream/config.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/metrics.hpp>

static const char USAGE[] = R"(
rstream-example-core-metrics

usage:
    rstream-example-core-metrics [-v]
    rstream-example-core-metrics (-h|--help)
    rstream-example-core-metrics --version

options:
    -h --help       show this screen
    --version       show version
    -v --verbose    enable verbose mode
)";

const auto version = std::string("rstream-example-core-metrics ") + RSTREAM_VERSION;

int main(int argc, char** argv)
{
  auto args    = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  bool verbose = false;
  {
    auto it = args.find("--verbose");
    if (it != args.end() && it->second.asBool()) {
      verbose = true;
    }
  }
  if (verbose) {
    rstream::core::log::enable_ansicolor_stdout_mt();
  }
  {
    auto counter = rstream::core::metrics::counter("my_failures", "description of counter");
    counter.increment();     // increment by 1
    counter.increment(1.6);  // increment by given value
  }
  {
    auto gauge = rstream::core::metrics::gauge("my_inprogress_requests", "description of gauge");
    gauge.increment();    // increment by 1
    gauge.decrement(10);  // decrement by given value
    gauge.set(4.2);       // set to a given value
  }
  {
    auto summary = rstream::core::metrics::summary("request_latency_seconds", "description of summary");
    summary.observe(4.7);  // observe 4.7 (seconds in this case)
  }
  {
    auto histogram = rstream::core::metrics::histogram("request_latency_seconds", "description of histogram");
    histogram.observe(4.7);  // observe 4.7 (seconds in this case)
  }
  {
    auto info = rstream::core::metrics::info("my_build_version", "description of info");
    info.set({{"version", "1.2.3"}, {"buildhost", "foo@bar"}});
  }
  {
    auto counter = rstream::core::metrics::counter("my_requests_total", "HTTP Failures", {{"method", "endpoint"}});
    counter.labels({{"get", "/"}}).increment();
    counter.labels({{"post", "/submit"}}).increment();
  }
  {
    auto counter = rstream::core::metrics::counter("my_requests_total", "HTTP Failures", {{"method", "endpoint"}});
    counter.labels({{"get", "/"}}).increment({{"trace_id", "abc123"}});
    counter.labels({{"post", "/submit"}}).increment(1.0, {{"trace_id", "def456"}});
  }
  {
    auto histogram = rstream::core::metrics::histogram("request_latency_seconds", "description of histogram");
    histogram.observe(4.7, {{"trace_id", "abc123"}});
  }
  {
    auto registry = std::make_shared<rstream::core::metrics::registry>();
    rstream::core::metrics::counter("my_requests_total", "HTTP Failures", {{"method", "endpoint"}}, registry).increment();
  }
  {
    auto registry = std::make_shared<rstream::core::metrics::registry>();
    rstream::core::metrics::builder<rstream::core::metrics::counter>()
        .name("my_requests_total")
        .help("HTTP Failures")
        .labels({{"method", "endpoint"}})
        .registry(registry)
        .build()
        .increment();
  }
  {
    rstream::core::metrics::default_registry()->add_collectable(rstream::core::metrics::system_collector());
    std::cout << std::endl
              << rstream::core::metrics::serializer()(rstream::core::metrics::default_registry()->collectable::collect()) << std::endl;
  }
  return 0;
}
