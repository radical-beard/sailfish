#include "rpc/channel.h"

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>

#include "log/log.h"
#include "msgpack/decoder.h"
#include "msgpack/encoder.h"

namespace sailfish::rpc {

namespace {

constexpr int64_t kRequest = 0;
constexpr int64_t kResponse = 1;
constexpr int64_t kNotification = 2;

std::string encode_request(uint32_t id, std::string_view method, msgpack::Array params) {
  return msgpack::encode(msgpack::Value(msgpack::Array{kRequest, id, method, std::move(params)}));
}

std::string encode_response(uint32_t id, const msgpack::Value& error, const msgpack::Value& result) {
  return msgpack::encode(msgpack::Value(msgpack::Array{kResponse, id, error, result}));
}

uint32_t parse_msgid(const msgpack::Value& v) {
  const int64_t id = v.as_int();
  if (id < 0 || id > std::numeric_limits<uint32_t>::max()) {
    throw msgpack::TypeError(std::format("msgpack-RPC message id {} is out of range", id));
  }
  return static_cast<uint32_t>(id);
}

Response parse_response(msgpack::Value error, msgpack::Value result) {
  Response r{std::move(result), std::nullopt};
  if (!error.is_nil()) {
    r.error = std::move(error);
  }
  return r;
}

} // namespace

// Serializes writes onto a dedicated thread, then ends the peer's input.
// Shared with Responders, which may outlive the Channel; once closed, sends
// are dropped.
class Outbox {
public:
  explicit Outbox(io::ByteStream& stream) : stream_(stream), writer_([this] { write_loop(); }) {}

  ~Outbox() {
    close();
    writer_.join();
  }

  void send(std::string bytes) {
    {
      std::lock_guard lock(mutex_);
      if (closed_) {
        return;
      }
      queue_.push_back(std::move(bytes));
    }
    cv_.notify_one();
  }

  // The writer ends the peer's input once the queue drains.
  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    cv_.notify_one();
  }

private:
  void write_loop() {
    while (std::optional<std::string> bytes = next()) {
      try {
        stream_.write(*bytes);
      } catch (const io::StreamClosed&) {
        // The peer is gone (the reader sees it hang up): nothing more can be written.
        std::lock_guard lock(mutex_);
        closed_ = true;
        queue_.clear();
        break;
      }
    }
    stream_.close_write();
  }

  // The next message to write; nullopt once closed and drained.
  std::optional<std::string> next() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return std::nullopt;
    }
    std::string bytes = std::move(queue_.front());
    queue_.pop_front();
    return bytes;
  }

  io::ByteStream& stream_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  bool closed_ = false;
  std::thread writer_;
};

std::string Response::error_message() const {
  if (!error) {
    return {};
  }
  if (error->is_array() && error->as_array().size() >= 2 && error->as_array()[1].is_string()) {
    return error->as_array()[1].as_string();
  }
  return error->to_debug_string();
}

Responder::Responder(std::weak_ptr<Outbox> outbox, uint32_t id) : outbox_(std::move(outbox)), id_(id) {}

Responder::Responder(Responder&& other) noexcept : outbox_(std::move(other.outbox_)), id_(other.id_), answered_(other.answered_) {
  other.answered_ = true;
}

Responder::~Responder() {
  if (!answered_) {
    fail("request was not answered");
  }
}

void Responder::ok(const msgpack::Value& result) { answer(msgpack::Value(), result); }

void Responder::fail(std::string_view message) { answer(msgpack::Value(message), msgpack::Value()); }

void Responder::answer(const msgpack::Value& error, const msgpack::Value& result) {
  if (answered_) {
    throw std::logic_error(std::format("request {} answered twice", id_));
  }
  answered_ = true;
  if (auto outbox = outbox_.lock()) {
    outbox->send(encode_response(id_, error, result));
  }
}

Channel::Channel(io::ByteStream& stream, std::function<void()> wake)
  : stream_(stream), wake_(std::move(wake)), outbox_(std::make_shared<Outbox>(stream)), reader_([this] { read_loop(); }) {}

Channel::~Channel() {
  // Destroying the Outbox ends the peer's input; the peer then hangs up,
  // which ends the reader.
  outbox_.reset();
  reader_.join();
}

void Channel::read_loop() {
  msgpack::StreamDecoder decoder;
  char buffer[64 * 1024];
  try {
    while (const size_t n = stream_.read(buffer)) {
      decoder.feed(std::string_view(buffer, n));
      if (queue_messages(decoder)) {
        inbox_cv_.notify_all();
        wake_();
      }
    }
  } catch (const msgpack::DecodeError& e) {
    // A corrupt stream can't be resynchronized; treat it as a hang-up.
    log::error("the peer's output is not msgpack: {}", e.what());
  }
  {
    std::lock_guard lock(inbox_mutex_);
    hung_up_ = true;
  }
  inbox_cv_.notify_all();
  wake_();
}

