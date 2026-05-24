// See LICENSE file in the project root for license information.

#include "rtty.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <lmcons.h>
#include <windows.h>
#include <winternl.h>
#else
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#include <cstdlib>

#include <rstream/core/log.hpp>
#include <rstream/core/system.hpp>

#include "error.hpp"

static const rstream::core::logger g_logger({"rstream", "rtty", "core"});

namespace {

struct os_details {
  std::string id;
  std::string version_id;
  std::string codename;
  std::string pretty_name;
  std::string kernel;
  std::string hostname;
};

std::string trim_copy(const std::string& value)
{
  auto begin = value.begin();
  auto end   = value.end();
  while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  while (begin != end) {
    auto last = end;
    --last;
    if (!std::isspace(static_cast<unsigned char>(*last))) {
      break;
    }
    end = last;
  }
  return std::string(begin, end);
}

std::string unquote_value(const std::string& value)
{
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
    return value;
  }
  std::string out;
  out.reserve(value.size() - 2);
  for (std::size_t i = 1; i + 1 < value.size(); ++i) {
    char c = value[i];
    if (c == '\\' && i + 1 < value.size() - 1) {
      char next = value[++i];
      switch (next) {
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case '\\':
        case '"':
          out.push_back(next);
          break;
        default:
          out.push_back(next);
          break;
      }
      continue;
    }
    out.push_back(c);
  }
  return out;
}

std::optional<std::map<std::string, std::string>> parse_os_release()
{
  std::ifstream file("/etc/os-release");
  if (!file.is_open()) {
    return std::nullopt;
  }
  std::map<std::string, std::string> release;
  std::string line;
  while (std::getline(file, line)) {
    line = trim_copy(line);
    if (line.empty() || line.front() == '#') {
      continue;
    }
    auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    auto key     = trim_copy(line.substr(0, pos));
    auto value   = trim_copy(line.substr(pos + 1));
    release[key] = unquote_value(value);
  }
  return release;
}

std::string release_codename(const std::optional<std::map<std::string, std::string>>& release)
{
  if (!release) {
    return "";
  }
  auto it = release->find("VERSION_CODENAME");
  if (it != release->end() && !it->second.empty()) {
    return it->second;
  }
  it = release->find("UBUNTU_CODENAME");
  if (it != release->end()) {
    return it->second;
  }
  return "";
}

std::string release_pretty_fallback(const std::optional<std::map<std::string, std::string>>& release)
{
  if (!release) {
    return "";
  }
  auto name_it              = release->find("NAME");
  auto version_it           = release->find("VERSION");
  const std::string name    = name_it == release->end() ? "" : name_it->second;
  const std::string version = version_it == release->end() ? "" : version_it->second;
  if (!name.empty() && !version.empty()) {
    return name + " " + version;
  }
  if (!name.empty()) {
    return name;
  }
  return version;
}

std::string product_pretty(const std::string& name, const std::string& version)
{
  if (!name.empty() && !version.empty()) {
    return name + " " + version;
  }
  if (!name.empty()) {
    return name;
  }
  return version;
}

std::string plist_value(const std::string& data, const std::string& key)
{
  const std::string needle = "<key>" + key + "</key>";
  auto pos                 = data.find(needle);
  if (pos == std::string::npos) {
    return "";
  }
  auto rest  = data.substr(pos + needle.size());
  auto start = rest.find("<string>");
  auto end   = rest.find("</string>");
  if (start == std::string::npos || end == std::string::npos || end <= start) {
    return "";
  }
  return trim_copy(rest.substr(start + std::string("<string>").size(), end - (start + std::string("<string>").size())));
}

