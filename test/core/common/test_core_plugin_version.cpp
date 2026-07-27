// See LICENSE file in the project root for license information.

#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/system/system_error.hpp>

#include <nlohmann/json.hpp>

#include <rstream/core/error.hpp>
#include <rstream/core/object_id.hpp>
#include <rstream/core/plugin.hpp>
#include <rstream/core/version.hpp>
#include <rstream/io/detail/metrics/error.hpp>

namespace core_plugin   = rstream::core::plugin;
namespace detail_plugin = rstream::core::detail::plugin;
namespace metrics_error = rstream::io::detail::metrics::error;

class sample_element : public core_plugin::element {
 public:
  static core_plugin::element::info get_element_info()
  {
    return core_plugin::element::info{
        .m_name        = "sample.element",
        .m_description = "sample element",
    };
  }
};

class failing_plugin : public detail_plugin::plugin_simple {
 public:
  failing_plugin()
      : detail_plugin::plugin_simple(
            core_plugin::plugin::location(),
            core_plugin::make_plugin_descriptor(
                core_plugin::plugin::info{
                    .m_name         = "failing.plugin",
                    .m_description  = "failing plugin",
                    .m_version      = "1.0.0",
                    .m_license      = "Apache-2.0",
                    .m_release_date = boost::gregorian::from_simple_string("2026-May-10"),
                }))
  {
  }

 private:
  void init() override
  {
    throw std::runtime_error("initialization failure");
  }
};

static core_plugin::plugin::info sample_plugin_info()
{
  return core_plugin::plugin::info{
      .m_name         = "sample.plugin",
      .m_description  = "sample plugin",
      .m_version      = "1.2.3",
      .m_license      = "Apache-2.0",
      .m_release_date = boost::gregorian::from_simple_string("2026-May-10"),
  };
}

static std::shared_ptr<detail_plugin::plugin_simple> make_sample_plugin()
{
  std::list<core_plugin::element::handle> elements = {
      core_plugin::make_element<sample_element>(),
  };
  return std::make_shared<detail_plugin::plugin_simple>(
      core_plugin::plugin::location(),
      core_plugin::make_plugin_descriptor(sample_plugin_info(), elements));
}

static void check_object_id_shape_and_uniqueness()
{
  std::set<std::string> ids;
  for (int i = 0; i < 512; ++i) {
    auto id = rstream::core::object_id();
    assert(id.size() == 24);
    for (char ch : id) {
      assert(std::isxdigit(static_cast<unsigned char>(ch)));
      assert(!std::isupper(static_cast<unsigned char>(ch)));
    }
    ids.insert(id);
  }
  assert(ids.size() == 512);
}

