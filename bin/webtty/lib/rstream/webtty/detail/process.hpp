// See LICENSE file in the project root for license information.

#pragma once

#define BOOST_PROCESS_VERSION 1

#ifndef _WIN32
#include <grp.h>
#include <unistd.h>
#endif

#include <memory>
#include <vector>

#if __has_include(<boost/process/v1/child.hpp>)
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#endif

#if __has_include(<boost/process/v1/extend.hpp>)
#include <boost/process/v1/extend.hpp>
#else
#include <boost/process/extend.hpp>
#endif

#include <rstream/webtty/stream.hpp>
#include <rstream/webtty/webtty.hpp>

namespace rstream {
namespace webtty {
namespace detail {

namespace process {

template <typename... Args>
std::shared_ptr<boost::process::child> make_child(stream::ptr stream_ptr, Args&&... args);

namespace pty {

class handler : public boost::process::extend::handler {
 public:
  handler(std::shared_ptr<stream::pty> stream_ptr);

#ifdef _WIN32

  template <typename Char, typename Sequence>
  void on_setup(boost::process::extend::windows_executor<Char, Sequence>& executor) const;

  template <typename Char, typename Sequence>
  void on_error(boost::process::extend::windows_executor<Char, Sequence>& executor, const std::error_code&) const;

  template <typename Char, typename Sequence>
  void on_success(boost::process::extend::windows_executor<Char, Sequence>& executor) const;

#else

  template <class Executor>
  void on_exec_setup(Executor& executor) const;

  template <class Executor>
  void on_success(Executor& executor) const;

#endif

 private:
  std::shared_ptr<stream::pty> m_stream_ptr;
};

}  // namespace pty

#ifndef _WIN32

namespace uid {

class handler : public boost::process::detail::posix::handler_base_ext, public boost::process::detail::uses_handles {
 public:
  handler(const protocol::user_info& user_info);

  template <class Executor>
  void on_exec_setup(Executor& executor) const;

 private:
  const protocol::user_info m_user_info;
};

}  // namespace uid

#endif

template <typename... Args>
std::shared_ptr<boost::process::child> make_child(stream::ptr stream_ptr, Args&&... args)
{
  std::shared_ptr<boost::process::child> child = nullptr;
  if (stream_ptr->backend() == stream::backend::tty) {
    auto stream_ptr_pty = std::dynamic_pointer_cast<stream::pty>(stream_ptr);
    std::error_code error_code;
    stream_ptr_pty->allocate(error_code);
    if (error_code) {
      throw std::system_error(error_code);
    }
    child = std::make_shared<boost::process::child>(pty::handler(stream_ptr_pty), std::forward<Args>(args)...);
  }
  else {
    auto stream_ptr_pipe = std::dynamic_pointer_cast<stream::pipe>(stream_ptr);
    child                = std::make_shared<boost::process::child>(boost::process::std_out > stream_ptr_pipe->stream(stream::type::std_out),
                                                                   boost::process::std_err > stream_ptr_pipe->stream(stream::type::std_err),
                                                                   boost::process::std_in < stream_ptr_pipe->stream(stream::type::std_in),
                                                                   std::forward<Args>(args)...);
  }
  return child;
}

namespace pty {

#ifdef _WIN32

template <typename Char, typename Sequence>
void handler::on_setup(boost::process::extend::windows_executor<Char, Sequence>& executor) const
{
  std::error_code error_code;
  auto stream_ptr_pty_windows = std::dynamic_pointer_cast<stream::pty_windows>(m_stream_ptr);
  stream_ptr_pty_windows->on_setup(executor, error_code);
  if (error_code) {
    executor.set_error(error_code, "on_setup");
  }
}

template <typename Char, typename Sequence>
void handler::on_error(boost::process::extend::windows_executor<Char, Sequence>& executor, const std::error_code&) const
{
  std::error_code error_code;
  auto stream_ptr_pty_windows = std::dynamic_pointer_cast<stream::pty_windows>(m_stream_ptr);
  stream_ptr_pty_windows->on_error(executor, error_code);
  if (error_code) {
    executor.set_error(error_code, "on_error");
  }
}

template <typename Char, typename Sequence>
void handler::on_success(boost::process::extend::windows_executor<Char, Sequence>& executor) const
{
  std::error_code error_code;
  auto stream_ptr_pty_windows = std::dynamic_pointer_cast<stream::pty_windows>(m_stream_ptr);
  stream_ptr_pty_windows->on_success(executor, error_code);
  if (error_code) {
    executor.set_error(error_code, "on_success");
  }
}

#else

template <class Executor>
void handler::on_exec_setup(Executor& executor) const
{
  std::error_code error_code;
  auto stream_ptr_pty_posix = std::dynamic_pointer_cast<stream::pty_posix>(m_stream_ptr);
  stream_ptr_pty_posix->on_exec_setup(error_code);
  if (error_code) {
    executor.set_error(error_code, "on_exec_setup");
  }
}

template <class Executor>
void handler::on_success(Executor& executor) const
{
  std::error_code error_code;
  auto stream_ptr_pty_posix = std::dynamic_pointer_cast<stream::pty_posix>(m_stream_ptr);
  stream_ptr_pty_posix->on_success(error_code);
  if (error_code) {
    executor.set_error(error_code, "on_success");
  }
}

#endif

}  // namespace pty

#ifndef _WIN32

namespace uid {

template <class Executor>
void handler::on_exec_setup(Executor& executor) const
{
  if (getuid() == m_user_info.m_uid && getgid() == m_user_info.m_gid) {
    return;
  }
  std::vector<gid_t> groups;
  groups.reserve(m_user_info.m_groups.size());
  for (auto group : m_user_info.m_groups) {
    groups.push_back(static_cast<gid_t>(group));
  }
  if (!groups.empty() && setgroups(static_cast<int>(groups.size()), groups.data()) == -1) {
    executor.set_error(std::error_code(errno, std::system_category()), "setgroups() failed");
    return;
  }
  if (setgid(m_user_info.m_gid) == -1) {
    executor.set_error(std::error_code(errno, std::system_category()), "setgid() failed");
    return;
  }
  if (setuid(m_user_info.m_uid) == -1) {
    executor.set_error(std::error_code(errno, std::system_category()), "setuid() failed");
    return;
  }
}

}  // namespace uid

#endif

}  // namespace process

}  // namespace detail
}  // namespace webtty
}  // namespace rstream
