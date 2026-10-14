#include "msgpack/encoder.h"

#include <bit>
#include <limits>

namespace sailfish::msgpack {

void Encoder::big_endian(uint64_t v, int width) {
  for (int shift = (width - 1) * 8; shift >= 0; shift -= 8) {
    byte(static_cast<uint8_t>(v >> shift));
  }
}

void Encoder::nil() { byte(0xc0); }

void Encoder::boolean(bool b) { byte(b ? 0xc3 : 0xc2); }

void Encoder::integer(int64_t i) {
  if (i >= 0) {
    uinteger(static_cast<uint64_t>(i));
  } else if (i >= -32) {
    byte(static_cast<uint8_t>(i));
  } else if (i >= std::numeric_limits<int8_t>::min()) {
    byte(0xd0);
    big_endian(static_cast<uint8_t>(i), 1);
  } else if (i >= std::numeric_limits<int16_t>::min()) {
    byte(0xd1);
    big_endian(static_cast<uint16_t>(i), 2);
  } else if (i >= std::numeric_limits<int32_t>::min()) {
    byte(0xd2);
    big_endian(static_cast<uint32_t>(i), 4);
  } else {
    byte(0xd3);
    big_endian(static_cast<uint64_t>(i), 8);
  }
}

void Encoder::uinteger(uint64_t u) {
  if (u <= 0x7f) {
    byte(static_cast<uint8_t>(u));
  } else if (u <= 0xff) {
    byte(0xcc);
    big_endian(u, 1);
  } else if (u <= 0xffff) {
    byte(0xcd);
    big_endian(u, 2);
  } else if (u <= 0xffffffff) {
    byte(0xce);
    big_endian(u, 4);
  } else {
    byte(0xcf);
    big_endian(u, 8);
  }
}

void Encoder::float64(double d) {
  byte(0xcb);
  big_endian(std::bit_cast<uint64_t>(d), 8);
}

void Encoder::string(std::string_view s) {
  const auto n = s.size();
  if (n <= 31) {
    byte(static_cast<uint8_t>(0xa0 | n));
  } else if (n <= 0xff) {
    byte(0xd9);
    big_endian(n, 1);
  } else if (n <= 0xffff) {
    byte(0xda);
    big_endian(n, 2);
  } else {
    byte(0xdb);
    big_endian(n, 4);
  }
  buf_.append(s);
}

void Encoder::binary(std::string_view bytes) {
  const auto n = bytes.size();
  if (n <= 0xff) {
    byte(0xc4);
    big_endian(n, 1);
  } else if (n <= 0xffff) {
    byte(0xc5);
    big_endian(n, 2);
  } else {
    byte(0xc6);
    big_endian(n, 4);
  }
  buf_.append(bytes);
}

void Encoder::ext(int8_t type, std::string_view data) {
  const auto n = data.size();
  switch (n) {
    case 1: byte(0xd4); break;
    case 2: byte(0xd5); break;
    case 4: byte(0xd6); break;
    case 8: byte(0xd7); break;
    case 16: byte(0xd8); break;
    default:
      if (n <= 0xff) {
        byte(0xc7);
        big_endian(n, 1);
      } else if (n <= 0xffff) {
        byte(0xc8);
        big_endian(n, 2);
      } else {
        byte(0xc9);
        big_endian(n, 4);
      }
  }
  byte(static_cast<uint8_t>(type));
  buf_.append(data);
}

void Encoder::array_header(uint32_t count) {
  if (count <= 15) {
    byte(static_cast<uint8_t>(0x90 | count));
  } else if (count <= 0xffff) {
    byte(0xdc);
    big_endian(count, 2);
  } else {
    byte(0xdd);
    big_endian(count, 4);
  }
}

void Encoder::map_header(uint32_t count) {
  if (count <= 15) {
    byte(static_cast<uint8_t>(0x80 | count));
  } else if (count <= 0xffff) {
    byte(0xde);
    big_endian(count, 2);
  } else {
    byte(0xdf);
    big_endian(count, 4);
  }
}

void Encoder::value(const Value& v) {
  std::visit(
    [this](const auto& x) {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, Nil>) {
        nil();
      } else if constexpr (std::is_same_v<T, bool>) {
        boolean(x);
      } else if constexpr (std::is_same_v<T, int64_t>) {
        integer(x);
      } else if constexpr (std::is_same_v<T, uint64_t>) {
        uinteger(x);
      } else if constexpr (std::is_same_v<T, double>) {
        float64(x);
      } else if constexpr (std::is_same_v<T, std::string>) {
        string(x);
      } else if constexpr (std::is_same_v<T, Binary>) {
        binary(x.bytes);
      } else if constexpr (std::is_same_v<T, Ext>) {
        ext(x.type, x.data);
      } else if constexpr (std::is_same_v<T, Array>) {
        array_header(static_cast<uint32_t>(x.size()));
        for (const auto& item : x) {
          value(item);
        }
      } else if constexpr (std::is_same_v<T, Map>) {
        map_header(static_cast<uint32_t>(x.size()));
        for (const auto& [k, item] : x) {
          value(k);
          value(item);
        }
      }
    },
    v.storage());
}

std::string encode(const Value& v) {
  Encoder e;
  e.value(v);
  return e.take();
}

} // namespace sailfish::msgpack
