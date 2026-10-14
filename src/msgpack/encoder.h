#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "msgpack/value.h"

namespace sailfish::msgpack {

// Appends msgpack encodings to an owned byte buffer, always choosing the
// smallest wire format for a value.
class Encoder {
public:
  void nil();
  void boolean(bool b);
  void integer(int64_t i);
  void uinteger(uint64_t u);
  void float64(double d);
  void string(std::string_view s);
  void binary(std::string_view bytes);
  void ext(int8_t type, std::string_view data);
  void array_header(uint32_t count);
  void map_header(uint32_t count);
  void value(const Value& v);

  [[nodiscard]] const std::string& bytes() const { return buf_; }
  [[nodiscard]] std::string take() { return std::move(buf_); }

private:
  void byte(uint8_t b) { buf_.push_back(static_cast<char>(b)); }
  void big_endian(uint64_t v, int width);

  std::string buf_;
};

[[nodiscard]] std::string encode(const Value& v);

} // namespace sailfish::msgpack
