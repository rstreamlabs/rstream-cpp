// See LICENSE file in the project root for license information.

#ifdef _MSC_VER
// MSVC can flag Asio's buffer conversion as unreachable after inlining.
#pragma warning(push)
#pragma warning(disable : 4702)
#include <boost/asio/buffer.hpp>
#pragma warning(pop)
#endif

#include <cassert>
#include <csignal>
#include <stdexcept>
#include <string>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <webtty_discovery.hpp>

namespace cli = rstream::webtty::cli;

template <typename F>
void expect_error(F&& call, const std::string& message)
{
  try {
    call();
  }
  catch (const std::runtime_error& error) {
    assert(std::string(error.what()).find(message) != std::string::npos);
    return;
  }
  throw std::runtime_error("expected discovery error: " + message);
}

void check_engine_discovery_io(const std::string& mode)
{
  boost::asio::io_context context;
  boost::asio::signal_set signals(context, SIGINT, SIGTERM);
  boost::asio::ip::tcp::acceptor acceptor(context, {boost::asio::ip::make_address("127.0.0.1"), 0});
  boost::asio::ip::tcp::socket peer(context);
  boost::beast::flat_buffer buffer(16 * 1024);
  boost::beast::http::request<boost::beast::http::empty_body> request;
  const std::string body     = R"([{"id":"shell","status":"online","protocol":"webtty","labels":{"rstream.webtty.transport":"plain"}}])";
  const std::string response = mode == "oversized" ? "HTTP/1.1 200 OK\r\nContent-Length: 2097152\r\n\r\n" : "HTTP/1.1 " + std::string(mode == "denied" ? "403 Forbidden" : "200 OK") + "\r\nContent-Length: " + std::to_string(mode == "malformed" ? 1 : body.size()) + "\r\nConnection: close\r\n\r\n" + (mode == "malformed" ? "{" : body);
  bool observed              = false;
  acceptor.async_accept(peer, [&](const boost::system::error_code& error) {
    assert(!error);
    boost::beast::http::async_read(peer, buffer, request, [&](const boost::system::error_code& error, std::size_t) {
      assert(!error);
      assert(request.target() == "/api/tunnels");
      assert(request[boost::beast::http::field::authorization].empty());
      observed = true;
      if (mode == "cancel") {
        std::raise(SIGINT);
        return;
      }
      if (mode == "timeout") {
        return;
      }
      boost::asio::async_write(peer, boost::asio::buffer(response), [&](const boost::system::error_code&, std::size_t) {
        boost::system::error_code ignored;
        peer.close(ignored);
      });
    });
  });
  boost::urls::url target("rstrm://shell");
  target.params().append({"server", "tcp://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port())});
  target.params().append({"rstream.no_token", "true"});
  const auto start = std::chrono::steady_clock::now();
  const auto run   = [&]() { return cli::discover_webtty_server(context, signals, rstream::io::address(target), "", std::chrono::milliseconds(200)); };
  if (mode == "ok") {
    const auto server = run();
    assert(server.m_transport == "plain");
    assert(server.m_target == "shell");
  }
  else {
    expect_error(run, mode == "denied" ? "rejected" : mode == "malformed" ? "invalid WebTTY engine inventory"
                                                                          : "discovery failed");
  }
  assert(observed);
  assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
  assert(context.poll() == 0);
}

void check_explicit_engine_requires_explicit_auth_before_io()
{
  boost::asio::io_context context;
  boost::asio::signal_set signals(context, SIGINT, SIGTERM);
  boost::asio::ip::tcp::acceptor acceptor(context, {boost::asio::ip::make_address("127.0.0.1"), 0});
  boost::urls::url target("rstrm://shell");
  target.params().append({"server", "tcp://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port())});
  expect_error([&] { cli::discover_webtty_server(context, signals, rstream::io::address(target), ""); }, "explicit engine URI requires an explicit token");
  assert(context.poll() == 0);
  acceptor.non_blocking(true);
  boost::asio::ip::tcp::socket peer(context);
  boost::system::error_code error;
  acceptor.accept(peer, error);
  assert(error == boost::asio::error::would_block || error == boost::asio::error::try_again);
}

int main()
{
  check_explicit_engine_requires_explicit_auth_before_io();
  for (const auto& mode : {"ok", "denied", "malformed", "oversized", "cancel", "timeout"}) {
    check_engine_discovery_io(mode);
  }
  for (const auto& transport : {"plain", "websocket", "webtransport"}) {
    for (const bool managed : {false, true}) {
      for (const bool publish : {false, true}) {
        const nlohmann::json server = {
            {"id", "tunnel"},
            {"name", "shell"},
            {"status", "online"},
            {"publish", publish},
            {"protocol", managed ? "webtty" : ""},
            {"type", std::string(transport) == "webtransport" ? "datagram" : "bytestream"},
            {"labels", {{"application-protocol", "rstream.webtty"}, {"rstream.webtty.transport", transport}, {"rstream.webtty.exec.path", "/terminal"}}},
        };
        const auto inventory = nlohmann::json::array({server});
        if (std::string(transport) == "webtransport") {
          expect_error([&]() { cli::select_discovered_server(inventory, "shell", ""); }, "not implemented");
          continue;
        }
        const auto selected = cli::select_discovered_server(inventory, "shell", "");
        assert(selected.m_transport == transport);
        assert(selected.m_target == "tunnel");
        assert(selected.m_exec_path == "/terminal");
        assert(!selected.m_requires_known_server);
        expect_error([&]() { cli::select_discovered_server(inventory, "shell", std::string(transport) == "plain" ? "websocket" : "plain"); }, "conflicts");
        expect_error([&]() { cli::select_discovered_server(nlohmann::json::array({server, server}), "shell", ""); }, "multiple");
      }
    }
  }
  nlohmann::json legacy = {{"id", "shell"}, {"status", "online"}, {"protocol", "webtty"}};
  assert(cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", "").m_transport == "websocket");
  assert(cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", "plain").m_transport == "plain");
  legacy["type"] = "datagram";
  expect_error([&]() { cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", ""); }, "not implemented");
  legacy["labels"] = {{"rstream.webtty.transport", "future"}};
  expect_error([&]() { cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", ""); }, "invalid WebTTY transport");
  legacy["labels"] = {{"rstream.webtty.transport", "websocket"}};
  expect_error([&]() { cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", ""); }, "conflicts");
  legacy["type"]                         = "bytestream";
  legacy["labels"]["rstream.webtty.e2e"] = "required";
  assert(cli::select_discovered_server(nlohmann::json::array({legacy}), "shell", "").m_requires_known_server);
}
