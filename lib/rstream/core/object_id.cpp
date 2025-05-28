// See LICENSE file in the project root for license information.

#include "object_id.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <boost/random/random_device.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include <rstream/config.hpp>

namespace rstream {
namespace core {

class RSTREAM_GNUC_INTERNAL object_id_generator {
 public:
  object_id_generator();

  std::string generate();

 private:
  uint16_t get_process_id();

  uint32_t random();

  void append_hex(std::ostringstream& oss, uint8_t value);

  void append_hex(std::ostringstream& oss, uint32_t value);

  uint32_t m_machine_id;

  uint16_t m_process_id;

  std::atomic<uint32_t> m_counter;
};

object_id_generator::object_id_generator()
    : m_machine_id(random()),
      m_process_id(get_process_id()),
      m_counter(random())
{
}

std::string object_id_generator::generate()
{
  std::ostringstream oss;
  // 1. Add the 4-byte timestamp
  uint32_t timestamp = static_cast<uint32_t>(std::time(nullptr));
  append_hex(oss, timestamp);
  // 2. Add the 3-byte machine identifier
  for (int i = 2; i >= 0; --i) {
    append_hex(oss, static_cast<uint8_t>((m_machine_id >> (i * 8)) & 0xFF));
  }
  // 3. Add the 2-byte process identifier
  append_hex(oss, static_cast<uint8_t>((m_process_id >> 8) & 0xFF));
  append_hex(oss, static_cast<uint8_t>(m_process_id & 0xFF));
  // 4. Add the 3-byte counter (incremented atomically)
  uint32_t count = m_counter.fetch_add(1) & 0xFFFFFF;  // 3 bytes
  for (int i = 2; i >= 0; --i) {
    append_hex(oss, static_cast<uint8_t>((count >> (i * 8)) & 0xFF));
  }
  return oss.str();
}

uint16_t object_id_generator::get_process_id()
{
#ifdef _WIN32
  return static_cast<uint16_t>(::GetCurrentProcessId());
#else
  return static_cast<uint16_t>(::getpid());
#endif
}

uint32_t object_id_generator::random()
{
  boost::random::random_device rd;
  boost::random::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFF);
  return dist(rd);
}

void object_id_generator::append_hex(std::ostringstream& oss, uint8_t value)
{
  oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(value);
}

void object_id_generator::append_hex(std::ostringstream& oss, uint32_t value)
{
  oss << std::hex << std::setw(8) << std::setfill('0') << value;
}

std::string object_id()
{
  static object_id_generator object_id_generator;
  return object_id_generator.generate();
}

}  // namespace core
}  // namespace rstream