static void check_version_serialization()
{
  rstream::core::version version = {
      .m_major = "1",
      .m_minor = "2",
      .m_patch = "3",
  };
  std::ostringstream out;
  out << version;
  assert(out.str().find("major : 1") != std::string::npos);
  assert(out.str().find("minor : 2") != std::string::npos);
  assert(out.str().find("patch : 3") != std::string::npos);

  nlohmann::json json;
  json << version;
  assert(json.at("major") == "1");
  assert(json.at("minor") == "2");
  assert(json.at("patch") == "3");

  rstream::core::compiler compiler = {
      .m_id      = "AppleClang",
      .m_version = "21.0.0",
  };
  std::ostringstream compiler_out;
  compiler_out << compiler;
  assert(compiler_out.str().find("id      : AppleClang") != std::string::npos);
  assert(compiler_out.str().find("version : 21.0.0") != std::string::npos);
  json << compiler;
  assert(json.at("id") == "AppleClang");
  assert(json.at("version") == "21.0.0");

  rstream::core::system system = {
      .m_system    = "Darwin",
      .m_processor = "arm64",
  };
  std::ostringstream system_out;
  system_out << system;
  assert(system_out.str().find("system    : Darwin") != std::string::npos);
  assert(system_out.str().find("processor : arm64") != std::string::npos);
  json << system;
  assert(json.at("system") == "Darwin");
  assert(json.at("processor") == "arm64");

  rstream::core::version_control version_control = {
      .m_branch           = "main",
      .m_commit           = "0123456789abcdef",
      .m_unstaged_changes = true,
  };
  std::ostringstream version_control_out;
  version_control_out << version_control;
  assert(version_control_out.str().find("branch           : main") != std::string::npos);
  assert(version_control_out.str().find("commit           : 0123456789abcdef") != std::string::npos);
  assert(version_control_out.str().find("unstaged_changes : 1") != std::string::npos);
  json << version_control;
  assert(json.at("branch") == "main");
  assert(json.at("commit") == "0123456789abcdef");
  assert(json.at("unstaged_changes") == true);

  rstream::core::project_info manual_info = {
      .m_version         = version,
      .m_compiler        = {.m_cxx = compiler},
      .m_system          = {.m_host = system, .m_target = {.m_system = "Linux", .m_processor = "x86_64"}},
      .m_build_date      = "2026-05-10",
      .m_version_control = version_control,
  };
  std::ostringstream manual_info_out;
  manual_info_out << manual_info;
  assert(manual_info_out.str().find("version control") != std::string::npos);
  assert(manual_info_out.str().find("build date : 2026-05-10") != std::string::npos);
  json << manual_info;
  assert(json.at("version").at("major") == "1");
  assert(json.at("compiler").at("CXX").at("id") == "AppleClang");
  assert(json.at("system").at("host").at("processor") == "arm64");
  assert(json.at("system").at("target").at("system") == "Linux");
  assert(json.at("version_control").at("branch") == "main");

  auto info = rstream::core::get_project_info();
  assert(!info.m_version.m_major.empty());
  assert(!info.m_compiler.m_cxx.m_id.empty());
  assert(!info.m_system.m_host.m_system.empty());
  assert(!info.m_system.m_target.m_processor.empty());
  assert(!info.m_build_date.empty());
  nlohmann::json info_json;
  info_json << info;
  assert(info_json.contains("version"));
  assert(info_json.contains("compiler"));
  assert(info_json.contains("system"));
  assert(info_json.contains("build_date"));
}

static void check_plugin_factory_registration_and_lookup()
{
  core_plugin::factory factory(nlohmann::json::object());
  auto plugin = make_sample_plugin();

  bool uninitialized_failed = false;
  try {
    (void)plugin->get_extended_info();
  }
  catch (const std::logic_error&) {
    uninitialized_failed = true;
  }
  assert(uninitialized_failed);

  boost::system::error_code error_code;
  error_code = rstream::core::error::code::plugin_not_found;
  factory.register_plugin(plugin, error_code);
  assert(!error_code);

  auto plugins = factory.get_plugins();
  assert(plugins.size() == 1);
  assert(plugins.front().m_name == "sample.plugin");
  assert(plugins.front().m_type == detail_plugin::type::static_);

  auto found_plugin = factory.get_plugin("sample.plugin", error_code);
  assert(!error_code);
  assert(found_plugin.m_name == "sample.plugin");
  assert(found_plugin.m_version == "1.2.3");
  assert(found_plugin.m_type == detail_plugin::type::static_);

  auto elements = factory.get_elements();
  assert(elements.size() == 1);
  assert(elements.front().m_name == "sample.element");

  auto found_element = factory.get_element("sample.element", error_code);
  assert(!error_code);
  assert(found_element.m_name == "sample.element");

  auto created = factory.create("sample.element", error_code);
  assert(!error_code);
  assert(created);
  assert(core_plugin::dynamic_element_cast<sample_element>(created));

  factory.register_plugin(nullptr, error_code);
  assert(error_code == rstream::core::error::make_error_code(rstream::core::error::code::object_null));

  factory.register_plugin(plugin, error_code);
  assert(error_code == rstream::core::error::make_error_code(rstream::core::error::code::plugin_already_registered));
  assert(factory.get_plugins().size() == 1);

  factory.register_plugin(std::make_shared<failing_plugin>(), error_code);
  assert(error_code == rstream::core::error::make_error_code(rstream::core::error::code::plugin_initialization_failed));
  assert(factory.get_plugins().size() == 1);

  auto missing = factory.create("missing.element", error_code);
  assert(!missing);
  assert(error_code == rstream::core::error::make_error_code(rstream::core::error::code::plugin_not_found));

  bool throwing_lookup_failed = false;
  try {
    (void)factory.get_plugin("missing.plugin");
  }
  catch (const boost::system::system_error& error) {
    throwing_lookup_failed = error.code() == rstream::core::error::make_error_code(rstream::core::error::code::plugin_not_found);
  }
  assert(throwing_lookup_failed);
}

