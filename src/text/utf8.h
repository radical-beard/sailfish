#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace sailfish::text {

constexpr char32_t kReplacement = 0xfffd;

// Decodes the code point at `pos` and advances past it. Malformed input
// yields U+FFFD and advances one byte, so iteration always terminates.
char32_t decode_utf8(std::string_view s, size_t& pos);

void append_utf8(std::string& out, char32_t cp);

} // namespace sailfish::text
