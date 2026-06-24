// See LICENSE file in the project root for license information.

#include "webtty.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>
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

static const rstream::core::logger g_logger({"rstream", "webtty", "core"});

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

#ifndef _WIN32

std::vector<std::uint32_t> lookup_group_ids(const struct passwd* pw, std::error_code& error_code)
{
#ifdef __APPLE__
  using native_group_type = int;
#else
  using native_group_type = gid_t;
#endif
  auto append_unique = [](std::vector<std::uint32_t>& dst, std::uint32_t value) {
    if (std::find(dst.begin(), dst.end(), value) == dst.end()) {
      dst.push_back(value);
    }
  };
  std::vector<std::uint32_t> groups;
  append_unique(groups, static_cast<std::uint32_t>(pw->pw_gid));
  int group_count = 16;
  std::vector<native_group_type> native_groups(static_cast<std::size_t>(group_count));
  for (;;) {
    group_count   = static_cast<int>(native_groups.size());
    errno         = 0;
    auto base_gid = static_cast<native_group_type>(pw->pw_gid);
    if (::getgrouplist(pw->pw_name, base_gid, native_groups.data(), &group_count) != -1) {
      break;
    }
    if (group_count <= static_cast<int>(native_groups.size())) {
      group_count = static_cast<int>(native_groups.size() * 2);
    }
    if (group_count <= 0 || group_count > 4096) {
      error_code = errno == 0 ? std::make_error_code(std::errc::invalid_argument) : std::error_code(errno, std::system_category());
      return {};
    }
    native_groups.resize(static_cast<std::size_t>(group_count));
  }
  native_groups.resize(static_cast<std::size_t>(group_count));
  for (auto group : native_groups) {
    if constexpr (std::is_signed_v<native_group_type>) {
      if (group < 0) {
        error_code = std::make_error_code(std::errc::invalid_argument);
        return {};
      }
    }
    if (static_cast<std::uint64_t>(group) > std::numeric_limits<std::uint32_t>::max()) {
      error_code = std::make_error_code(std::errc::invalid_argument);
      return {};
    }
    append_unique(groups, static_cast<std::uint32_t>(group));
  }
  return groups;
}

#endif

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
namespace webtty {

void parse_execution_mode(execution_mode& dst, const std::string& src)
{
  if (src == "spawn" || src.empty()) {
    dst = execution_mode::spawn;
  }
  else if (src == "login") {
    dst = execution_mode::login;
  }
  else {
    throw std::runtime_error("invalid execution mode '" + src + "'");
  }
}

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
    throw std::runtime_error("invalid --transport \"" + src + "\" (valid: plain, websocket)");
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
  identifier user   = username ? username.get() : getuid();
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
    auto groups = lookup_group_ids(pw, error_code);
    if (error_code) {
      return;
    }
    user_info = {
        .m_name   = pw->pw_name,
        .m_shell  = pw->pw_shell,
        .m_home   = pw->pw_dir,
        .m_uid    = pw->pw_uid,
        .m_gid    = pw->pw_gid,
        .m_groups = groups,
    };
    error_code.clear();
  }
}

#endif

}  // namespace protocol

std::string build_webtty_uri()
{
  return build_webtty_uri({});
}

std::map<std::string, std::string> build_webtty_labels(const webtty_uri_options& options)
{
  std::map<std::string, std::string> labels;
  auto details          = get_os_details();
  auto runtime_identity = rstream::core::get_runtime_identity();
  auto set_label        = [&labels](const std::string& key, const std::string& value) {
    if (!value.empty()) {
      labels[key] = value;
    }
  };
  set_label("application-protocol", "rstream.webtty");
  set_label("rstream.webtty.capabilities", "exec");
  set_label("rstream.webtty.execution.mode", options.m_execution_mode == execution_mode::login ? "login" : "spawn");
  set_label("rstream.webtty.exec.path", "/");
  set_label("rstream.webtty.server_id", options.m_server_id);
  set_label("rstream.webtty.host_key_id", options.m_host_key_id);
  set_label("rstream.webtty.encryption_policy", options.m_encryption_policy);
  if (!options.m_host_key_id.empty()) {
    set_label("rstream.webtty.e2e", "required");
    set_label("rstream.webtty.client_proof", "required");
  }
  else {
    set_label("rstream.webtty.e2e", "disabled");
    set_label("rstream.webtty.client_proof", "none");
  }
  set_label("rstream.webtty.os_family", runtime_identity.m_os);
  set_label("rstream.webtty.arch", runtime_identity.m_arch);
  set_label("rstream.webtty.os_id", details.id);
  set_label("rstream.webtty.os_version_id", details.version_id);
  set_label("rstream.webtty.os_version_codename", details.codename);
  set_label("rstream.webtty.os_pretty_name", details.pretty_name);
  set_label("rstream.webtty.kernel_release", details.kernel);
  set_label("rstream.webtty.hostname", details.hostname);
  for (const auto& label : options.m_labels) {
    set_label("rstream.webtty.label." + label.first, label.second);
  }
  if (!options.m_server_admission_label.empty()) {
    set_label("rstream.webtty.server_admission", options.m_server_admission_label);
  }
  return labels;
}

std::string build_webtty_uri(const webtty_uri_options& options)
{
  std::string uri = "rstrm://";
  if (options.m_managed && !options.m_server_id.empty()) {
    uri.append(pct_encode(options.m_server_id));
  }
  uri.append("?rstrm.publish=");
  uri.append(options.m_publish ? "true" : "false");
  uri.append("&rstrm.protocol=");
  uri.append(options.m_managed ? "webtty&rstrm.type=bytestream" : "http");
  if (options.m_publish) {
    uri.append("&rstrm.token_auth=true");
  }
  auto labels       = build_webtty_labels(options);
  auto append_label = [&uri](const std::string& key, const std::string& value) {
    const auto label = key + "=" + value;
    uri.append("&rstrm.labels=");
    uri.append(pct_encode(label));
  };
  for (const auto& label : labels) {
    append_label(label.first, label.second);
  }
  return uri;
}

}  // namespace webtty
}  // namespace rstream
