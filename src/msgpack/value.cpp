#include "msgpack/value.h"

#include <format>
#include <limits>

namespace sailfish::msgpack {

namespace {

[[noreturn]] void wrong_type(const Value& v, std::string_view wanted) {
  throw TypeError(std::format("expected {}, got {} ({})", wanted, v.type_name(), v.to_debug_string()));
}

void append_debug(const Value& v, std::string& out, int depth) {
  constexpr int kMaxDepth = 6;
  if (depth > kMaxDepth) {
    out += "…";
    return;
  }
  std::visit(
    [&](const auto& x) {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, Nil>) {
        out += "nil";
      } else if constexpr (std::is_same_v<T, bool>) {
        out += x ? "true" : "false";
      } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, double>) {
        out += std::format("{}", x);
      } else if constexpr (std::is_same_v<T, std::string>) {
        out += std::format("\"{}\"", x);
      } else if constexpr (std::is_same_v<T, Binary>) {
        out += std::format("<bin {} bytes>", x.bytes.size());
      } else if constexpr (std::is_same_v<T, Ext>) {
        out += std::format("<ext {} {} bytes>", x.type, x.data.size());
      } else if constexpr (std::is_same_v<T, Array>) {
        out += '[';
        for (size_t i = 0; i < x.size(); ++i) {
          if (i) {
            out += ", ";
          }
          append_debug(x[i], out, depth + 1);
        }
        out += ']';
      } else if constexpr (std::is_same_v<T, Map>) {
        out += '{';
        for (size_t i = 0; i < x.size(); ++i) {
          if (i) {
            out += ", ";
          }
          append_debug(x[i].first, out, depth + 1);
          out += ": ";
          append_debug(x[i].second, out, depth + 1);
        }
        out += '}';
      }
    },
    v.storage());
}

} // namespace

bool Value::operator==(const Value& other) const {
  if (is_int() && other.is_int()) {
    const bool neg_a = std::holds_alternative<int64_t>(storage_) && std::get<int64_t>(storage_) < 0;
    const bool neg_b = std::holds_alternative<int64_t>(other.storage_) && std::get<int64_t>(other.storage_) < 0;
    if (neg_a || neg_b) {
      return neg_a && neg_b && std::get<int64_t>(storage_) == std::get<int64_t>(other.storage_);
    }
    const auto as_u = [](const Storage& s) {
      return std::holds_alternative<uint64_t>(s) ? std::get<uint64_t>(s) : static_cast<uint64_t>(std::get<int64_t>(s));
    };
    return as_u(storage_) == as_u(other.storage_);
  }
  return storage_ == other.storage_;
}

bool Value::is_int() const {
  return std::holds_alternative<int64_t>(storage_) || std::holds_alternative<uint64_t>(storage_);
}

bool Value::as_bool() const {
  if (const auto* b = std::get_if<bool>(&storage_)) {
    return *b;
  }
  wrong_type(*this, "bool");
}

int64_t Value::as_int() const {
  if (const auto* i = std::get_if<int64_t>(&storage_)) {
    return *i;
  }
  if (const auto* u = std::get_if<uint64_t>(&storage_)) {
    if (*u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      wrong_type(*this, "int64-sized integer");
    }
    return static_cast<int64_t>(*u);
  }
  wrong_type(*this, "integer");
}

const std::string& Value::as_string() const {
  if (const auto* s = std::get_if<std::string>(&storage_)) {
    return *s;
  }
  wrong_type(*this, "string");
}

const Array& Value::as_array() const& {
  if (const auto* a = std::get_if<Array>(&storage_)) {
    return *a;
  }
  wrong_type(*this, "array");
}

Array Value::as_array() && {
  if (auto* a = std::get_if<Array>(&storage_)) {
    return std::move(*a);
  }
  wrong_type(*this, "array");
}

const Map& Value::as_map() const {
  if (const auto* m = std::get_if<Map>(&storage_)) {
    return *m;
  }
  wrong_type(*this, "map");
}

const Ext& Value::as_ext() const {
  if (const auto* e = std::get_if<Ext>(&storage_)) {
    return *e;
  }
  wrong_type(*this, "ext");
}

const Value* Value::find(std::string_view key) const {
  for (const auto& [k, v] : as_map()) {
    if (k.is_string() && k.as_string() == key) {
      return &v;
    }
  }
  return nullptr;
}

std::string Value::type_name() const {
  static constexpr const char* kNames[] = {"nil", "bool", "int", "uint", "float", "string", "binary", "array", "map", "ext"};
  return kNames[storage_.index()];
}

std::string Value::to_debug_string() const {
  std::string out;
  append_debug(*this, out, 0);
  constexpr size_t kMaxLength = 400;
  if (out.size() > kMaxLength) {
    out.resize(kMaxLength);
    out += "…";
  }
  return out;
}

} // namespace sailfish::msgpack
