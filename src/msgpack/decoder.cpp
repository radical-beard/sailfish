#include "msgpack/decoder.h"

#include <bit>
#include <format>

namespace sailfish::msgpack {

namespace {

// Neovim's messages nest a handful of containers deep. Refusing anything
// deeper keeps parse_object's recursion far inside a thread's stack.
constexpr int kMaxNesting = 128;

// Wire layout of one msgpack header byte: how many bytes follow the type byte
// before the payload (or element list), and how the length is found.
struct Header {
  enum class Kind { Nil, False, True, PosFix, NegFix, Uint, Int, Float32, Float64, Str, Bin, Array, Map, Ext, FixExt };
  Kind kind;
  int length_bytes = 0;   // width of the big-endian length/value field
  uint64_t inline_value = 0;  // fix* formats carry their value/length in the type byte
};

Header classify(uint8_t b) {
  using K = Header::Kind;
  if (b <= 0x7f) return {K::PosFix, 0, b};
  if (b <= 0x8f) return {K::Map, 0, static_cast<uint64_t>(b & 0x0f)};
  if (b <= 0x9f) return {K::Array, 0, static_cast<uint64_t>(b & 0x0f)};
  if (b <= 0xbf) return {K::Str, 0, static_cast<uint64_t>(b & 0x1f)};
  if (b >= 0xe0) return {K::NegFix, 0, b};
  switch (b) {
    case 0xc0: return {K::Nil};
    case 0xc2: return {K::False};
    case 0xc3: return {K::True};
    case 0xc4: return {K::Bin, 1};
    case 0xc5: return {K::Bin, 2};
    case 0xc6: return {K::Bin, 4};
    case 0xc7: return {K::Ext, 1};
    case 0xc8: return {K::Ext, 2};
    case 0xc9: return {K::Ext, 4};
    case 0xca: return {K::Float32, 4};
    case 0xcb: return {K::Float64, 8};
    case 0xcc: return {K::Uint, 1};
    case 0xcd: return {K::Uint, 2};
    case 0xce: return {K::Uint, 4};
    case 0xcf: return {K::Uint, 8};
    case 0xd0: return {K::Int, 1};
    case 0xd1: return {K::Int, 2};
    case 0xd2: return {K::Int, 4};
    case 0xd3: return {K::Int, 8};
    case 0xd4: return {K::FixExt, 0, 1};
    case 0xd5: return {K::FixExt, 0, 2};
    case 0xd6: return {K::FixExt, 0, 4};
    case 0xd7: return {K::FixExt, 0, 8};
    case 0xd8: return {K::FixExt, 0, 16};
    case 0xd9: return {K::Str, 1};
    case 0xda: return {K::Str, 2};
    case 0xdb: return {K::Str, 4};
    case 0xdc: return {K::Array, 2};
    case 0xdd: return {K::Array, 4};
    case 0xde: return {K::Map, 2};
    case 0xdf: return {K::Map, 4};
    default: throw DecodeError(std::format("invalid msgpack type byte 0x{:02x}", b));
  }
}

class Cursor {
public:
  explicit Cursor(std::string_view bytes) : bytes_(bytes) {}

  [[nodiscard]] size_t position() const { return pos_; }
  [[nodiscard]] bool has(size_t n) const { return bytes_.size() - pos_ >= n; }

  uint8_t byte() { return static_cast<uint8_t>(bytes_[pos_++]); }

  uint64_t big_endian(int width) {
    uint64_t v = 0;
    for (int i = 0; i < width; ++i) {
      v = (v << 8) | byte();
    }
    return v;
  }

  std::string_view take(size_t n) {
    auto s = bytes_.substr(pos_, n);
    pos_ += n;
    return s;
  }

