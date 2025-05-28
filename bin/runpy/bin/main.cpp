// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/dll/runtime_symbol_info.hpp>

#include <rstream/config.hpp>
#include <rstream/python/exception.hpp>
#include <rstream/python/python.hpp>

static int run(int argc, char** argv);

int main(int argc, char** argv)
{
  int res;
  std::exception_ptr error = nullptr;
  try {
    res = run(argc, argv);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    std::string error_msg;
    try {
      std::rethrow_exception(error);
    }
    catch (const std::exception& exception) {
      error_msg = exception.what();
    }
    catch (...) {
      error_msg = "unknown error occured";
    }
    std::cerr << error_msg << std::endl;
  }
  return error ? 1 : res;
}

static int run(int argc, char** argv)
{
  if (argc > 0) {
    if (boost::filesystem::path(argv[0]).filename() == "rstream-runpy") {
      throw std::runtime_error("this program cannot be called directly");
    }
  }
  auto program_location     = boost::filesystem::canonical(boost::dll::program_location());
  auto python3_sitepackages = boost::filesystem::canonical(program_location.parent_path() / boost::filesystem::path(PYTHON3_SITEPACKAGES));
  auto python3_stdlib       = python3_sitepackages.parent_path() / "stdlib";
  PyPreConfig preconfig;
  PyPreConfig_InitPythonConfig(&preconfig);
  preconfig.utf8_mode = 1;
  if (PyStatus_Exception(Py_PreInitialize(&preconfig))) {
    throw std::runtime_error("Failed to preinitialize Python interpreter");
  }
  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  PyWideStringList argv_list = {0, nullptr};
  PyWideStringList_Append(&argv_list, L"");
  for (int i = 0; i < argc; ++i) {
    size_t length = strlen(argv[i]);
    wchar_t* arg  = (wchar_t*)malloc((length + 1) * sizeof(wchar_t));
    mbstowcs(arg, argv[i], length + 1);
    PyWideStringList_Append(&argv_list, arg);
    free(arg);
  }
  config.argv = argv_list;
  if (boost::filesystem::exists(python3_stdlib)) {
    std::wstring stdlib_path = python3_stdlib.wstring() + L":" + (python3_stdlib / "lib-dynload").wstring();
    PyWideStringList_Append(&config.module_search_paths, stdlib_path.c_str());
  }
  if (PyStatus_Exception(Py_InitializeFromConfig(&config))) {
    throw std::runtime_error("Failed to initialize Python interpreter");
  }
  PyConfig_Clear(&config);
  int res;
  try {
    if (boost::filesystem::exists(python3_sitepackages)) {
      rstream::python::prepend_sys_path(python3_sitepackages.string());
    }
    res = boost::python::extract<int>(boost::python::import("rstream.runpy").attr("run")());
  }
  catch (const boost::python::error_already_set&) {
    throw rstream::python::exception();
  }
  if (Py_IsInitialized()) {
    Py_FinalizeEx();
  }
  return res;
}
