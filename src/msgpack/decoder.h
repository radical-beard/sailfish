#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "msgpack/value.h"

namespace sailfish::msgpack {

// The byte stream is not msgpack. Unrecoverable for the stream that produced it.
class DecodeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// A well-formed object nests containers deeper than the decoder parses. Its
// bytes have been consumed, so the stream carrying it stays usable.
class NestingTooDeep : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Decodes exactly one object spanning all of `bytes`.
[[nodiscard]] Value decode(std::string_view bytes);

// Incremental decoder for a byte stream carrying back-to-back objects (an RPC
// pipe). Bytes are fed as they arrive; complete objects are pulled out. Each
// pull resumes measuring the next object where the previous one stopped, and
// the object is parsed once, when complete, so the cost stays linear in its
// size however finely it arrives.
class StreamDecoder {
public:
  void feed(std::string_view bytes);
  // The next object, or nullopt until its last byte has arrived. Throws
  // NestingTooDeep (the object is skipped) or DecodeError.
  [[nodiscard]] std::optional<Value> next();
  [[nodiscard]] size_t buffered() const { return buf_.size() - pos_; }

private:
  [[nodiscard]] bool measure();

  std::string buf_;
  size_t pos_ = 0;           // first byte of the next object
  size_t measured_ = 0;      // its leading bytes whose structure is known
  uint64_t unmeasured_ = 1;  // objects in it (itself, then elements) not yet measured
};

} // namespace sailfish::msgpack