static void check_plugin_factory_ignores_invalid_dynamic_libraries()
{
  const auto directory = std::filesystem::temp_directory_path() / ("rstream-plugin-test-" + rstream::core::object_id());
  std::filesystem::create_directories(directory);
#ifdef _WIN32
  const auto plugin = directory / "rstream-plugin-invalid.dll";
#else
  const auto plugin = directory / "rstream-plugin-invalid.so";
#endif
  {
    std::ofstream stream(plugin.string(), std::ios::binary);
    stream << "invalid shared library";
  }
  core_plugin::factory factory({
#ifdef _WIN32
      {"pattern", "^rstream-plugin(.*).dll$"},
#else
      {"pattern", "^rstream-plugin(.*).so$"},
#endif
      {"search_paths", {directory.string()}},
  });
  assert(factory.get_plugins().empty());
  std::filesystem::remove_all(directory);
}

#ifdef RSTREAM_TEST_DYNAMIC_PLUGINS
static void check_plugin_factory_reuses_dynamic_libraries()
{
  for (std::size_t iteration = 0; iteration < 8; ++iteration) {
    core_plugin::element::ptr element;
    {
      core_plugin::factory factory(core_plugin::factory::default_config());
      boost::system::error_code error_code;
      element = factory.create("io.stream.tcp", error_code);
      assert(!error_code);
      assert(element);
    }
    element.reset();
  }
}
#endif

static void check_plugin_serialization()
{
  auto info = sample_plugin_info();
  std::ostringstream plugin_out;
  plugin_out << info;
  assert(plugin_out.str().find("sample.plugin") != std::string::npos);
  assert(plugin_out.str().find("Apache-2.0") != std::string::npos);

  std::ostringstream element_out;
  element_out << sample_element::get_element_info();
  assert(element_out.str().find("sample.element") != std::string::npos);
}

static void check_metrics_error_messages()
{
  struct expected_message {
    metrics_error::code m_code;
    std::string m_message;
  };
  const expected_message expected[] = {
      {metrics_error::code::success, "success"},
      {metrics_error::code::invalid_state, "invalid state"},
      {metrics_error::code::no_valid_endpoint, "no valid endpoint"},
      {metrics_error::code::operation_aborted, "operation aborted"},
      {metrics_error::code::operation_timeout, "operation timeout"},
      {metrics_error::code::no_data_available, "no data available"},
  };
  for (const auto& item : expected) {
    auto code = metrics_error::make_error_code(item.m_code);
    assert(code.message() == item.m_message);
    assert(rstream::io::detail::metrics::to_string(item.m_code) == item.m_message);
  }
  assert(rstream::io::detail::metrics::to_string(static_cast<metrics_error::code>(999)) == "unknown error");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_object_id_shape_and_uniqueness();
  check_version_serialization();
  check_plugin_factory_registration_and_lookup();
  check_plugin_factory_ignores_invalid_dynamic_libraries();
#ifdef RSTREAM_TEST_DYNAMIC_PLUGINS
  check_plugin_factory_reuses_dynamic_libraries();
#endif
  check_plugin_serialization();
  check_metrics_error_messages();
  return 0;
}
