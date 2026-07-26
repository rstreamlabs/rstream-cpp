// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace rstream {
namespace core {

namespace log {

class registry;

class logger {
  friend class registry;

 public:
  using lower_level = spdlog::logger;
  logger();
  logger(const std::list<std::string>& name);
  logger(const std::string& name);
  std::shared_ptr<lower_level> operator->() const;

 private:
  std::shared_ptr<lower_level> m_lower_level;
};

void subscribe(const spdlog::sink_ptr sink);
template <class T, class... Args>
void subscribe(Args&&... args)
{
  subscribe((spdlog::sink_ptr)std::make_shared<T>(args...));
}

using str_sink = std::function<void(const std::string&)>;

str_sink enable_ansicolor_stdout_mt(bool color = true);

str_sink enable_ansicolor_stderr_mt(bool color = true);

str_sink enable_json_stdout_mt(bool pretty = false);

str_sink enable_json_stderr_mt(bool pretty = false);

}  // namespace log

std::string format_timestamp(unsigned long milliseconds, std::time_t time_t);

template <class Clock>
std::string format_timestamp(const std::chrono::time_point<Clock>& time_point);

using logger = core::log::logger;

const logger& default_logger();

template <class Clock>
std::string format_timestamp(const std::chrono::time_point<Clock>& time_point)
{
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count() % 1000;
  const auto time_t       = Clock::to_time_t(time_point);
  return format_timestamp(milliseconds, time_t);
}

}  // namespace core
}  // namespace rstream
