// See LICENSE file in the project root for license information.

#include "log.hpp"

#include <cctype>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <spdlog/details/fmt_helper.h>
#include <spdlog/sinks/dist_sink.h>

#include <rstream/config.hpp>

namespace rstream {
namespace core {

namespace log {

namespace colors RSTREAM_GNUC_INTERNAL {

// foreground colors
static const spdlog::string_view_t cyan       = "\033[36m";
static const spdlog::string_view_t dark_gray  = "\033[90m";
static const spdlog::string_view_t green      = "\033[32m";
static const spdlog::string_view_t init       = "\033[39m";
static const spdlog::string_view_t light_gray = "\033[37m";

/// bold colors
static const spdlog::string_view_t bold_on_red = "\033[1m\033[41m";
static const spdlog::string_view_t red_bold    = "\033[31m\033[1m";
static const spdlog::string_view_t yellow_bold = "\033[33m\033[1m";

static const spdlog::string_view_t reset = "\033[m";

// log levels
static const std::unordered_map<spdlog::level::level_enum, spdlog::string_view_t> log_levels = {
    {spdlog::level::trace, light_gray},
    {spdlog::level::debug, init},
    {spdlog::level::info, green},
    {spdlog::level::warn, yellow_bold},
    {spdlog::level::err, red_bold},
    {spdlog::level::critical, bold_on_red},
    {spdlog::level::off, reset}};

}  // namespace colors RSTREAM_GNUC_INTERNAL

static std::string format_logger_name(const std::list<std::string>& name);

class RSTREAM_GNUC_INTERNAL registry {
 public:
  registry();
  static registry& instance();
  void subscribe(const spdlog::sink_ptr sink);
  std::shared_ptr<logger::lower_level> make_logger(const std::string& name);

 private:
  std::shared_ptr<spdlog::sinks::dist_sink_mt> m_dist_sink_ptr;
};

class RSTREAM_GNUC_INTERNAL default_sink_mt : public spdlog::sinks::sink {
 public:
  default_sink_mt(std::ostream& ostream);
  virtual ~default_sink_mt() override = default;
  void log(const std::string& msg);

 private:
  void log(const spdlog::details::log_msg& msg) override;
  void flush() override;
  void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override;
  void set_pattern(const std::string& pattern) override;
  std::mutex m_mutex;
  std::ostream& m_ostream;
  std::unique_ptr<spdlog::formatter> m_formatter;
};

class RSTREAM_GNUC_INTERNAL default_formatter : public spdlog::formatter {
 public:
  default_formatter(bool color);
  void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dst) override;
  std::unique_ptr<spdlog::formatter> clone() const override;

 private:
  bool m_color;
};

class RSTREAM_GNUC_INTERNAL json_formatter : public spdlog::formatter {
 public:
  json_formatter(int indent);
  void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dst) override;
  std::unique_ptr<spdlog::formatter> clone() const override;

 private:
  int m_indent;
};

default_formatter::default_formatter(bool color)
    : m_color(color)
{
}

void default_formatter::format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dst)
{
  const auto tm             = spdlog::details::os::gmtime(spdlog::log_clock::to_time_t(msg.time));
  const auto milliseconds   = spdlog::details::fmt_helper::time_fraction<std::chrono::milliseconds>(msg.time);
  const auto level_str_view = spdlog::level::to_string_view(msg.level);
  std::string level_str(level_str_view.begin(), level_str_view.end());
  std::transform(level_str.begin(), level_str.end(), level_str.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  fmt::format_to(std::back_inserter(dst), "{}{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}{} {}{:>8}{} {}{:<45}{} {}-{} {}{}{}",
                 m_color ? colors::dark_gray : "",
                 tm.tm_year + 1900,
                 tm.tm_mon + 1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec,
                 static_cast<std::uint32_t>(milliseconds.count()),
                 m_color ? colors::reset : "",
                 m_color ? colors::log_levels.at(msg.level) : "",
                 level_str,
                 m_color ? colors::reset : "",
                 m_color ? colors::cyan : "",
                 msg.logger_name,
                 m_color ? colors::reset : "",
                 m_color ? colors::dark_gray : "",
                 m_color ? colors::reset : "",
                 m_color ? colors::log_levels.at(msg.level) : "",
                 msg.payload,
                 m_color ? colors::reset : "");
}

std::unique_ptr<spdlog::formatter> default_formatter::clone() const
{
  return std::make_unique<default_formatter>(m_color);
}

json_formatter::json_formatter(int indent)
    : m_indent(indent)
{
}

void json_formatter::format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dst)
{
  const auto level_str_view = spdlog::level::to_string_view(msg.level);
  std::string level_str(level_str_view.begin(), level_str_view.end());
  nlohmann::json json;
  json["level"]     = level_str;
  json["logger"]    = std::string(msg.logger_name.begin(), msg.logger_name.end());
  json["message"]   = (std::string)fmt::to_string(msg.payload);
  json["thread"]    = msg.thread_id;
  json["timestamp"] = format_timestamp(msg.time);
  json["type"]      = "log";
  fmt::format_to(std::back_inserter(dst), "{}", json.dump(m_indent));
}

