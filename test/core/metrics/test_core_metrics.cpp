// See LICENSE file in the project root for license information.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rstream/config.hpp>
#include <rstream/core/detail/metrics/common.hpp>
#include <rstream/core/metrics.hpp>

namespace metrics = rstream::core::detail::metrics;

class static_collectable : public rstream::core::metrics::collectable {
 public:
  using rstream::core::metrics::collectable::collect;

  void collect(metrics::metrics& out) override
  {
    out.push_back(metrics::metric{
        .m_name    = "rstream_test_collectable",
        .m_help    = "collectable help",
        .m_type    = metrics::metric::type::gauge,
        .m_samples = {
            metrics::sample{
                .m_value     = 12.5,
                .m_labels    = {{"source", "custom"}},
                .m_timestamp = std::chrono::system_clock::now(),
                .m_examplar  = {},
            },
        },
    });
  }
};

template <class T>
void compare(const T& current, const T& expected)
{
  if (current != expected) {
    std::stringstream stringstream;
    stringstream << "unexpected value [current: " << current << ", expected: " << expected << "]";
    throw std::runtime_error(stringstream.str());
  }
}

template <class F>
void expect_invalid_argument(F&& fn)
{
  bool thrown = false;
  try {
    fn();
  }
  catch (const std::invalid_argument&) {
    thrown = true;
  }
  compare(thrown, true);
}

void test_1()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto counter = rstream::core::metrics::counter("my_failures", "description of counter", {{"key1", "value1"}});
  compare(counter.name(), std::string("my_failures"));
  compare(counter.help(), std::string("description of counter"));
  compare(counter.value(), 0.0);
  counter.labels({{"key2", "value2"}}).increment(42.0);
  compare(counter.labels({{"key2", "value2"}}).value(), 42.0);
}

void test_metric_validation()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  expect_invalid_argument([] {
    metrics::check_metric_name("1bad");
  });
  expect_invalid_argument([] {
    metrics::check_label_name("bad-label");
  });
  expect_invalid_argument([] {
    metrics::check_label_name(metrics::labels{{"ok", "1"}, {"bad.label", "2"}});
  });
  expect_invalid_argument([] {
    metrics::check_label_name_overlap(metrics::labels{{"env", "prod"}}, metrics::labels{{"env", "staging"}});
  });
}

