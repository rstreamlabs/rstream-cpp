// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>

#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>

namespace rstream::webtty::cli {

struct discovered_server {
  std::string m_transport;
  std::string m_target;
  std::string m_exec_path;
  bool m_requires_known_server = false;
};

inline std::string inventory_string(const nlohmann::json& object, const char* key, const char* fallback = "")
{
  const auto field = object.find(key);
  if (field == object.end() || field->is_null()) {
    return fallback;
  }
  if (!field->is_string()) {
    throw std::runtime_error("invalid WebTTY engine inventory field " + std::string(key));
  }
  return field->get<std::string>();
}

inline discovered_server select_discovered_server(const nlohmann::json& inventory, const std::string& target, const std::string& requested)
{
  if (!inventory.is_array()) {
    throw std::runtime_error("invalid WebTTY engine inventory");
  }
  std::optional<discovered_server> match;
  for (const auto& server : inventory) {
    if (!server.is_object() || inventory_string(server, "status", "") != "online") {
      continue;
    }
    const auto labels_entry = server.find("labels");
    const auto labels       = labels_entry == server.end() || labels_entry->is_null() ? nlohmann::json::object() : *labels_entry;
    if (!labels.is_object()) {
      throw std::runtime_error("invalid WebTTY engine inventory labels");
    }
    if (inventory_string(server, "protocol", "") != "webtty" && inventory_string(labels, "application-protocol", "") != "rstream.webtty") {
      continue;
    }
    const auto id        = inventory_string(server, "id", "");
    const auto name      = inventory_string(server, "name", "");
    const auto server_id = inventory_string(labels, "rstream.webtty.server_id", "");
    if (target != id && target != name && target != server_id && target != inventory_string(labels, "rstream.webtty.server_name", "")) {
      continue;
    }
    if (match) {
      throw std::runtime_error("multiple WebTTY servers match; use the tunnel ID");
    }
    const auto kind         = inventory_string(server, "type", "");
    const auto http_version = inventory_string(server, "http_version", "");
    const bool advertised   = labels.contains("rstream.webtty.transport");
    const auto transport    = advertised ? inventory_string(labels, "rstream.webtty.transport") : kind == "datagram" ? "webtransport"
                                                                                              : requested.empty()    ? "websocket"
                                                                                                                     : requested;
    if (transport != "plain" && transport != "websocket" && transport != "webtransport") {
      throw std::runtime_error("server advertises an invalid WebTTY transport");
    }
    if (!kind.empty() && (transport == "webtransport") != (kind == "datagram")) {
      throw std::runtime_error("server WebTTY transport conflicts with its tunnel type");
    }
    if (transport == "webtransport" && !http_version.empty() && http_version != "h3") {
      throw std::runtime_error("server WebTransport requires HTTP/3");
    }
    if (!requested.empty() && requested != transport) {
      throw std::runtime_error("requested WebTTY transport conflicts with server transport " + transport);
    }
    if (transport == "webtransport") {
      throw std::runtime_error("server requires WebTransport, which is not implemented by the C++ WebTTY client; use the Go client");
    }
    match = discovered_server{
        transport,
        server_id.empty() ? id : server_id,
        inventory_string(labels, "rstream.webtty.exec.path", "/"),
        inventory_string(labels, "rstream.webtty.e2e", "") == "required" || inventory_string(labels, "rstream.webtty.client_proof", "") == "required" || !inventory_string(labels, "rstream.webtty.host_key_id", "").empty(),
    };
  }
  if (!match) {
    throw std::runtime_error("online WebTTY server not found in engine inventory; for stream-only tokens use --no-discovery with --transport and local security");
  }
  return *match;
}

inline discovered_server discover_webtty_server(boost::asio::io_context& context, boost::asio::signal_set& signals, const io::address& target, const std::string& requested, std::chrono::steady_clock::duration timeout = std::chrono::seconds(5))
{
  io_rstrm::settings_socket settings;
  boost::system::error_code error;
  io_rstrm::parse_settings_socket(target.m_url, settings, error);
  if (error) {
    throw boost::system::system_error(error);
  }
  const auto endpoint = io_rstrm::make_endpoint(target.m_url).value();
  if (endpoint.m_server_address_from_uri_param && !settings.m_config.m_no_token && !settings.m_config.m_token_from_uri_param) {
    throw std::runtime_error("an explicit engine URI requires an explicit token or no-token option");
  }
  auto engine      = endpoint.m_server_address;
  const auto token = io_rstrm::get_rstream_token(settings.m_config, engine).value();
  engine.m_url.params().erase("ssl.alpn_protos");
  engine.m_url.params().append({"ssl.alpn_protos", "http/1.1"});
  io::stream::resolver resolver(context.get_executor());
  io::stream::socket socket(context.get_executor());
  boost::asio::steady_timer deadline(context, timeout);
  boost::beast::flat_buffer buffer(64 * 1024);
  boost::beast::http::response_parser<boost::beast::http::string_body> response;
  response.body_limit(1024 * 1024);
  response.header_limit(16 * 1024);
  boost::beast::http::request<boost::beast::http::empty_body> request(boost::beast::http::verb::get, "/api/tunnels", 11);
  const auto sni = engine.m_url.params().find("ssl.sni");
  request.set(boost::beast::http::field::host, sni == engine.m_url.params().end() ? engine.host() : std::string((*sni).value));
  request.set(boost::beast::http::field::connection, "close");
  if (token) {
    request.set(boost::beast::http::field::authorization, "Bearer " + *token);
  }
  bool done         = false;
  const auto finish = [&](const boost::system::error_code& result) {
    if (done) {
      return;
    }
    done  = true;
    error = result;
    resolver.cancel();
    boost::system::error_code ignored;
    socket.close(ignored);
    deadline.cancel();
    signals.cancel(ignored);
  };
  deadline.async_wait([&](const boost::system::error_code& result) {
    if (!result) {
      finish(boost::asio::error::timed_out);
    }
  });
  signals.async_wait([&](const boost::system::error_code& result, int) {
    if (!result) {
      finish(boost::asio::error::operation_aborted);
    }
  });
  resolver.async_resolve(engine.m_url, [&](const boost::system::error_code& result, const auto& endpoints) {
    if (result || done) {
      finish(result);
      return;
    }
    boost::asio::async_connect(socket, endpoints, [&](const boost::system::error_code& result, const auto&) {
      if (result || done) {
        finish(result);
        return;
      }
      boost::beast::http::async_write(socket, request, [&](const boost::system::error_code& result, std::size_t) {
        if (result || done) {
          finish(result);
          return;
        }
        boost::beast::http::async_read(socket, buffer, response, [&](const boost::system::error_code& result, std::size_t) { finish(result); });
      });
    });
  });
  context.run();
  context.restart();
  if (error) {
    throw std::runtime_error("WebTTY engine discovery failed: " + error.message());
  }
  if (response.get().result() != boost::beast::http::status::ok) {
    throw std::runtime_error("WebTTY engine discovery was rejected; use --no-discovery with --transport and local security for stream-only tokens");
  }
  const auto inventory = nlohmann::json::parse(response.get().body(), nullptr, false);
  return select_discovered_server(inventory, std::string(target.m_url.host()), requested);
}

}  // namespace rstream::webtty::cli
