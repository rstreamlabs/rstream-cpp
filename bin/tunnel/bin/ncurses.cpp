// See LICENSE file in the project root for license information.

#include "ncurses.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>

#include <ncurses.h>
#include <unistd.h>

#include <rstream/config.hpp>

#include "error.hpp"

class RSTREAM_GNUC_INTERNAL ncurses::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor);

  void async_run(async_run_completion_handler&& handler);

  void cancel();

  void join();

  void render_status(const rstream::tunnel::status_proxy& status);

  void render_new_connection(const rstream::io_rstrm::endpoint& endpoint);

 private:
  void run();

  void notify_ui();

  static void print_truncated(WINDOW* win, const char* text);

  static void print_wrapped(WINDOW* win, const char* text);

  static void render_header(WINDOW* win);

  static void render_status(WINDOW* win, const rstream::tunnel::status_proxy& status);

  static void render_connections(WINDOW* win, int scroll_pos, int selected, const std::vector<std::string>& connections);

  static void render_footer(WINDOW* win);

  static void render_screen(WINDOW* win, int scroll_pos, int selected, const rstream::tunnel::status_proxy& status, const std::vector<std::string>& connections);

  static const std::size_t g_max_connections = 20;

  executor_type m_executor;

  std::mutex m_mutex;

  std::condition_variable m_condition_variable;

  bool m_running;

  bool m_cancelled;

  std::shared_ptr<std::thread> m_thread;

  async_run_completion_handler m_handler;

  rstream::tunnel::status_proxy m_status;

  std::vector<std::string> m_connections;
};

ncurses::ncurses(const executor_type& executor)
    : io_object(executor)
{
  m_impl = std::make_shared<impl>(executor);
}

ncurses::~ncurses()
{
  join();
}

void ncurses::async_run(async_run_completion_handler&& handler)
{
  m_impl->async_run(std::move(handler));
}

void ncurses::cancel()
{
  m_impl->cancel();
}

void ncurses::join()
{
  m_impl->join();
}

void ncurses::render_status(const rstream::tunnel::status_proxy& status)
{
  m_impl->render_status(status);
}

void ncurses::render_new_connection(const rstream::io_rstrm::endpoint& endpoint)
{
  m_impl->render_new_connection(endpoint);
}

ncurses::impl::impl(const executor_type& executor)
    : m_executor(executor),
      m_running(false),
      m_cancelled(false)
{
}

void ncurses::impl::async_run(async_run_completion_handler&& handler)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_running || m_cancelled) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), boost::asio::error::operation_aborted);
  }
  else {
    m_running = true;
    m_handler.swap(handler);
    m_thread = std::make_shared<std::thread>(&impl::run, shared_from_this());
  }
}

void ncurses::impl::cancel()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_running   = false;
  m_cancelled = true;
  m_condition_variable.notify_one();
}

void ncurses::impl::join()
{
  std::shared_ptr<std::thread> thread;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_running   = false;
    m_cancelled = true;
    thread.swap(m_thread);
    m_condition_variable.notify_one();
  }
  if (thread) {
    thread->join();
  }
}

void ncurses::impl::render_status(const rstream::tunnel::status_proxy& status)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_status = status;
  m_condition_variable.notify_one();
}

void ncurses::impl::render_new_connection(const rstream::io_rstrm::endpoint& endpoint)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  {
    std::stringstream ss;
    auto now        = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms         = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    ss << "[date: "
       << std::put_time(std::gmtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count()
       << " UTC"
       << ", stream_id: "
       << endpoint.m_id_name.value_or("-")
       << ", source_ip: "
       << (endpoint.m_source_ip ? endpoint.m_source_ip.get().to_string() : "-")
       << "]";
    m_connections.push_back(ss.str());
  }
  if (m_connections.size() > g_max_connections) {
    m_connections.erase(m_connections.begin());
  }
  m_condition_variable.notify_one();
}

