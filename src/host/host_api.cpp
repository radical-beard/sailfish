#include "host/host_api.h"

#include <stdexcept>

#include "log/log.h"

namespace sailfish::host {

void HostApi::on_host_notification(nvim::Editor& from, std::string_view method, const msgpack::Array& params) {
  const auto it = notifications_.find(std::string(method));
  if (it == notifications_.end()) {
    log::warn("unknown host notification '{}'", method);
    return;
  }
  try {
    it->second(from, params);
  } catch (const msgpack::TypeError& e) {
    log::warn("bad arguments to '{}': {}", method, e.what());
  } catch (const std::out_of_range&) {
    log::warn("missing arguments to '{}'", method);
  }
}

void HostApi::on_host_request(nvim::Editor& from, std::string_view method, const msgpack::Array& params, rpc::Responder responder) {
  const auto it = requests_.find(std::string(method));
  if (it == requests_.end()) {
    responder.fail("Sailfish has no method " + std::string(method));
    return;
  }
  // Arguments come from user Lua: a shape error becomes an RPC error the
  // caller sees, rather than anything on our side.
  try {
    it->second(from, params, std::move(responder));
  } catch (const msgpack::TypeError& e) {
    log::warn("bad arguments to '{}': {}", method, e.what());
  } catch (const std::out_of_range&) {
    log::warn("missing arguments to '{}'", method);
  }
}

} // namespace sailfish::host