void test_registry_collects_registered_metrics()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto registry = std::make_shared<rstream::core::metrics::registry>();
  auto counter  = rstream::core::metrics::counter("rstream_test_counter", "counter help", {{"base", "root"}}, registry);
  counter.increment(2.0, {{"trace", "counter"}});
  counter.labels({{"method", "GET"}}).increment(3.0);

  auto gauge = rstream::core::metrics::gauge("rstream_test_gauge", "gauge help", {{"base", "root"}}, registry);
  gauge.decrement(0.25);
  gauge.set_current_time({{"trace", "current-time"}});
  gauge.labels({{"method", "POST"}}).set(9.5, {{"trace", "gauge"}});

  auto histogram = rstream::core::metrics::histogram(
      "rstream_test_histogram",
      "histogram help",
      {},
      registry,
      {1.0, 5.0, 10.0});
  histogram.observe(0.5);
  histogram.observe(7.0);
  histogram.labels({{"route", "api"}}).observe(99.0);

  auto summary = rstream::core::metrics::summary(
      "rstream_test_summary",
      "summary help",
      {},
      registry,
      {{0.5}, {0.9}},
      std::chrono::seconds(10),
      2);
  summary.observe(10.0);
  summary.observe(20.0);

  auto info = rstream::core::metrics::info("rstream_test_info", "info help", {{"component", "sdk"}}, registry);
  info.set({{"version", "1.2.3"}, {"channel", "dev"}});

  metrics::metrics collected;
  registry->collect(collected);
  compare(collected.size(), static_cast<std::size_t>(5));

  bool saw_counter = false;
  bool saw_gauge   = false;
  bool saw_hist    = false;
  bool saw_summary = false;
  bool saw_info    = false;
  for (const auto& metric : collected) {
    if (metric.m_name == "rstream_test_counter") {
      saw_counter = true;
      compare(metric.m_type == metrics::metric::type::counter, true);
      compare(metric.m_samples.size(), static_cast<std::size_t>(2));
    }
    if (metric.m_name == "rstream_test_gauge") {
      saw_gauge = true;
      compare(metric.m_type == metrics::metric::type::gauge, true);
      compare(metric.m_samples.size(), static_cast<std::size_t>(2));
      compare(metric.m_samples.front().m_examplar.at("trace"), std::string("current-time"));
    }
    if (metric.m_name == "rstream_test_histogram") {
      saw_hist = true;
      compare(metric.m_type == metrics::metric::type::histogram, true);
      compare(metric.m_samples.size(), static_cast<std::size_t>(2));
      const auto* value = boost::get<metrics::sample::histogram>(&metric.m_samples.front().m_value);
      compare(value != nullptr, true);
      compare(value->m_sample_count, static_cast<std::uint64_t>(2));
      compare(value->m_buckets.size(), static_cast<std::size_t>(4));
      compare(value->m_buckets.at(0).m_cumulative_count, static_cast<std::uint64_t>(1));
      compare(value->m_buckets.at(1).m_cumulative_count, static_cast<std::uint64_t>(1));
      compare(value->m_buckets.at(2).m_cumulative_count, static_cast<std::uint64_t>(2));
      compare(value->m_buckets.at(3).m_cumulative_count, static_cast<std::uint64_t>(2));
    }
    if (metric.m_name == "rstream_test_summary") {
      saw_summary = true;
      compare(metric.m_type == metrics::metric::type::summary, true);
      const auto* value = boost::get<metrics::sample::summary>(&metric.m_samples.front().m_value);
      compare(value != nullptr, true);
      compare(value->m_sample_count, static_cast<std::uint64_t>(2));
    }
    if (metric.m_name == "rstream_test_info") {
      saw_info = true;
      compare(metric.m_type == metrics::metric::type::info, true);
      compare(metric.m_samples.front().m_labels.at("component"), std::string("sdk"));
      compare(metric.m_samples.front().m_labels.at("version"), std::string("1.2.3"));
    }
  }
  compare(saw_counter && saw_gauge && saw_hist && saw_summary && saw_info, true);

  auto serialized = rstream::core::metrics::serializer()(collected);
  compare(serialized.find("# HELP rstream_test_counter counter help") != std::string::npos, true);
  compare(serialized.find("# TYPE rstream_test_histogram histogram") != std::string::npos, true);
  compare(serialized.find("rstream_test_info{channel=\"dev\",component=\"sdk\",version=\"1.2.3\"} 1") != std::string::npos, true);
}

void test_metric_child_label_rules()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto counter = rstream::core::metrics::counter("rstream_test_label_counter", "counter help", {{"base", "root"}});
  auto child   = counter.labels({{"route", "api"}});
  child.increment();
  metrics::labels child_labels;
  child.get_labels(child_labels);
  compare(child_labels.at("base"), std::string("root"));
  compare(child_labels.at("route"), std::string("api"));

  expect_invalid_argument([&] {
    counter.labels({{"base", "duplicate"}});
  });
  expect_invalid_argument([&] {
    child.labels({{"route", "duplicate"}});
  });
  expect_invalid_argument([] {
    (void)rstream::core::metrics::counter("bad-counter", "bad help");
  });
  expect_invalid_argument([] {
    (void)rstream::core::metrics::counter("valid_counter", "bad help", {{"bad-label", "x"}});
  });
}

void test_histogram_and_info_reject_invalid_inputs()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  expect_invalid_argument([] {
    (void)rstream::core::metrics::histogram("rstream_test_bad_histogram", "histogram help", {}, nullptr, {1.0, 1.0});
  });

  auto info = rstream::core::metrics::info("rstream_test_info_overlap", "info help", {{"component", "sdk"}});
  expect_invalid_argument([&] {
    info.set({{"component", "duplicate"}});
  });
}

