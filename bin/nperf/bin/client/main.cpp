// See LICENSE file in the project root for license information.

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/format.hpp>

#include <docopt.h>
#include <nlohmann/json.hpp>

#include <rstream/config.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/nperf/client.hpp>
#include <rstream/nperf/nperf.hpp>

static const char USAGE[] = R"(
rstream-nperf-client - https://rstream.io/ - network performance testing utility using rstream primitives

this program is distributed with the rstream C++ tools. See https://rstream.io/docs/integrations/cpp-sdk and https://github.com/rstreamlabs/rstream-cpp.

usage:
  rstream-nperf-client [options] [ping|download|upload]
  rstream-nperf-client (-h|--help)
  rstream-nperf-client --version

example:
  rstream-nperf-client
  rstream-nperf-client download -v --format json
  rstream-nperf-client download --progress 0 --format json-pretty
  rstream-nperf-client ping -c 0 -s 1 --period 250 -r --max-ping 10 --progress 0 --format json

options:
  -h --help             show this screen
  --version             show version
  -v --verbose          enable verbose mode
  -f --format=ARG       output format (see below for valid formats)
  -i --infos            print extra informations
  --precision=ARG       number of decimals to use (3-10) [default: 3]
  --uri=ARG             URI [default: 127.0.0.1:6003]
  -s --sessions=ARG     number of sessions to run simultaneously [default: 1]
  -j --jobs=ARG         number of threads to run simultaneously (0 = auto) [default: 0]
  -t --timeout=ARG      maximum amount of time in milliseconds that command will run [default: 10000]
  -c --count=ARG        executes command count times (0 = indefinitely) [default: 1]
  -r --retry            retry to execute command in case of error
  --period=ARG          time in milliseconds that nperf waits between successive executions attempts (0 = do not wait) [default: 250]
  --progress=ARG        time in milliseconds that nperf will refresh progress (0 = do not display progress) [default: 250]
  --max-data=ARG        maximum amount in bytes of data transfered per execution (download & upload) (0 = infinite) [default: 0]
  --max-ping=ARG        maximum number of ping sent per execution (ping) (0 = infinite) [default: 50]
  --buffer-size=ARG     buffer size in bytes expressed as a power of 2 (download & upload) [default: 22]
  --ping-size=ARG       ping size in bytes expressed as a power of 2 (ping) [default: 4]
  --protocol=ARG        protocol to use [default: websocket]

valid output formats: human, human-pretty, json, json-pretty
valid protocols: websocket, plain
)";

const auto version = std::string("rstream-nperf-client ") + RSTREAM_VERSION;

static rstream::core::log::str_sink g_str_sink = nullptr;

enum class format {
  human,
  human_pretty,
  json,
  json_pretty,
};

static void parse_format(format& dst, const std::string& src);

double compute_speed_kbits(double measured_bytes, double elapsed_time_ms);

std::string format_time_us(double time, unsigned int precision = 3, int width = 0);

std::string format_speed_kbps(double speed, unsigned int precision = 3, int width = 0);

std::string format_measured_data_kb(double data, unsigned int precision = 3, int width = 0);

std::string format_speed_kbps(double measured_bytes, double elapsed_time_ms, unsigned int precision = 3, int width = 0);

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::sample, unsigned int>& speed);

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::speed, unsigned int>& speed);

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::metrics, unsigned int>& metrics);

static void log(const nlohmann::json& json, bool pretty);

static void log(const rstream::nperf::metrics& metrics, bool pretty, bool display_progress, unsigned int precision, bool extra_infos);

static void log(const rstream::nperf::metrics& metrics, format format, bool display_progress, unsigned int precision, bool extra_infos);

