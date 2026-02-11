// See LICENSE file in the project root for license information.

#include "system.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

namespace {

std::string resolve_compiletime_os()
{
#ifdef RSTREAM_BUILD_OS
  return std::string(RSTREAM_BUILD_OS);
#endif
  return rstream::core::get_system_info().m_sysname;  // Fallback
}

std::string resolve_compiletime_arch()
{
#ifdef RSTREAM_BUILD_ARCH
  return std::string(RSTREAM_BUILD_ARCH);
#endif
  return rstream::core::get_system_info().m_machine;  // Fallback
}

std::string resolve_runtime_os()
{
  return rstream::core::get_system_info().m_sysname;
}

std::string resolve_runtime_arch()
{
  return rstream::core::get_system_info().m_machine;
}

}  // namespace

namespace rstream {
namespace core {

#ifdef _WIN32
inline std::string get_processor_architecture(WORD wProcessorArchitecture)
{
  switch (wProcessorArchitecture) {
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
#endif

system_info get_system_info()
{
#ifdef _WIN32  // Windows
  OSVERSIONINFOEX osver;
  ::ZeroMemory(&osver, sizeof(osver));
  osver.dwOSVersionInfoSize = sizeof(osver);
  ::GetVersionEx(reinterpret_cast<LPOSVERSIONINFO>(&osver));
  SYSTEM_INFO sysInfo;
  ::GetSystemInfo(&sysInfo);
  TCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD size = sizeof(computerName) / sizeof(computerName[0]);
  ::GetComputerName(computerName, &size);
  std::ostringstream releaseStream;
  releaseStream << osver.dwMajorVersion << "." << osver.dwMinorVersion;
  std::ostringstream versionStream;
  versionStream << osver.dwBuildNumber;
  system_info info;
  info.m_sysname = "windows";
#ifdef UNICODE
  std::wstring wstrComputerName(computerName);
  info.m_nodename = std::string(wstrComputerName.begin(), wstrComputerName.end());
#else
  info.m_nodename = std::string(computerName);
#endif
  info.m_release = releaseStream.str();
  info.m_version = versionStream.str();
  info.m_machine = get_processor_architecture(sysInfo.wProcessorArchitecture);
  return info;
#else  // Non-Windows (Linux, macOS, etc.)
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

}  // namespace core
}  // namespace rstream
