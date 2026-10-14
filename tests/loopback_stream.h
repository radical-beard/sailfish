#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

#include "io/byte_stream.h"

namespace sailfish::test {

// One direction of an in-memory pipe.
struct PipeBuffer {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<char> bytes;
  bool eof = false;
};

// An in-memory full-duplex stream; make_loopback() returns two connected ends.
class LoopbackStream final : public io::ByteStream {
public:
  LoopbackStream(std::shared_ptr<PipeBuffer> in, std::shared_ptr<PipeBuffer> out) : in_(std::move(in)), out_(std::move(out)) {}

  size_t read(std::span<char> buffer) override {
    std::unique_lock lock(in_->mutex);
    in_->cv.wait(lock, [&] { return !in_->bytes.empty() || in_->eof; });
    size_t n = 0;
    while (n < buffer.size() && !in_->bytes.empty()) {
      buffer[n++] = in_->bytes.front();
      in_->bytes.pop_front();
    }
    return n;
  }

  void write(std::string_view bytes) override {
    std::lock_guard lock(out_->mutex);
    if (out_->eof) {
      throw io::StreamClosed("loopback closed");
    }
    out_->bytes.insert(out_->bytes.end(), bytes.begin(), bytes.end());
    out_->cv.notify_all();
  }

  // Models a peer like `nvim --embed`, which exits when its input closes:
  // closing our write side also ends our read side.
  void close_write() override {
    for (const auto& buffer : {out_, in_}) {
      std::lock_guard lock(buffer->mutex);
      buffer->eof = true;
      buffer->cv.notify_all();
    }
  }

private:
  std::shared_ptr<PipeBuffer> in_;
  std::shared_ptr<PipeBuffer> out_;
};

inline std::pair<std::unique_ptr<LoopbackStream>, std::unique_ptr<LoopbackStream>> make_loopback() {
  auto a_to_b = std::make_shared<PipeBuffer>();
  auto b_to_a = std::make_shared<PipeBuffer>();
  return {std::make_unique<LoopbackStream>(b_to_a, a_to_b), std::make_unique<LoopbackStream>(a_to_b, b_to_a)};
}

} // namespace sailfish::test