int run(int argc, char** argv)
{
  auto args    = docopt::docopt(USAGE, {argv + 1, argv + argc}, true, version);
  auto verbose = false;
  {
    auto arg = args.at("--verbose");
    if (arg) {
      verbose = arg.asBool();
    }
  }
#ifdef _WIN32
  auto is_tty = _isatty(_fileno(stdout));
#else
  auto is_tty = isatty(STDOUT_FILENO);
#endif
  auto format = (!verbose && is_tty) ? format::human_pretty : format::human;
  {
    auto arg = args.at("--format");
    if (arg) {
      parse_format(format, arg.asString());
    }
  }
  auto extra_infos = false;
  {
    auto arg = args.at("--infos");
    if (arg) {
      extra_infos = arg.asBool();
    }
  }
  if (verbose && format == format::human_pretty) {
    throw std::runtime_error("verbose mode cannot be used in conjonction with 'human-pretty' output format");
  }
  if (verbose) {
    if (format == format::human) {
      g_str_sink = rstream::core::log::enable_ansicolor_stdout_mt();
    }
    else {
      g_str_sink = rstream::core::log::enable_json_stdout_mt(format == format::json_pretty ? true : false);
    }
  }
  rstream::core::default_logger()->info(version);
  rstream::nperf::options options = 0;
  {
    auto arg = args.at("ping");
    if (arg && arg.asBool()) {
      options |= rstream::nperf::option::ping;
    }
  }
  {
    auto arg = args.at("download");
    if (arg && arg.asBool()) {
      options |= rstream::nperf::option::download;
    }
  }
  {
    auto arg = args.at("upload");
    if (arg && arg.asBool()) {
      options |= rstream::nperf::option::upload;
    }
  }
  if (options == 0) {
    options |= rstream::nperf::option::ping;
    options |= rstream::nperf::option::download;
    options |= rstream::nperf::option::upload;
  }
  unsigned int precision = 3;
  {
    auto arg = args.at("--precision");
    if (arg) {
      precision = std::stoul(arg.asString());
    }
  }
  precision = std::clamp(precision, (unsigned int)3, (unsigned int)10);
  auto jobs = std::max((long)0, args.at("--jobs").asLong());
  if (jobs == 0) {
    jobs = std::thread::hardware_concurrency();
  }
  if (format == format::human || format == format::human_pretty) {
    std::cout << "\n\tnperf by rstream - https://rstream.io/\n"
              << std::endl;
  }
  boost::asio::io_context io_context(jobs);
  boost::asio::signal_set signal_set(io_context, SIGINT, SIGTERM);
  rstream::nperf::protocol protocol;
  rstream::nperf::parse_protocol(protocol, args.at("--protocol").asString());
  rstream::nperf::client::config config = {
      .m_address = args.at("--uri").asString(),
  };
  rstream::nperf::settings_client settings = {
      .m_common = {
          .m_buffer_size            = static_cast<std::uint32_t>(std::max((long)0, (long)std::pow(2, args.at("--buffer-size").asLong()))),
          .m_timeouts_max_time_ms   = static_cast<std::uint32_t>(std::max((long)0, args.at("--timeout").asLong())),
          .m_timeouts_open_close_ms = 10000,
          .m_protocol               = protocol,
      },
      .m_execution_count   = static_cast<std::uint32_t>(std::max((long)0, args.at("--count").asLong())),
      .m_max_ping          = static_cast<std::uint32_t>(std::max((long)0, args.at("--max-ping").asLong())),
      .m_period_metrics_ms = static_cast<std::uint32_t>(std::max((long)0, args.at("--progress").asLong())),
      .m_period_ms         = static_cast<std::uint32_t>(std::max((long)0, args.at("--period").asLong())),
      .m_ping_buffer_size  = static_cast<std::uint32_t>(std::max((long)0, (long)std::pow(2, args.at("--ping-size").asLong()))),
      .m_sessions          = static_cast<std::uint32_t>(std::max((long)1, args.at("--sessions").asLong())),
      .m_max_data_bytes    = static_cast<std::uint64_t>(std::max((long)0, args.at("--max-data").asLong())),
      .m_retry             = args.at("--retry").asBool(),
  };
  auto progress = settings.m_period_metrics_ms != 0;
  rstream::nperf::client client(io_context.get_executor(), config, settings);
  signal_set.async_wait([&client](const boost::system::error_code&, int) {
    client.cancel();
  });
  boost::system::error_code result;
  auto on_complete = [&signal_set, &result](const boost::system::error_code& error_code) {
    result = error_code;
    signal_set.cancel();
  };
  rstream::nperf::client::callbacks callbacks = {
      .m_on_metrics_cb = (rstream::nperf::on_metrics_cb)std::bind((void (*)(const rstream::nperf::metrics&, enum format, bool, unsigned int, bool)) & log, std::placeholders::_1, format, progress, precision, extra_infos),
  };
  client.async_run(options, callbacks, on_complete);
  std::vector<std::thread> threads;
  if (jobs > 1) {
    auto n = jobs - 1;
    threads.reserve(n);
    for (unsigned int i = 0; i < n; ++i) {
      threads.emplace_back(std::bind((boost::asio::io_context::count_type(boost::asio::io_context::*)()) & boost::asio::io_context::run, &io_context));
    }
  }
  io_context.run();
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  if (result) {
    if (format == format::human_pretty) {
      std::cout << "\33[2K\r";
    }
    std::cerr << result.message() << std::endl;
  }
  return result ? -1 : 0;
}

