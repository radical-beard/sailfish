#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "msgpack/decoder.h"
#include "msgpack/encoder.h"

using namespace sailfish::msgpack;

namespace {

Value roundtrip(const Value& v) { return decode(encode(v)); }

// `depth` arrays, each holding the next, around a nil.
std::string nested_arrays(int depth) { return std::string(static_cast<size_t>(depth), '\x91') + '\xc0'; }

} // namespace

TEST_CASE("scalars survive a roundtrip") {
  CHECK(roundtrip(Value()).is_nil());
  CHECK(roundtrip(Value(true)).as_bool());
  CHECK_FALSE(roundtrip(Value(false)).as_bool());
  for (int64_t i : {0LL, 1LL, 127LL, 128LL, 255LL, 256LL, 65535LL, 65536LL, 4294967296LL, -1LL, -32LL, -33LL, -128LL, -129LL,
         -32768LL, -32769LL, -2147483648LL, -2147483649LL}) {
    CHECK(roundtrip(Value(i)).as_int() == i);
  }
  CHECK(roundtrip(Value(3.25)) == Value(3.25));
  CHECK(roundtrip(Value("")).as_string().empty());
  CHECK(roundtrip(Value(std::string(40, 'x'))).as_string() == std::string(40, 'x'));
  CHECK(roundtrip(Value(std::string(300, 'y'))).as_string() == std::string(300, 'y'));
  CHECK(roundtrip(Value(std::string(70000, 'z'))).as_string().size() == 70000);
}

TEST_CASE("containers keep structure and order") {
  Value v(Array{1, "two", Array{3.5, Value()}, Map{{"b", 1}, {"a", 2}}});
  const Value back = roundtrip(v);
  CHECK(back == v);
  const auto& m = back.as_array()[3];
  CHECK(m.as_map()[0].first.as_string() == "b");
  CHECK(m.find("a")->as_int() == 2);
  CHECK(m.find("missing") == nullptr);

  Array big;
  for (int i = 0; i < 70000; ++i) {
    big.push_back(i);
  }
  CHECK(roundtrip(Value(big)).as_array().size() == 70000);
}

TEST_CASE("ext values keep their tag and payload") {
  const Value window(Ext{1, std::string("\xcd\x03\xe9", 3)});
  CHECK(roundtrip(window) == window);
}

TEST_CASE("wrong shapes throw TypeError") {
  CHECK_THROWS_AS((void)Value("x").as_int(), TypeError);
  CHECK_THROWS_AS((void)Value(1).as_string(), TypeError);
  CHECK_THROWS_AS((void)Value(Array{}).find("k"), TypeError);
}

TEST_CASE("stream decoder assembles objects split across chunks") {
  const std::string a = encode(Value(Array{2, "redraw", Array{Array{"flush"}}}));
  const std::string b = encode(Value(Array{1, 7, Value(), "ok"}));
  const std::string stream = a + b;

  StreamDecoder d;
  std::vector<Value> out;
  for (char ch : stream) {
    d.feed(std::string_view(&ch, 1));
    while (auto v = d.next()) {
      out.push_back(std::move(*v));
    }
  }
  REQUIRE(out.size() == 2);
  CHECK(out[0].as_array()[1].as_string() == "redraw");
  CHECK(out[1].as_array()[3].as_string() == "ok");
  CHECK(d.buffered() == 0);
}

TEST_CASE("an object fed one byte at a time is measured once, in linear time") {
  // Re-measuring from the first byte on every chunk would make this quadratic:
  // billions of steps for these few hundred kilobytes.
  Array events;
  for (int i = 0; i < 4000; ++i) {
    events.push_back(Array{"grid_line", Array{2, i, 0, Array{Array{std::string(40, 'x'), 7, 1}, Array{"y"}}, false}, Map{{"k", i}}});
  }
  const Value batch(Array{2, "redraw", std::move(events)});
  const std::string bytes = encode(batch);
  REQUIRE(bytes.size() > 200000);

  StreamDecoder d;
  std::vector<size_t> completed_at;
  std::optional<Value> decoded;
  for (size_t i = 0; i < bytes.size(); ++i) {
    d.feed(std::string_view(&bytes[i], 1));
    if (auto v = d.next()) {
      completed_at.push_back(i);
      decoded = std::move(v);
    }
  }
  CHECK(completed_at == std::vector<size_t>{bytes.size() - 1});
  REQUIRE(decoded);
  CHECK(*decoded == batch);
  CHECK(d.buffered() == 0);
}

TEST_CASE("objects nested past the cap are skipped without losing the stream") {
  CHECK_NOTHROW((void)decode(nested_arrays(128)));
  CHECK_THROWS_AS((void)decode(nested_arrays(129)), NestingTooDeep);

  StreamDecoder d;
  d.feed(nested_arrays(100000));
  d.feed(encode(Value("after")));
  CHECK_THROWS_AS((void)d.next(), NestingTooDeep);
  const std::optional<Value> after = d.next();
  REQUIRE(after);
  CHECK(after->as_string() == "after");
  CHECK(d.buffered() == 0);
}

TEST_CASE("garbage is rejected") {
  CHECK_THROWS_AS((void)decode(std::string("\xc1", 1)), DecodeError);
  CHECK_THROWS_AS((void)decode(std::string("\x92\x01", 2)), DecodeError);  // truncated
  CHECK_THROWS_AS((void)decode(std::string("\x01\x02", 2)), DecodeError);  // trailing
}