std::pair<std::string, std::string> macos_product()
{
  std::ifstream file("/System/Library/CoreServices/SystemVersion.plist");
  if (!file.is_open()) {
    return {"", ""};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  const auto data = buffer.str();
  return {plist_value(data, "ProductName"), plist_value(data, "ProductVersion")};
}

std::string kernel_release()
{
#ifdef _WIN32
  return "";
#else
  struct utsname info;
  if (uname(&info) != 0) {
    return "";
  }
  return info.release;
#endif
}

std::string hostname()
{
#ifdef _WIN32
  DWORD size = 0;
  ::GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
  if (size > 0) {
    std::wstring buffer(size, L'\0');
    if (::GetComputerNameExW(ComputerNameDnsHostname, buffer.data(), &size)) {
      if (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
      }
      int utf8_size = ::WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), nullptr, 0, nullptr, nullptr);
      if (utf8_size > 0) {
        std::string out(utf8_size, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), out.data(), utf8_size, nullptr, nullptr);
        return out;
      }
    }
  }
  wchar_t fallback[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD fallback_size                           = MAX_COMPUTERNAME_LENGTH + 1;
  if (::GetComputerNameW(fallback, &fallback_size)) {
    int utf8_size = ::WideCharToMultiByte(CP_UTF8, 0, fallback, static_cast<int>(fallback_size), nullptr, 0, nullptr, nullptr);
    if (utf8_size > 0) {
      std::string out(utf8_size, '\0');
      ::WideCharToMultiByte(CP_UTF8, 0, fallback, static_cast<int>(fallback_size), out.data(), utf8_size, nullptr, nullptr);
      return out;
    }
  }
  return "";
#else
  char buffer[256] = {};
  if (gethostname(buffer, sizeof(buffer)) == 0) {
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
  }
  return "";
#endif
}

#ifdef _WIN32
std::wstring read_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name)
{
  DWORD type = 0;
  DWORD size = 0;
  if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &size) != ERROR_SUCCESS) {
    return L"";
  }
  std::wstring buffer(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, buffer.data(), &size) != ERROR_SUCCESS) {
    return L"";
  }
  if (!buffer.empty() && buffer.back() == L'\0') {
    buffer.pop_back();
  }
  return buffer;
}

std::pair<std::string, std::string> windows_product_info()
{
  const wchar_t* subkey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  std::wstring product  = read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"ProductName");
  std::wstring display  = read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"DisplayVersion");
  if (display.empty()) {
    display = read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"ReleaseId");
  }
  if (display.empty()) {
    display = read_registry_string(HKEY_LOCAL_MACHINE, subkey, L"CSDVersion");
  }
  auto to_utf8 = [](const std::wstring& value) -> std::string {
    if (value.empty()) {
      return "";
    }
    int size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
      return "";
    }
    std::string out(size, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
    return out;
  };
  return {to_utf8(product), to_utf8(display)};
}

std::pair<std::string, std::string> windows_version()
{
  OSVERSIONINFOEXW info    = {};
  info.dwOSVersionInfoSize = sizeof(info);
  auto module              = ::GetModuleHandleW(L"ntdll.dll");
  if (module) {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto fn               = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(module, "RtlGetVersion"));
    if (fn) {
      fn(reinterpret_cast<PRTL_OSVERSIONINFOW>(&info));
    }
    else {
      ::GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&info));
    }
  }
  else {
    ::GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&info));
  }
  std::ostringstream version_stream;
  version_stream << info.dwMajorVersion << "." << info.dwMinorVersion << "." << info.dwBuildNumber;
  auto product_info = windows_product_info();
  std::string pretty;
  if (!product_info.first.empty() && !product_info.second.empty()) {
    pretty = product_info.first + " " + product_info.second;
  }
  else if (!product_info.first.empty()) {
    pretty = product_info.first;
  }
  return {version_stream.str(), pretty};
}
#endif

