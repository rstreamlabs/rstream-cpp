// See LICENSE file in the project root for license information.

#include "system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winternl.h>
#else
#include <sys/utsname.h>
#endif

namespace {

std::string to_lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string normalize_os(std::string value)
{
  value = to_lower(value);
  if (value == "darwin" || value == "macos" || value == "macosx" || value == "osx") {
    return "macos";
  }
  if (value == "windows" || value == "win32" || value == "win") {
    return "windows";
  }
  if (value == "linux") {
    return "linux";
  }
  if (value == "netbsd") {
    return "netbsd";
  }
  if (value == "openbsd") {
    return "openbsd";
  }
  if (value == "freebsd") {
    return "freebsd";
  }
  return value;
}

std::string resolve_compiletime_os()
{
#ifdef RSTREAM_BUILD_OS
  return normalize_os(std::string(RSTREAM_BUILD_OS));
#endif
  return "";
}

std::string resolve_compiletime_arch()
{
#ifdef RSTREAM_BUILD_ARCH
  return std::string(RSTREAM_BUILD_ARCH);
#endif
  return "";
}

std::string resolve_runtime_os()
{
  return normalize_os(rstream::core::get_system_info().m_sysname);
}

std::string resolve_runtime_arch()
{
  return rstream::core::get_system_info().m_machine;
}

}  // namespace

namespace rstream {
namespace core {

#ifdef _WIN32
inline std::string get_processor_architecture(WORD processor_architecture)
{
  switch (processor_architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return "x86_64";
    case PROCESSOR_ARCHITECTURE_INTEL:
      return "x86";
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "arm64";
    case PROCESSOR_ARCHITECTURE_ARM:
      return "arm";
    default:
      return "unknown";
  }
}

using rtl_get_version_fn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);

inline rtl_get_version_fn get_rtl_get_version()
{
  const auto module = ::GetModuleHandleW(L"ntdll.dll");
  if (module == nullptr) {
    return nullptr;
  }
  const auto procedure = ::GetProcAddress(module, "RtlGetVersion");
  static_assert(sizeof(procedure) == sizeof(rtl_get_version_fn));
  rtl_get_version_fn function = nullptr;
  std::memcpy(&function, &procedure, sizeof(function));
  return function;
}
#endif

system_info get_system_info()
{
#ifdef _WIN32
  RTL_OSVERSIONINFOW osver{};
  osver.dwOSVersionInfoSize  = sizeof(osver);
  const auto rtl_get_version = get_rtl_get_version();
  const auto version_status  = rtl_get_version == nullptr ? -1 : rtl_get_version(&osver);
  SYSTEM_INFO sys_info{};
  ::GetNativeSystemInfo(&sys_info);
  std::array<char, MAX_COMPUTERNAME_LENGTH + 1> computer_name{};
  DWORD computer_name_size     = static_cast<DWORD>(computer_name.size());
  const auto has_computer_name = ::GetComputerNameA(computer_name.data(), &computer_name_size) != FALSE;
  std::ostringstream release_stream;
  std::ostringstream version_stream;
  if (version_status >= 0) {
    release_stream << osver.dwMajorVersion << "." << osver.dwMinorVersion;
    version_stream << osver.dwBuildNumber;
  }
  system_info info;
  info.m_sysname  = "windows";
  info.m_nodename = has_computer_name ? std::string(computer_name.data(), computer_name_size) : std::string();
  info.m_release  = release_stream.str();
  info.m_version  = version_stream.str();
  info.m_machine  = get_processor_architecture(sys_info.wProcessorArchitecture);
  return info;
#else
  utsname uts;
  uname(&uts);
  system_info info;
  info.m_sysname  = uts.sysname;
  info.m_nodename = uts.nodename;
  info.m_release  = uts.release;
  info.m_version  = uts.version;
  info.m_machine  = uts.machine;
  return info;
#endif
}

os_identity get_compiletime_identity()
{
  static const os_identity identity = [] {
    os_identity value;
    value.m_os   = resolve_compiletime_os();
    value.m_arch = resolve_compiletime_arch();
    return value;
  }();
  return identity;
}

os_identity get_runtime_identity()
{
  static const os_identity identity = [] {
    os_identity value;
    value.m_os   = resolve_runtime_os();
    value.m_arch = resolve_runtime_arch();
    return value;
  }();
  return identity;
}

std::string get_compiletime_os()
{
  return get_compiletime_identity().m_os;
}

std::string get_compiletime_arch()
{
  return get_compiletime_identity().m_arch;
}

std::string get_runtime_os()
{
  return get_runtime_identity().m_os;
}

std::string get_runtime_arch()
{
  return get_runtime_identity().m_arch;
}

std::optional<std::string> get_environment_variable(const std::string& name)
{
#ifdef _WIN32
  char* value      = nullptr;
  std::size_t size = 0;
  const auto error = ::_dupenv_s(&value, &size, name.c_str());
  if (error != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const auto* value = std::getenv(name.c_str());
  return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

}  // namespace core
}  // namespace rstream
