#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "nvim/editor.h"

namespace sailfish::host {

// The methods Lua can call on the host, by name:
//   vim.rpcrequest(vim.g.sailfish.channel, "sailfish.clipboard.get", "+")
// Each service registers its own methods at composition time; adding a host
// capability never touches this class or the instance plumbing.
class HostApi final : public nvim::HostSink {
public:
  using Request = std::function<void(nvim::Editor& from, const msgpack::Array& params, rpc::Responder responder)>;
  using Notification = std::function<void(nvim::Editor& from, const msgpack::Array& params)>;

  void on_request(std::string method, Request handler) { requests_[std::move(method)] = std::move(handler); }
  void on_notification(std::string method, Notification handler) { notifications_[std::move(method)] = std::move(handler); }

  void on_host_notification(nvim::Editor& from, std::string_view method, const msgpack::Array& params) override;
  void on_host_request(nvim::Editor& from, std::string_view method, const msgpack::Array& params, rpc::Responder responder) override;

private:
  std::unordered_map<std::string, Request> requests_;
  std::unordered_map<std::string, Notification> notifications_;
};

} // namespace sailfish::host
