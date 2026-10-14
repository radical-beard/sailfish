#include "text/utf8.h"

namespace sailfish::text {

char32_t decode_utf8(std::string_view s, size_t& pos) {
  // The smallest code point each sequence length may carry; less is an overlong form.
  static constexpr char32_t kMinimum[] = {0, 0, 0x80, 0x800, 0x10000};
  const auto byte = [&](size_t i) { return static_cast<unsigned char>(s[i]); };
  const auto malformed = [&] {
    ++pos;
    return kReplacement;
  };
  const unsigned char lead = byte(pos);
  int length = 0;
  char32_t cp = 0;
  if (lead < 0x80) {
    ++pos;
    return lead;
  } else if ((lead & 0xe0) == 0xc0) {
    length = 2;
    cp = lead & 0x1f;
  } else if ((lead & 0xf0) == 0xe0) {
    length = 3;
    cp = lead & 0x0f;
  } else if ((lead & 0xf8) == 0xf0) {
    length = 4;
    cp = lead & 0x07;
  } else {
    return malformed();
  }
  if (pos + static_cast<size_t>(length) > s.size()) {
    return malformed();
  }
  for (int i = 1; i < length; ++i) {
    const unsigned char c = byte(pos + static_cast<size_t>(i));
    if ((c & 0xc0) != 0x80) {
      return malformed();
    }
    cp = (cp << 6) | (c & 0x3f);
  }
  if (cp < kMinimum[length] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
    return malformed();
  }
  pos += static_cast<size_t>(length);
  return cp;
}

void append_utf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xc0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xe0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  } else {
    out += static_cast<char>(0xf0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out += static_cast<char>(0x80 | (cp & 0x3f));
  }
}

} // namespace sailfish::text
