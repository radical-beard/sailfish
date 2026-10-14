#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>
#include <vector>

#include "input/key_encoding.h"
#include "text/utf8.h"

using namespace sailfish::input;

namespace {

std::vector<uint32_t> decode_all(std::string_view utf8) {
  std::vector<uint32_t> code_points;
  for (size_t pos = 0; pos < utf8.size();) {
    code_points.push_back(sailfish::text::decode_utf8(utf8, pos));
  }
  return code_points;
}

} // namespace

TEST_CASE("named keys") {
  CHECK(encode_named(NamedKey::Escape, {}) == "<Esc>");
  CHECK(encode_named(NamedKey::Enter, {}) == "<CR>");
  CHECK(encode_named(NamedKey::Enter, {.shift = true}) == "<S-CR>");
  CHECK(encode_named(NamedKey::Tab, {.shift = true}) == "<S-Tab>");
  CHECK(encode_named(NamedKey::PageDown, {}) == "<PageDown>");
  CHECK(encode_named(NamedKey::F5, {.ctrl = true}) == "<C-F5>");
  CHECK(encode_named(NamedKey::F24, {.shift = true}) == "<S-F24>");
  CHECK(encode_named(NamedKey::Left, {.alt = true, .super = true}) == "<M-D-Left>");
  CHECK(encode_named(NamedKey::Space, {}) == " ");
  CHECK(encode_named(NamedKey::Space, {.ctrl = true}) == "<C-Space>");
}

TEST_CASE("chords GUIs can send but terminals cannot") {
  CHECK(encode_chord('i', 'I', {.ctrl = true}) == "<C-i>");  // distinct from <Tab>
  CHECK(encode_chord('a', 'A', {.ctrl = true, .shift = true}) == "<C-S-a>");
  CHECK(encode_chord('s', 'S', {.super = true}) == "<D-s>");
  CHECK(encode_chord('c', 'C', {.super = true}) == "<D-c>");
  CHECK(encode_chord('a', 'A', {.super = true, .shift = true}) == "<D-A>");
  CHECK(encode_chord('j', 'J', {.alt = true}) == "<M-j>");
  CHECK(encode_chord('j', 'J', {.alt = true, .shift = true}) == "<M-J>");
}

TEST_CASE("shifted symbols and special characters") {
  CHECK(encode_chord('2', '@', {.shift = true, .ctrl = true}) == "<C-@>");
  CHECK(encode_chord('/', '?', {.shift = true, .super = true}) == "<D-?>");
  CHECK(encode_chord(',', '<', {.shift = true, .ctrl = true}) == "<C-lt>");
  CHECK(encode_chord('\\', '|', {.ctrl = true}) == "<C-Bslash>");
  CHECK(encode_chord('\\', '|', {.ctrl = true, .shift = true}) == "<C-Bar>");
  CHECK(encode_chord('[', '{', {.ctrl = true}) == "<C-[>");
}

TEST_CASE("typed text escapes only '<'") {
  CHECK(encode_text("hello") == "hello");
  CHECK(encode_text("a<b") == "a<lt>b");
  CHECK(encode_text("é漢") == "é漢");
}

TEST_CASE("UTF-8 decoding turns every malformed sequence into U+FFFD") {
  constexpr uint32_t R = sailfish::text::kReplacement;
  CHECK(decode_all("a\xc3\xa9\xe6\xbc\xa2\xf0\x9f\x98\x80") == std::vector<uint32_t>{'a', 0xe9, 0x6f22, 0x1f600});
  CHECK(decode_all("\xc0\x80") == std::vector<uint32_t>{R, R});                  // overlong NUL
  CHECK(decode_all("\xe0\x80\xaf") == std::vector<uint32_t>{R, R, R});           // overlong '/'
  CHECK(decode_all("\xed\xa0\x80") == std::vector<uint32_t>{R, R, R});           // UTF-16 surrogate
  CHECK(decode_all("\xf4\x90\x80\x80") == std::vector<uint32_t>{R, R, R, R});    // beyond U+10FFFF
  CHECK(decode_all("\xe6\xbc") == std::vector<uint32_t>{R, R});                  // truncated
}