void ncurses::impl::run()
{
  boost::system::error_code error_code;
  try {
    const auto program_location = boost::filesystem::canonical(boost::dll::program_location());
    const auto terminfodb       = boost::filesystem::canonical(program_location.parent_path().parent_path() / "share" / "terminfo.db");
    if (boost::filesystem::exists(terminfodb)) {
      const auto env = std::getenv("TERMINFO");
      if (env == nullptr) {
        setenv("TERMINFO", terminfodb.string().c_str(), 1);
      }
    }
  }
  catch (...) {
  }
  SCREEN* screen = newterm(std::getenv("TERM"), stdout, stdin);
  if (screen == nullptr) {
    error_code = error::code::ncurses_terminal;
  }
  else {
    set_term(screen);
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    int max_row, max_col;
    WINDOW* win    = nullptr;
    int scroll_pos = 0, selected = 0;
    int ch = KEY_RESIZE;
    do {
      switch (ch) {
        case KEY_RESIZE:
          getmaxyx(stdscr, max_row, max_col);
          clearok(stdscr, TRUE);
          if (win != nullptr) {
            wresize(win, max_row - 2, max_col - 2);
            mvwin(win, 1, 1);
          }
          else {
            win = newwin(max_row - 2, max_col - 2, 1, 1);
          }
          wclear(stdscr);
          wrefresh(stdscr);
          break;
        case KEY_UP:
          if (selected > 0) {
            selected--;
            if (selected < scroll_pos) {
              scroll_pos = selected;
            }
          }
          break;
        case KEY_DOWN:
          if (selected < m_connections.size() - 1) {
            selected++;
            if (selected >= scroll_pos + g_max_connections) {
              scroll_pos = selected - g_max_connections + 1;
            }
          }
          break;
      }
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_running) {
          break;
        }
        render_screen(win, scroll_pos, selected, m_status, m_connections);
        m_condition_variable.wait_for(lock, std::chrono::milliseconds(10));
      }
    } while ((ch = getch()) != 'q' && ch != 'Q');
    if (win != nullptr) {
      delwin(win);
    }
    endwin();
    delscreen(screen);
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), error_code);
  }
  m_handler = nullptr;
}

void ncurses::impl::notify_ui()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_condition_variable.notify_one();
}

void ncurses::impl::print_truncated(WINDOW* win, const char* text)
{
  wprintw(win, "%.*s\n", getmaxx(win) - 1, text);
}

void ncurses::impl::print_wrapped(WINDOW* win, const char* text)
{
  const int len       = strlen(text);
  const int max_width = getmaxx(win) - 1;
  for (int i = 0; i < len; i += max_width) {
    wprintw(win, "%.*s\n", max_width, text + i);
  }
}

void ncurses::impl::render_header(WINDOW* win)
{
  print_wrapped(win, "rstream-tunnel - (https://rstream.io/) - serverless networking");
  wprintw(win, "\n");
  print_wrapped(win, "this program is part of rstream (https://rstream.io/download) and was created using rstream C++ SDK");
}

void ncurses::impl::render_status(WINDOW* win, const rstream::tunnel::status_proxy& status)
{
  print_truncated(win, ("version      : " + status.m_version.value_or("-")).c_str());
  print_truncated(win, ("update       : " + status.m_update.value_or("-")).c_str());
  print_truncated(win, ("status       : " + status.m_status.value_or("-")).c_str());
  print_truncated(win, ("plan         : " + status.m_plan.value_or("-")).c_str());
  print_truncated(win, ("region       : " + status.m_region.value_or("-")).c_str());
  print_truncated(win, ("tunnel ID    : " + status.m_tunnel_id.value_or("-")).c_str());
  print_truncated(win, ("forwarding   : " + status.m_forwarding.value_or("-")).c_str());
  print_truncated(win, ("forwarded    : " + status.m_forwarded.value_or("-")).c_str());
}

void ncurses::impl::render_connections(WINDOW* win, int scroll_pos, int selected, const std::vector<std::string>& connections)
{
  print_truncated(win, "incoming connections:");
  wprintw(win, "\n");
  if (connections.empty()) {
    print_truncated(win, "no connection");
  }
  else {
    for (int i = scroll_pos; i < scroll_pos + g_max_connections && i < connections.size(); ++i) {
      if (i == selected) {
        wattron(win, A_REVERSE);
        print_truncated(win, connections[i].c_str());
        wattroff(win, A_REVERSE);
      }
      else {
        print_truncated(win, connections[i].c_str());
      }
    }
  }
}

void ncurses::impl::render_footer(WINDOW* win)
{
  wmove(win, getmaxy(win) - 2, 0);
  wclrtoeol(win);
  wmove(win, getmaxy(win) - 1, 0);
  wclrtoeol(win);
  mvwprintw(win, getmaxy(win) - 1, 0, "press 'q' or 'Ctrl-C' to exit");
}

void ncurses::impl::render_screen(WINDOW* win, int scroll_pos, int selected, const rstream::tunnel::status_proxy& status, const std::vector<std::string>& connections)
{
  werase(win);
  render_header(win);
  wprintw(win, "\n");
  render_status(win, status);
  wprintw(win, "\n");
  render_connections(win, scroll_pos, selected, connections);
  render_footer(win);
  wrefresh(win);
}