int main(int argc, char** argv)
{
  std::exception_ptr error;
  try {
    return run(argc, argv);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::cerr << "a fatal error occurred: " << rstream::core::throwable::message(error) << std::endl;
  }
  return error ? -1 : 0;
}

void parse_format(format& dst, const std::string& src)
{
  if (src == "human") {
    dst = format::human;
  }
  else if (src == "human-pretty") {
    dst = format::human_pretty;
  }
  else if (src == "json") {
    dst = format::json;
  }
  else if (src == "json-pretty") {
    dst = format::json_pretty;
  }
  else {
    throw std::runtime_error("invalid output format '" + src + "'");
  }
}

double compute_speed_kbits(double measured_bytes, double elapsed_time_ms)
{
  return (elapsed_time_ms > 0.0) ? ((measured_bytes * 8.0) / elapsed_time_ms) : 0.0;
}

std::string format_time_us(double time, unsigned int precision, int width)
{
  std::string unit = "us";
  if (time > 1000.0) {
    unit = "ms";
    time /= 1000.0;
    if (time > 1000.0) {
      unit = "s";
      time /= 1000.0;
    }
  }
  std::stringstream str;
  str << std::setprecision(precision) << std::setw(width) << std::right << time << " " << unit;
  return str.str();
}

std::string format_speed_kbps(double speed, unsigned int precision, int width)
{
  std::string unit = "Kbit/s";
  if (speed > 1000.0) {
    unit = "Mbit/s";
    speed /= 1000.0;
    if (speed > 1000.0) {
      unit = "Gbit/s";
      speed /= 1000.0;
    }
  }
  std::stringstream str;
  str << std::setprecision(precision) << std::setw(width) << std::right << speed << " " << unit;
  return str.str();
}

std::string format_measured_data_kb(double data, unsigned int precision, int width)
{
  std::string unit = "KB";
  if (data > 1000.0) {
    unit = "MB";
    data /= 1000.0;
    if (data > 1000.0) {
      unit = "GB";
      data /= 1000.0;
    }
  }
  std::stringstream str;
  str << std::setprecision(precision) << std::setw(width) << std::right << data << " " << unit;
  return str.str();
}

std::string format_speed_kbps(double measured_bytes, double elapsed_time_ms, unsigned int precision, int width)
{
  return format_speed_kbps(compute_speed_kbits(measured_bytes, elapsed_time_ms), precision, width);
}

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::sample, unsigned int>& speed)
{
  os << format_time_us(speed.first.m_mean_us, speed.second, speed.second + 1);
  return os;
}

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::speed, unsigned int>& speed)
{
  auto percent = ((double)speed.first.m_elapsed_time_ms / speed.first.m_max_time_ms) * 100;
  os << "[" << std::fixed << std::setprecision(0) << std::setw(3) << std::right << percent << "%] ";
  os << format_speed_kbps(speed.first.m_measured_bytes, speed.first.m_elapsed_time_ms, speed.second, speed.second + 1);
  return os;
}