void test_collectable_and_system_registry()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto collectable = std::make_shared<static_collectable>();
  auto direct      = collectable->collect();
  compare(direct.size(), static_cast<std::size_t>(1));
  compare(direct.front().m_name, std::string("rstream_test_collectable"));
  compare(boost::get<double>(direct.front().m_samples.front().m_value), 12.5);
  compare(direct.front().m_samples.front().m_labels.at("source"), std::string("custom"));

  auto registry = std::make_shared<rstream::core::metrics::registry>();
  registry->add_collectable(collectable);
  metrics::metrics collected;
  registry->collect(collected);
  compare(collected.size(), static_cast<std::size_t>(1));
  compare(collected.front().m_name, std::string("rstream_test_collectable"));

  auto default_registry = rstream::core::metrics::default_registry();
  compare(default_registry != nullptr, true);

  auto system_collector = rstream::core::metrics::system_collector();
  compare(system_collector != nullptr, true);
  auto system_metrics  = system_collector->collect();
  bool saw_system_info = false;
  for (const auto& metric : system_metrics) {
    if (metric.m_name == "system_info") {
      saw_system_info = true;
      compare(metric.m_type == metrics::metric::type::info, true);
      compare(metric.m_samples.empty(), false);
      compare(metric.m_samples.front().m_labels.count("sysname") == 1, true);
      compare(metric.m_samples.front().m_labels.count("machine") == 1, true);
    }
  }
  compare(saw_system_info, true);
}

void test_system_collector_is_thread_safe_singleton()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  std::vector<rstream::core::metrics::collectable::ptr> collectors(32);
  std::vector<std::thread> threads;
  threads.reserve(collectors.size());
  for (std::size_t i = 0; i < collectors.size(); ++i) {
    threads.emplace_back([&collectors, i]() {
      collectors[i] = rstream::core::metrics::system_collector();
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  for (const auto& collector : collectors) {
    compare(collector != nullptr, true);
    compare(collector == collectors.front(), true);
  }
}

void test_summary_concurrent_collection()
{
  std::cout << "running '" << RSTREAM_STRFUNC << "'" << std::endl;
  auto summary = rstream::core::metrics::summary(
      "rstream_test_concurrent_summary",
      "summary help",
      {},
      nullptr,
      {{0.5}, {0.9}},
      std::chrono::milliseconds(2),
      2);
  for (std::size_t i = 0; i < 499; ++i) {
    summary.observe(static_cast<double>(i));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::atomic<bool> start{false};
  std::vector<std::thread> threads;
  threads.reserve(16);
  for (std::size_t i = 0; i < 16; ++i) {
    threads.emplace_back([&summary, &start]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (std::size_t j = 0; j < 100; ++j) {
        (void)summary.get_sample();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }

  const auto sample = summary.get_sample();
  const auto* value = boost::get<metrics::sample::summary>(&sample.m_value);
  compare(value != nullptr, true);
  compare(value->m_sample_count, static_cast<std::uint64_t>(499));
}

void run()
{
  test_1();
  test_metric_validation();
  test_registry_collects_registered_metrics();
  test_metric_child_label_rules();
  test_histogram_and_info_reject_invalid_inputs();
  test_collectable_and_system_registry();
  test_system_collector_is_thread_safe_singleton();
  test_summary_concurrent_collection();
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  std::exception_ptr exception_ptr = nullptr;
  try {
    run();
  }
  catch (...) {
    exception_ptr = std::current_exception();
  }
  if (exception_ptr) {
    try {
      std::rethrow_exception(exception_ptr);
    }
    catch (const std::exception& exception) {
      std::cerr << "an error occurred: " << exception.what() << std::endl;
    }
  }
  return (exception_ptr ? -1 : 0);
}
