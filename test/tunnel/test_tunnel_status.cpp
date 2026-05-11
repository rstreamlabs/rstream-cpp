// See LICENSE file in the project root for license information.

#include <cassert>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <rstream/tunnel/error.hpp>
#include <rstream/tunnel/tunnel.hpp>

static void check_tunnel_error_messages()
{
  auto code = rstream::tunnel::error::make_error_code(rstream::tunnel::error::code::no_valid_endpoint);
  assert(code.category() == rstream::tunnel::error::rstream_tunnel_error_category());
  assert(code.message() == "no valid endpoint");
  assert(rstream::tunnel::to_string(rstream::tunnel::error::code::success) == "success");
  assert(rstream::tunnel::to_string(rstream::tunnel::error::code::invalid_state) == "invalid state");
  assert(rstream::tunnel::to_string(rstream::tunnel::error::code::operation_aborted) == "operation aborted");
  assert(rstream::tunnel::to_string(rstream::tunnel::error::code::operation_timeout) == "operation timeout");
  assert(rstream::tunnel::error::make_error_code(999).message() == "unknown error");
}

static void check_status_proxy_serialization()
{
  rstream::tunnel::status_proxy status;
  status.m_update     = "available";
  status.m_status     = "online";
  status.m_plan       = "pro";
  status.m_provider   = "rstream";
  status.m_region     = "eu";
  status.m_tunnel_id  = "tun_123";
  status.m_forwarding = "https://public.example";
  status.m_forwarded  = "127.0.0.1:8080";

  std::ostringstream out;
  out << status;
  assert(out.str().find("available") != std::string::npos);
  assert(out.str().find("https://public.example") != std::string::npos);
  assert(out.str().find("127.0.0.1:8080") != std::string::npos);

  nlohmann::json json = nlohmann::json::object();
  json << status;
  assert(json["status"] == "online");
  assert(json["tunnel_id"] == "tun_123");
  assert(json["forwarding"] == "https://public.example");
  assert(json["forwarded"] == "127.0.0.1:8080");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_tunnel_error_messages();
  check_status_proxy_serialization();
  return 0;
}