std::unique_ptr<spdlog::formatter> json_formatter::clone() const
{
  return std::make_unique<json_formatter>(m_indent);
}

registry::registry()
    : m_dist_sink_ptr(std::make_shared<spdlog::sinks::dist_sink_mt>())
{
}

registry& registry::instance()
{
  static registry instance;
  return instance;
}

void registry::subscribe(const spdlog::sink_ptr sink)
{
  m_dist_sink_ptr->add_sink(sink);
}

std::shared_ptr<logger::lower_level> registry::make_logger(const std::string& name)
{
  return std::make_shared<logger::lower_level>(name, m_dist_sink_ptr);
}

logger::logger()
    : logger(std::string())
{
}

logger::logger(const std::list<std::string>& name)
    : logger(format_logger_name(name))
{
}

logger::logger(const std::string& name)
    : m_lower_level(registry::instance().make_logger(name))

{
  m_lower_level->set_level(spdlog::level::trace);
}

std::shared_ptr<logger::lower_level> logger::operator->() const
{
  return m_lower_level;
}

void subscribe(const spdlog::sink_ptr sink)
{
  registry::instance().subscribe(sink);
}

std::string format_logger_name(const std::list<std::string>& name)
{
  std::string result;
  for (auto it = name.begin(); it != name.end(); ++it) {
    result += (std::distance(name.begin(), it) != 0 ? "." : "") + *it;
  }
  return result;
}

str_sink enable_ansicolor_stdout_mt(bool color)
{
  auto sink_ptr  = std::make_shared<default_sink_mt>(std::cout);
  auto formatter = std::make_unique<default_formatter>(color);
  static_cast<std::shared_ptr<spdlog::sinks::sink>>(sink_ptr)->set_formatter(std::move(formatter));
  subscribe((spdlog::sink_ptr)sink_ptr);
  return [sink_ptr](const std::string& msg) { sink_ptr->log(msg); };
}

str_sink enable_ansicolor_stderr_mt(bool color)
{
  auto sink_ptr  = std::make_shared<default_sink_mt>(std::cerr);
  auto formatter = std::make_unique<default_formatter>(color);
  static_cast<std::shared_ptr<spdlog::sinks::sink>>(sink_ptr)->set_formatter(std::move(formatter));
  subscribe((spdlog::sink_ptr)sink_ptr);
  return [sink_ptr](const std::string& msg) { sink_ptr->log(msg); };
}

str_sink enable_json_stdout_mt(bool pretty)
{
  auto sink_ptr  = std::make_shared<default_sink_mt>(std::cout);
  auto formatter = std::make_unique<json_formatter>(pretty ? 2 : -1);
  static_cast<std::shared_ptr<spdlog::sinks::sink>>(sink_ptr)->set_formatter(std::move(formatter));
  subscribe((spdlog::sink_ptr)sink_ptr);
  return [sink_ptr](const std::string& msg) { sink_ptr->log(msg); };
}

str_sink enable_json_stderr_mt(bool pretty)
{
  auto sink_ptr  = std::make_shared<default_sink_mt>(std::cerr);
  auto formatter = std::make_unique<json_formatter>(pretty ? 2 : -1);
  static_cast<std::shared_ptr<spdlog::sinks::sink>>(sink_ptr)->set_formatter(std::move(formatter));
  subscribe((spdlog::sink_ptr)sink_ptr);
  return [sink_ptr](const std::string& msg) { sink_ptr->log(msg); };
}

default_sink_mt::default_sink_mt(std::ostream& ostream)
    : m_ostream(ostream)

{
}

void default_sink_mt::log(const std::string& msg)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_ostream << msg << std::endl;
  m_ostream.flush();
}

void default_sink_mt::log(const spdlog::details::log_msg& msg)
{
  spdlog::memory_buf_t formatted;
  m_formatter->format(msg, formatted);
  log(std::string(formatted.data(), formatted.size()));
}

void default_sink_mt::flush()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_ostream.flush();
}

void default_sink_mt::set_formatter(std::unique_ptr<spdlog::formatter> formatter)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_formatter = std::move(formatter);
}

void default_sink_mt::set_pattern(const std::string& pattern)
{
  set_formatter(std::make_unique<spdlog::pattern_formatter>(pattern));
}

}  // namespace log

std::string format_timestamp(unsigned long milliseconds, std::time_t time_t)
{
  std::tm utc{};
#ifdef _WIN32
  if (gmtime_s(&utc, &time_t) != 0) {
    return "";
  }
#else
  if (gmtime_r(&time_t, &utc) == nullptr) {
    return "";
  }
#endif
  std::stringstream stringstream;
  stringstream << std::put_time(&utc, "%FT%T") << '.' << std::setfill('0') << std::setw(3) << milliseconds << 'Z';
  return stringstream.str();
}

const logger& default_logger()
{
  static logger instance;
  return instance;
}

}  // namespace core
}  // namespace rstream
