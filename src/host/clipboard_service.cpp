#include "host/clipboard_service.h"

#include <SDL3/SDL.h>

namespace sailfish::host {

namespace {

std::string read_clipboard() {
  char* raw = SDL_GetClipboardText();
  std::string text = raw ? raw : "";
  SDL_free(raw);
  std::erase(text, '\r');
  return text;
}

msgpack::Array split_lines(const std::string& text) {
  msgpack::Array lines;
  size_t start = 0;
  for (size_t nl = text.find('\n'); nl != std::string::npos; nl = text.find('\n', start)) {
    lines.emplace_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  if (start < text.size()) {
    lines.emplace_back(text.substr(start));
  }
  return lines;
}

} // namespace

void ClipboardService::register_methods(HostApi& api) {
  api.on_notification("sailfish.clipboard.set", [this](nvim::Editor&, const msgpack::Array& params) {
    const auto& lines = params.at(0).as_array();
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (i > 0) {
        text += '\n';
      }
      text += lines[i].as_string();
    }
    const std::string regtype = params.size() > 1 ? params[1].as_string() : "v";
    if (!regtype.empty() && regtype[0] == 'V') {
      text += '\n';
    }
    SDL_SetClipboardText(text.c_str());
    last_text_ = std::move(text);
    last_regtype_ = regtype;
  });

  api.on_request("sailfish.clipboard.get", [this](nvim::Editor&, const msgpack::Array&, rpc::Responder responder) {
    const std::string text = read_clipboard();
    std::string regtype = (!text.empty() && text.back() == '\n') ? "V" : "v";
    if (text == last_text_) {
      regtype = last_regtype_;
    }
    responder.ok(msgpack::Value(msgpack::Array{msgpack::Value(split_lines(text)), regtype}));
  });
}

} // namespace sailfish::host