// The reader thread's boundary: an object that isn't a msgpack-RPC message is
// dropped here, so everything queued is well-formed. True if any were queued.
bool Channel::queue_messages(msgpack::StreamDecoder& decoder) {
  bool queued = false;
  for (;;) {
    try {
      std::optional<msgpack::Value> value = decoder.next();
      if (!value) {
        return queued;
      }
      Message message = parse_message(std::move(*value));
      std::lock_guard lock(inbox_mutex_);
      inbox_.push_back(std::move(message));
      queued = true;
    } catch (const msgpack::NestingTooDeep& e) {
      log::warn("dropped a message from the peer: {}", e.what());
    } catch (const msgpack::TypeError& e) {
      log::warn("dropped a malformed message from the peer: {}", e.what());
    }
  }
}

Channel::Message Channel::parse_message(msgpack::Value value) {
  msgpack::Array a = std::move(value).as_array();
  if (a.empty()) {
    throw msgpack::TypeError("empty msgpack-RPC message");
  }
  const int64_t type = a[0].as_int();
  if (type == kRequest && a.size() == 4) {
    return Request{parse_msgid(a[1]), a[2].as_string(), std::move(a[3]).as_array()};
  }
  if (type == kResponse && a.size() == 4) {
    return Reply{parse_msgid(a[1]), parse_response(std::move(a[2]), std::move(a[3]))};
  }
  if (type == kNotification && a.size() == 3) {
    return Notification{a[1].as_string(), std::move(a[2]).as_array()};
  }
  throw msgpack::TypeError(std::format("not a msgpack-RPC message: {}", msgpack::Value(std::move(a)).to_debug_string()));
}

uint32_t Channel::send_request(std::string_view method, msgpack::Array params) {
  const uint32_t id = next_id_++;
  outbox_->send(encode_request(id, method, std::move(params)));
  return id;
}

void Channel::request(std::string_view method, msgpack::Array params, ResponseHandler on_response) {
  const uint32_t id = send_request(method, std::move(params));
  pending_.emplace(id, std::move(on_response));
}

void Channel::notify(std::string_view method, msgpack::Array params) {
  outbox_->send(msgpack::encode(msgpack::Value(msgpack::Array{kNotification, method, std::move(params)})));
}

void Channel::close_input() { outbox_->close(); }

Response Channel::call(std::string_view method, msgpack::Array params, std::chrono::milliseconds timeout) {
  const uint32_t id = send_request(method, std::move(params));
  const auto is_our_reply = [id](const Message& m) {
    const auto* reply = std::get_if<Reply>(&m);
    return reply && reply->id == id;
  };

  std::unique_lock lock(inbox_mutex_);
  std::deque<Message>::iterator found;
  const bool done = inbox_cv_.wait_for(lock, timeout, [&] {
    found = std::find_if(inbox_.begin(), inbox_.end(), is_our_reply);
    return found != inbox_.end() || hung_up_;
  });
  if (!done) {
    throw CallTimeout(std::format("no response to {} within {} ms", method, timeout.count()));
  }
  if (found == inbox_.end()) {
    throw ChannelClosed(std::format("channel closed while waiting for {}", method));
  }
  Response response = std::move(std::get<Reply>(*found).response);
  inbox_.erase(found);
  return response;
}

void Channel::dispatch(MessageHandler& handler) {
  std::deque<Message> batch;
  bool hung_up = false;
  {
    std::lock_guard lock(inbox_mutex_);
    batch.swap(inbox_);
    hung_up = hung_up_;
  }
  for (Message& message : batch) {
    try {
      std::visit([&](auto& m) { deliver(m, handler); }, message);
    } catch (const msgpack::TypeError& e) {
      log::warn("ignored a message from the peer: {}", e.what());
    } catch (const std::out_of_range& e) {
      log::warn("ignored a message from the peer with missing arguments: {}", e.what());
    }
  }
  if (hung_up) {
    fail_pending();
    closed_ = true;
  }
}

void Channel::deliver(Request& request, MessageHandler& handler) {
  handler.on_request(request.method, request.params, Responder(outbox_, request.id));
}

void Channel::deliver(Reply& reply, MessageHandler&) {
  const auto it = pending_.find(reply.id);
  if (it == pending_.end()) {
    return;  // the response to a call() that timed out
  }
  ResponseHandler on_response = std::move(it->second);
  pending_.erase(it);
  on_response(reply.response);
}

void Channel::deliver(Notification& notification, MessageHandler& handler) {
  handler.on_notification(notification.method, notification.params);
}

void Channel::fail_pending() {
  const Response closed{msgpack::Value(), msgpack::Value("channel closed")};
  for (auto& [id, on_response] : std::exchange(pending_, {})) {
    on_response(closed);
  }
}

} // namespace sailfish::rpc
