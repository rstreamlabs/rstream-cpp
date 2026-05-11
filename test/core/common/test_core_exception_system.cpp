// See LICENSE file in the project root for license information.

#include <cassert>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/system/errc.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/core/error.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/system.hpp>

static void check_system_error_formats_code_and_context()
{
  rstream::core::system_error plain(rstream::core::error::make_error_code(rstream::core::error::code::invalid_size));
  assert(plain.code() == rstream::core::error::make_error_code(rstream::core::error::code::invalid_size));
  assert(std::string(plain.what()) == "invalid size");
  assert(std::string(plain.what()) == "invalid size");

  rstream::core::system_error with_context(
      rstream::core::error::make_error_code(rstream::core::error::code::object_not_writable),
      "while mapping user buffer");
  assert(std::string(with_context.what()).find("object is not writable") != std::string::npos);
  assert(std::string(with_context.what()).find("while mapping user buffer") != std::string::npos);
}

static void check_system_error_what_is_thread_safe()
{
  const rstream::core::system_error error(
      rstream::core::error::make_error_code(rstream::core::error::code::invalid_size),
      "while checking concurrent readers");
  const auto expected = std::string(error.what());
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i < 8; ++i) {
    threads.emplace_back([&error, &expected]() {
      for (std::size_t j = 0; j < 1000; ++j) {
        assert(std::string(error.what()) == expected);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

static void check_throwable_wraps_standard_exception()
{
  std::exception_ptr ptr;
  try {
    throw std::runtime_error("boom");
  }
  catch (...) {
    ptr = std::current_exception();
  }

  rstream::core::throwable error(ptr);
  assert(error);
  assert(error.get() == ptr);
  assert(error.get_message() == "boom");
  assert(std::string(error.what()).find("boom") != std::string::npos);
  assert(rstream::core::throwable::message(ptr) == "boom");
  assert(rstream::core::throwable::to_string(ptr).find("boom") != std::string::npos);
  assert(!rstream::core::throwable::name(ptr).empty());
}

static void check_throwable_wraps_boost_and_core_errors()
{
  boost::system::error_code boost_code = boost::system::errc::make_error_code(boost::system::errc::permission_denied);
  rstream::core::throwable boost_error(boost_code);
  assert(boost_error);
  assert(boost_error.get_message().find("Permission denied") != std::string::npos
         || boost_error.get_message().find("permission denied") != std::string::npos);

  rstream::core::system_error core_error(rstream::core::error::make_error_code(rstream::core::error::code::object_null));
  rstream::core::throwable wrapped_core_error(core_error);
  assert(wrapped_core_error);
  assert(wrapped_core_error.get_message().find("object is null") != std::string::npos);
}

static void check_throwable_rejects_null_access()
{
  rstream::core::throwable undefined(nullptr);
  assert(!undefined);
  assert(std::string(undefined.what()) == "undefined exception");

  bool name_failed = false;
  try {
    (void)undefined.get_name();
  }
  catch (const boost::system::system_error& error) {
    name_failed = error.code() == rstream::core::error::make_error_code(rstream::core::error::code::object_null);
  }
  assert(name_failed);

  bool message_failed = false;
  try {
    (void)undefined.get_message();
  }
  catch (const boost::system::system_error& error) {
    message_failed = error.code() == rstream::core::error::make_error_code(rstream::core::error::code::object_null);
  }
  assert(message_failed);
}

static void check_nested_error_keeps_original_context()
{
  rstream::core::nested_error root("root failure");
  assert(std::string(root.what()).find("root failure") != std::string::npos);
  assert(!root.get_cause());

  rstream::core::nested_error child("child failure", root.get_error());
  assert(std::string(child.what()).find("child failure") != std::string::npos);
  assert(std::string(child.what()).find("caused by") != std::string::npos);
  assert(std::string(child.what()).find("root failure") != std::string::npos);

  auto replacement = rstream::core::throwable(std::make_exception_ptr(std::runtime_error("replacement")));
  auto before      = std::string(child.what());
  child.set_error(replacement);
  child.set_cause(replacement);
  assert(std::string(child.what()) == before);

  rstream::core::nested_error mutable_child("mutable child");
  mutable_child.set_cause(root.get_error());
  assert(std::string(mutable_child.what()).find("root failure") != std::string::npos);
}

static void check_system_identity_is_normalized()
{
  const auto info = rstream::core::get_system_info();
  assert(!info.m_sysname.empty());
  assert(!info.m_machine.empty());

  const auto runtime = rstream::core::get_runtime_identity();
  assert(!runtime.m_os.empty());
  assert(!runtime.m_arch.empty());
  assert(rstream::core::get_runtime_os() == runtime.m_os);
  assert(rstream::core::get_runtime_arch() == runtime.m_arch);

  const auto compiletime = rstream::core::get_compiletime_identity();
  assert(rstream::core::get_compiletime_os() == compiletime.m_os);
  assert(rstream::core::get_compiletime_arch() == compiletime.m_arch);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_system_error_formats_code_and_context();
  check_system_error_what_is_thread_safe();
  check_throwable_wraps_standard_exception();
  check_throwable_wraps_boost_and_core_errors();
  check_throwable_rejects_null_access();
  check_nested_error_keeps_original_context();
  check_system_identity_is_normalized();
  return 0;
}
