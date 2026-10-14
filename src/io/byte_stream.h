#pragma once
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>

namespace sailfish::io {

// The far end went away mid-write.
class StreamClosed : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// A full-duplex byte pipe (a child's stdio today, a socket for remote Neovim
// later). read() and write() may be called concurrently from two threads.
class ByteStream {
public:
  virtual ~ByteStream() = default;
  // Blocks until at least one byte is available. Returns 0 at end of stream.
  virtual size_t read(std::span<char> buffer) = 0;
  // Blocks until every byte is written; throws StreamClosed if the peer is gone.
  virtual void write(std::string_view bytes) = 0;
  // Signals end-of-input to the peer.
  virtual void close_write() = 0;
};

} // namespace sailfish::io