os_details get_os_details()
{
  os_details details;
  details.hostname = hostname();
#ifdef _WIN32
  details.id          = "windows";
  auto win            = windows_version();
  details.version_id  = win.first;
  details.kernel      = win.first;
  details.pretty_name = win.second;
  return details;
#else
  auto release = parse_os_release();
#ifdef __APPLE__
  if (release) {
    auto it = release->find("ID");
    if (it != release->end()) {
      details.id = it->second;
    }
    it = release->find("VERSION_ID");
    if (it != release->end()) {
      details.version_id = it->second;
    }
    details.codename = release_codename(release);
    it               = release->find("PRETTY_NAME");
    if (it != release->end()) {
      details.pretty_name = it->second;
    }
  }
  auto product = macos_product();
  if (details.id.empty()) {
    details.id = "macos";
  }
  if (details.version_id.empty()) {
    details.version_id = product.second;
  }
  if (details.pretty_name.empty()) {
    details.pretty_name = product_pretty(product.first, details.version_id);
  }
#else
  if (release) {
    auto it = release->find("ID");
    if (it != release->end()) {
      details.id = it->second;
    }
    it = release->find("VERSION_ID");
    if (it != release->end()) {
      details.version_id = it->second;
    }
    details.codename = release_codename(release);
    it               = release->find("PRETTY_NAME");
    if (it != release->end()) {
      details.pretty_name = it->second;
    }
  }
  if (details.pretty_name.empty()) {
    details.pretty_name = release_pretty_fallback(release);
  }
#endif
  details.kernel = kernel_release();
  if (details.pretty_name.empty() && !details.id.empty() && !details.version_id.empty()) {
    details.pretty_name = details.id + " " + details.version_id;
  }
  return details;
#endif
}

