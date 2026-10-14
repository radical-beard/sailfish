#pragma once
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sailfish::msgpack {

class Value;
using Array = std::vector<Value>;
// Maps keep wire order; Neovim's maps are small and order is useful in logs.
using Map = std::vector<std::pair<Value, Value>>;

struct Nil {
  bool operator==(const Nil&) const = default;
};

struct Binary {
  std::string bytes;
  bool operator==(const Binary&) const = default;
};

// msgpack's extension type: an application-defined tag and payload.
struct Ext {
  int8_t type = 0;
  std::string data;
  bool operator==(const Ext&) const = default;
};

// Thrown when a value has a different shape than the reader required. Inside
// the protocol boundary this means Neovim sent something we don't understand.
class TypeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Value {
public:
  using Storage = std::variant<Nil, bool, int64_t, uint64_t, double, std::string, Binary, Array, Map, Ext>;

  Value() = default;
  Value(Nil) {}
  Value(bool b) : storage_(b) {}
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  Value(T v) {
    if constexpr (std::is_signed_v<T>) {
      storage_ = static_cast<int64_t>(v);
    } else {
      storage_ = static_cast<uint64_t>(v);
    }
  }
  Value(double d) : storage_(d) {}
  Value(std::string s) : storage_(std::move(s)) {}
  Value(std::string_view s) : storage_(std::string(s)) {}
  Value(const char* s) : storage_(std::string(s)) {}
  Value(Array a) : storage_(std::move(a)) {}
  Value(Map m) : storage_(std::move(m)) {}
  Value(Binary b) : storage_(std::move(b)) {}
  Value(Ext e) : storage_(std::move(e)) {}

  [[nodiscard]] bool is_nil() const { return std::holds_alternative<Nil>(storage_); }
  [[nodiscard]] bool is_bool() const { return std::holds_alternative<bool>(storage_); }
  [[nodiscard]] bool is_int() const;
  [[nodiscard]] bool is_float() const { return std::holds_alternative<double>(storage_); }
  [[nodiscard]] bool is_string() const { return std::holds_alternative<std::string>(storage_); }
  [[nodiscard]] bool is_array() const { return std::holds_alternative<Array>(storage_); }
  [[nodiscard]] bool is_map() const { return std::holds_alternative<Map>(storage_); }
  [[nodiscard]] bool is_ext() const { return std::holds_alternative<Ext>(storage_); }

  [[nodiscard]] bool as_bool() const;
  // Accepts either wire signedness; throws if the value doesn't fit int64.
  [[nodiscard]] int64_t as_int() const;
  [[nodiscard]] const std::string& as_string() const;
  [[nodiscard]] const Array& as_array() const&;
  // Moves the elements out, so a decoded message's parts change hands without copies.
  [[nodiscard]] Array as_array() &&;
  [[nodiscard]] const Map& as_map() const;
  [[nodiscard]] const Ext& as_ext() const;

  // Map lookup by string key; nullptr when absent.
  [[nodiscard]] const Value* find(std::string_view key) const;

  [[nodiscard]] const Storage& storage() const { return storage_; }
  [[nodiscard]] std::string type_name() const;
  // Compact human-readable rendering for logs and error messages.
  [[nodiscard]] std::string to_debug_string() const;

  // Integers compare by numeric value: the wire format decides signedness, so
  // `1` sent by Neovim decodes as uint64 while `Value(1)` holds int64.
  bool operator==(const Value& other) const;

private:
  Storage storage_;
};

} // namespace sailfish::msgpack
