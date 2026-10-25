// See LICENSE file in the project root for license information.

#include "process.hpp"

#include <cstdint>

#ifdef _WIN32
#include <boost/winapi/get_last_error.hpp>
#include <boost/winapi/process.hpp>
#else
#include <cerrno>
#include <csignal>

#include <sys/wait.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif

namespace rstream {
namespace webtty {
namespace detail {

namespace process {

#ifdef __APPLE__
static bool group_has_only_zombies(pid_t leader)
{
  // Keep this exceptional path bounded. A truncated or inaccessible snapshot
  // must preserve EPERM rather than hide a live process we could not signal.
  std::array<kinfo_proc, 256> members{};
  int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PGRP, leader};
  auto size   = sizeof(members);
  if (::sysctl(query, 4, members.data(), &size, nullptr, 0) != 0 || size > sizeof(members) || size % sizeof(kinfo_proc) != 0) {
    return false;
  }
  bool leader_found = false;
  for (std::size_t index = 0; index < size / sizeof(kinfo_proc); ++index) {
    const auto& member = members[index];
    if (member.kp_eproc.e_pgid != leader || member.kp_proc.p_stat != SZOMB) {
      return false;
    }
    leader_found = leader_found || member.kp_proc.p_pid == leader;
  }
  return leader_found;
}
#endif

group::group()
{
#ifdef _WIN32
  m_job = ::CreateJobObjectW(nullptr, nullptr);
  if (m_job == nullptr) {
    throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "CreateJobObject");
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!::SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, static_cast<DWORD>(sizeof(limits)))) {
    const auto error = ::GetLastError();
    ::CloseHandle(m_job);
    m_job = nullptr;
    throw std::system_error(static_cast<int>(error), std::system_category(), "SetInformationJobObject");
  }
#endif
}

group::~group() noexcept
{
#ifdef _WIN32
  if (m_job != nullptr) {
    ::CloseHandle(m_job);
  }
#endif
}

#ifdef _WIN32
void group::assign_and_resume(void* process, void* thread, std::error_code& error_code)
{
  if (!::AssignProcessToJobObject(m_job, process)) {
    error_code = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    return;
  }
  const auto previous = ::ResumeThread(thread);
  if (previous == static_cast<DWORD>(-1)) {
    error_code = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
  }
  else if (previous != 1) {
    error_code = std::make_error_code(std::errc::state_not_recoverable);
  }
}

void group::terminate(std::error_code& error_code)
{
  constexpr unsigned int interrupted_exit_code = 0xc000013aU;
  if (!::TerminateJobObject(m_job, interrupted_exit_code)) {
    error_code = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
  }
}
#endif

void request_termination(const boost::process::child& child, group& owner, bool using_pty, bool force, std::error_code& error_code)
{
  error_code.clear();
  if (owner.released()) {
    return;
  }
#ifdef _WIN32
  (void)using_pty;
  (void)force;
  (void)child;
  owner.terminate(error_code);
#else
  (void)owner;
  const auto signal = force ? SIGKILL : (using_pty ? SIGHUP : SIGINT);
  if (::kill(-child.id(), signal) != 0 && errno != ESRCH) {
    error_code = std::error_code(errno, std::system_category());
#ifdef __APPLE__
    // Darwin reports EPERM when every group member has already exited.
    if (error_code.value() == EPERM) {
      siginfo_t info{};
      if (::waitid(P_PID, static_cast<id_t>(child.id()), &info, WEXITED | WNOWAIT | WNOHANG) == 0 && info.si_pid == child.id()
          && (info.si_code == CLD_EXITED || info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED)
          && group_has_only_zombies(child.id())) {
        error_code.clear();
      }
    }
#endif
  }
#endif
}

#ifndef _WIN32
bool recover_exit(boost::process::child& child, group& owner, std::error_code& error_code)
{
  siginfo_t info{};
  if (::waitid(P_PID, static_cast<id_t>(child.id()), &info, WEXITED | WNOWAIT | WNOHANG) != 0) {
    if (errno == ECHILD) {
      owner.release();
    }
    if (errno != EINTR) {
      error_code = std::error_code(errno, std::system_category());
    }
    return false;
  }
  if (info.si_pid == 0 || (info.si_code != CLD_EXITED && info.si_code != CLD_KILLED && info.si_code != CLD_DUMPED)) {
    return false;
  }
  // Keep the leader unreaped until its group has been stopped. Its PID cannot
  // be recycled into an unrelated process group during cleanup.
  request_termination(child, owner, false, true, error_code);
  if (error_code) {
    return false;
  }
  const auto exited = !child.running(error_code);
  if (exited) {
    owner.release();
  }
  return exited;
}
#endif

namespace pty {
handler::handler(std::shared_ptr<stream::pty> stream_ptr)
    : m_stream_ptr(stream_ptr)
{
}
}  // namespace pty

#ifndef _WIN32

namespace uid {
handler::handler(const protocol::user_info& user_info)
    : m_user_info(user_info)
{
}
}  // namespace uid

#endif

}  // namespace process

}  // namespace detail
}  // namespace webtty
}  // namespace rstream