std::string pct_encode(std::string_view value)
{
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
      out.push_back(static_cast<char>(c));
    }
    else {
      out.push_back('%');
      out.push_back(hex[(c >> 4) & 0x0F]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

}  // namespace

namespace rstream {
namespace rtty {

namespace protocol {

void parse_environment(env_vars& dst, const std::vector<std::string>& src)
{
  static const std::string delimiter = "=";
  dst.clear();
  for (const auto& str : src) {
    if (str.empty()) {
      continue;
    }
    std::string key, value;
    auto pos = str.find(delimiter);
    if (pos != std::string::npos) {
      key   = str.substr(0, pos);
      value = str.substr(pos + delimiter.size());
    }
    else {
      key      = str;
      auto env = std::getenv(key.c_str());
      if (env != nullptr) {
        value = env;
      }
      else {
        continue;
      }
    }
    dst.push_back((environment){.m_key = key, .m_value = value});
  }
}

env_vars::iterator find_environment_variable(env_vars& dst, const std::string& key)
{
  env_vars::iterator it;
  for (it = dst.begin(); it != dst.end(); ++it) {
    if (it->m_key == key) {
      break;
    }
  }
  return it;
}

void add_environment_variable(std::list<environment>& dst, const std::string& key, const std::string& value, bool force)
{
  auto it = find_environment_variable(dst, key);
  if (force || it == dst.end()) {
    environment environment = {
        .m_key   = key,
        .m_value = value,
    };
    if (it != dst.end()) {
      *it = environment;
    }
    else {
      dst.push_back(environment);
    }
  }
}

void add_environment_variable(std::list<environment>& dst, const std::string& key, const char* value, bool force)
{
  add_environment_variable(dst, key, std::string(value == nullptr ? "" : value), force);
}

void add_environment_variable(env_vars& dst, const std::string& key, bool force)
{
  auto value = std::getenv(key.c_str());
  if (value != nullptr) {
    add_environment_variable(dst, key, value, force);
  }
}

void parse_type(type& dst, const std::string& src)
{
  if (src == "websocket") {
    dst = type::websocket;
  }
  else if (src == "plain") {
    dst = type::plain;
  }
  else {
    throw std::runtime_error("invalid protocol '" + src + "'");
  }
}

void parse_identifier(identifier& dst, const std::string& src)
{
  auto is_str_id = [](const std::string& str) {
    auto it = str.begin();
    while (it != str.end() && std::isdigit(static_cast<unsigned char>(*it))) {
      ++it;
    }
    return !str.empty() && it == str.end();
  };
  if (is_str_id(src)) {
    auto value = std::stoull(src);
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      throw std::out_of_range("numeric user identifier is out of range");
    }
    dst = static_cast<std::uint32_t>(value);
  }
  else {
    dst = src;
  }
}

void parse_username(username& dst, const std::string& src)
{
  dst = boost::none;
  if (!src.empty()) {
    identifier identifier;
    parse_identifier(identifier, src);
    dst = identifier;
  }
}

#ifdef _WIN32

void get_user_info(user_info& user_info, std::error_code& error_code)
{
  char username[UNLEN + 1];
  DWORD username_len = UNLEN + 1;
  if (!::GetUserNameA(username, &username_len)) {
    error_code = std::error_code(::GetLastError(), std::system_category());
  }
  if (error_code) {
    return;
  }
  const char* userprofile = getenv("USERPROFILE");
  if (!userprofile) {
#ifdef DEBUG_BUILD
    g_logger->warn("USERPROFILE environment variable is not set");
#endif
    error_code = error::code::server_error;
  }
  if (error_code) {
    return;
  }
  const char* comspec = std::getenv("ComSpec");
  if (!comspec) {
#ifdef DEBUG_BUILD
    g_logger->warn("ComSpec environment variable is not set");
#endif
    error_code = error::code::server_error;
  }
  user_info = {
      .m_name  = username,
      .m_shell = comspec,
      .m_home  = userprofile,
  };
}

#else

void get_user_info(user_info& user_info, const username& username, std::error_code& error_code)
{
  error_code.clear();
  identifier user = username ? username.get() : getuid();
  struct passwd* pw = nullptr;
  errno             = 0;
  if (user.type() == typeid(std::string)) {
    pw = getpwnam(boost::get<std::string>(user).c_str());
  }
  else if (user.type() == typeid(std::uint32_t)) {
    pw = getpwuid(boost::get<std::uint32_t>(user));
  }
  if (!pw) {
    error_code = errno == 0 ? error::code::server_error : std::error_code(errno, std::system_category());
  }
  else {
    user_info = {
        .m_name  = pw->pw_name,
        .m_shell = pw->pw_shell,
        .m_home  = pw->pw_dir,
        .m_uid   = pw->pw_uid,
        .m_gid   = pw->pw_gid,
    };
    error_code.clear();
  }
}

#endif

}  // namespace protocol

std::string build_webtty_uri()
{
  static const std::string base_uri = "rstrm://?rstrm.publish=true&rstrm.protocol=http&rstrm.token_auth=true";
  std::string uri                   = base_uri;
  auto details                      = get_os_details();
  auto runtime_identity             = rstream::core::get_runtime_identity();
  auto append_label                 = [&uri](const std::string& key, const std::string& value) {
    if (value.empty()) {
      return;
    }
    const auto label = key + "=" + value;
    uri.append("&rstrm.labels=");
    uri.append(pct_encode(label));
  };
  append_label("application-protocol", "rstream.webtty");
  append_label("rstream.webtty.capabilities", "exec");
  append_label("rstream.webtty.exec.path", "/");
  append_label("rstream.webtty.os_family", runtime_identity.m_os);
  append_label("rstream.webtty.arch", runtime_identity.m_arch);
  append_label("rstream.webtty.os_id", details.id);
  append_label("rstream.webtty.os_version_id", details.version_id);
  append_label("rstream.webtty.os_version_codename", details.codename);
  append_label("rstream.webtty.os_pretty_name", details.pretty_name);
  append_label("rstream.webtty.kernel_release", details.kernel);
  append_label("rstream.webtty.hostname", details.hostname);
  return uri;
}

}  // namespace rtty
}  // namespace rstream