  void skip(size_t n) { pos_ += n; }

private:
  std::string_view bytes_;
  size_t pos_ = 0;
};

// Length of the object's own payload (strings/bin/ext bytes) or its element
// count (array/map), read after the type byte. Returns false if truncated.
bool read_length(Cursor& c, const Header& h, uint64_t& out) {
  if (h.length_bytes == 0) {
    out = h.inline_value;
    return true;
  }
  if (!c.has(static_cast<size_t>(h.length_bytes))) {
    return false;
  }
  out = c.big_endian(h.length_bytes);
  return true;
}

// Consumes one object's own bytes: its header, plus the payload of anything
// that isn't a container. Returns how many element objects follow it (array
// items, map keys and values), or nullopt if `c` ends first.
std::optional<uint64_t> skip_header(Cursor& c) {
  using K = Header::Kind;
  if (!c.has(1)) {
    return std::nullopt;
  }
  const Header h = classify(c.byte());
  uint64_t n = 0;
  switch (h.kind) {
    case K::Nil:
    case K::False:
    case K::True:
    case K::PosFix:
    case K::NegFix:
      return 0;
    case K::Uint:
    case K::Int:
    case K::Float32:
    case K::Float64:
      if (!c.has(static_cast<size_t>(h.length_bytes))) return std::nullopt;
      c.skip(static_cast<size_t>(h.length_bytes));
      return 0;
    case K::Str:
    case K::Bin:
      if (!read_length(c, h, n) || !c.has(n)) return std::nullopt;
      c.skip(n);
      return 0;
    case K::Ext:
      if (!read_length(c, h, n) || !c.has(n + 1)) return std::nullopt;
      c.skip(n + 1);
      return 0;
    case K::FixExt:
      if (!c.has(h.inline_value + 1)) return std::nullopt;
      c.skip(h.inline_value + 1);
      return 0;
    case K::Array:
      if (!read_length(c, h, n)) return std::nullopt;
      return n;
    case K::Map:
      if (!read_length(c, h, n)) return std::nullopt;
      return 2 * n;
  }
  throw DecodeError("unreachable msgpack header kind");
}

// Parses one object the caller measured as complete; `depth` counts the
// containers around it.
Value parse_object(Cursor& c, int depth) {
  using K = Header::Kind;
  if (depth > kMaxNesting) {
    throw NestingTooDeep(std::format("msgpack object nested more than {} containers deep", kMaxNesting));
  }
  const Header h = classify(c.byte());
  uint64_t n = 0;
  switch (h.kind) {
    case K::Nil: return Value();
    case K::False: return Value(false);
    case K::True: return Value(true);
    case K::PosFix: return Value(static_cast<uint64_t>(h.inline_value));
    case K::NegFix: return Value(static_cast<int64_t>(static_cast<int8_t>(h.inline_value)));
    case K::Uint: return Value(c.big_endian(h.length_bytes));
    case K::Int: {
      const uint64_t raw = c.big_endian(h.length_bytes);
      switch (h.length_bytes) {
        case 1: return Value(static_cast<int64_t>(static_cast<int8_t>(raw)));
        case 2: return Value(static_cast<int64_t>(static_cast<int16_t>(raw)));
        case 4: return Value(static_cast<int64_t>(static_cast<int32_t>(raw)));
        default: return Value(static_cast<int64_t>(raw));
      }
    }
    case K::Float32: return Value(static_cast<double>(std::bit_cast<float>(static_cast<uint32_t>(c.big_endian(4)))));
    case K::Float64: return Value(std::bit_cast<double>(c.big_endian(8)));
    case K::Str:
      read_length(c, h, n);
      return Value(std::string(c.take(n)));
    case K::Bin:
      read_length(c, h, n);
      return Value(Binary{std::string(c.take(n))});
    case K::Ext: {
      read_length(c, h, n);
      const auto type = static_cast<int8_t>(c.byte());
      return Value(Ext{type, std::string(c.take(n))});
    }
    case K::FixExt: {
      const auto type = static_cast<int8_t>(c.byte());
      return Value(Ext{type, std::string(c.take(h.inline_value))});
    }
    case K::Array: {
      read_length(c, h, n);
      Array items;
      items.reserve(n);
      for (uint64_t i = 0; i < n; ++i) {
        items.push_back(parse_object(c, depth + 1));
      }
      return Value(std::move(items));
    }
    case K::Map: {
      read_length(c, h, n);
      Map entries;
      entries.reserve(n);
      for (uint64_t i = 0; i < n; ++i) {
        Value key = parse_object(c, depth + 1);
        Value value = parse_object(c, depth + 1);
        entries.emplace_back(std::move(key), std::move(value));
      }
      return Value(std::move(entries));
    }
  }
  throw DecodeError("unreachable msgpack header kind");
}

} // namespace

Value decode(std::string_view bytes) {
  StreamDecoder decoder;
  decoder.feed(bytes);
  std::optional<Value> v = decoder.next();
  if (!v) {
    throw DecodeError("truncated msgpack object");
  }
  if (decoder.buffered() != 0) {
    throw DecodeError(std::format("{} trailing bytes after msgpack object", decoder.buffered()));
  }
  return std::move(*v);
}

void StreamDecoder::feed(std::string_view bytes) {
  // Reclaim consumed space before growing, so the buffer stays proportional
  // to the largest in-flight object rather than to the stream's lifetime.
  if (pos_ > 0 && pos_ * 2 >= buf_.size()) {
    buf_.erase(0, pos_);
    pos_ = 0;
  }
  buf_.append(bytes);
}

std::optional<Value> StreamDecoder::next() {
  if (!measure()) {
    return std::nullopt;
  }
  Cursor c(std::string_view(buf_).substr(pos_, measured_));
  pos_ += measured_;
  measured_ = 0;
  unmeasured_ = 1;
  return parse_object(c, 0);
}

// Measuring needs no stack: an object is complete once every element its
// containers announced has been measured.
bool StreamDecoder::measure() {
  Cursor c(std::string_view(buf_).substr(pos_));
  c.skip(measured_);
  while (unmeasured_ > 0) {
    const std::optional<uint64_t> elements = skip_header(c);
    if (!elements) {
      return false;
    }
    unmeasured_ = unmeasured_ - 1 + *elements;
    measured_ = c.position();
  }
  return true;
}

} // namespace sailfish::msgpack