std::ostream& operator<<(std::ostream& os, const std::pair<rstream::nperf::metrics, unsigned int>& metrics)
{
  std::string name;
  if (metrics.first.m_data.type() == typeid(rstream::nperf::sample)) {
    auto type = boost::get<rstream::nperf::sample>(metrics.first.m_data).m_type;
    if (type == rstream::nperf::sample::type::connection) {
      name = "connection";
    }
    else if (type == rstream::nperf::sample::type::handshake) {
      name = "handshake";
    }
    else {
      name = "ping";
    }
  }
  else if (metrics.first.m_data.type() == typeid(rstream::nperf::speed)) {
    name = metrics.first.m_options & rstream::nperf::option::download ? "download" : "upload";
  }
  else {
    name = "error";
  }
  os << std::setw(10) << std::right << name << " : ";
  if (metrics.first.m_data.type() == typeid(rstream::nperf::sample)) {
    os << std::make_pair(boost::get<rstream::nperf::sample>(metrics.first.m_data), metrics.second);
  }
  else if (metrics.first.m_data.type() == typeid(rstream::nperf::speed)) {
    os << std::make_pair(boost::get<rstream::nperf::speed>(metrics.first.m_data), metrics.second);
  }
  else if (metrics.first.m_data.type() == typeid(boost::system::error_code)) {
    os << boost::get<boost::system::error_code>(metrics.first.m_data).message();
  }
  return os;
}

void log(const std::string& str)
{
  if (g_str_sink) {
    g_str_sink(str);
  }
  else {
    std::cout << str << std::endl;
  }
}

void log(const nlohmann::json& json, bool pretty)
{
  std::stringstream str;
  str << json.dump(pretty ? 2 : -1) << std::endl;
  log(str.str());
}

void log(const rstream::nperf::metrics& metrics, bool pretty, bool display_progress, unsigned int precision, bool extra_infos)
{
  if (!display_progress && !metrics.m_final) {
    return;
  }
  if (!extra_infos && metrics.m_data.type() == typeid(rstream::nperf::sample) && boost::get<rstream::nperf::sample>(metrics.m_data).m_type != rstream::nperf::sample::type::ping) {
    return;
  }
  if (!pretty) {
    std::cout << std::make_pair(metrics, precision);
  }
  else {
    std::cout << "\33[2K\r" << std::make_pair(metrics, precision) << ' ';
  }
  if (metrics.m_final) {
    if (!pretty) {
      std::cout << " ";
    }
    if (metrics.m_data.type() == typeid(rstream::nperf::sample)) {
      auto speed = boost::get<rstream::nperf::sample>(metrics.m_data);
      if (speed.m_size > 1) {
        std::cout << "(min: " << format_time_us(speed.m_min_us, precision) << " | max: " << format_time_us(speed.m_max_us, precision) << " | stdev: " << format_time_us(speed.m_stdev_us, precision) << ")";
      }
    }
    else if (metrics.m_data.type() == typeid(rstream::nperf::speed)) {
      std::cout << "(data transferred: " << format_measured_data_kb((double)boost::get<rstream::nperf::speed>(metrics.m_data).m_measured_bytes / 1000.0, precision) << ")";
    }
  }
  if (!pretty || metrics.m_final) {
    std::cout << std::endl;
  }
  else {
    std::cout << std::flush;
  }
}

void log(const rstream::nperf::metrics& metrics, format format, bool display_progress, unsigned int precision, bool extra_infos)
{
  if (!display_progress && !metrics.m_final) {
    return;
  }
  if (format == format::human || format == format::human_pretty) {
    log(metrics, format == format::human_pretty, display_progress, precision, extra_infos);
  }
  else if (format == format::json || format == format::json_pretty) {
    nlohmann::json json;
    json << metrics;
    log(json, format == format::json_pretty);
  }
}
