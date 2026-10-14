#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>

#include "io/byte_stream.h"
#include "msgpack/value.h"

namespace sailfish::msgpack {
class StreamDecoder;
}

namespace sailfish::rpc {

// Outcome of a request we sent: the peer's result, or its error object.
struct Response {
  msgpack::Value result;
  std::optional<msgpack::Value> error;

  [[nodiscard]] bool ok() const { return !error.has_value(); }
  // Neovim errors are [type, "message"]; anything else is rendered as-is.
  [[nodiscard]] std::string error_message() const;
};

using ResponseHandler = std::function<void(const Response&)>;

class Outbox;

// Answers one request from the peer, exactly once (a second answer throws
// std::logic_error). Handlers may keep it and answer later, on the channel's
// owner thread (after a background job); dropping it unanswered replies with
// an error, so the peer never blocks forever on a forgotten request.
class Responder {
public:
  Responder(std::weak_ptr<Outbox> outbox, uint32_t id);
  Responder(Responder&& other) noexcept;
  Responder& operator=(Responder&&) = delete;
  Responder(const Responder&) = delete;
  ~Responder();

  void ok(const msgpack::Value& result);
  void fail(std::string_view message);

private:
  void answer(const msgpack::Value& error, const msgpack::Value& result);

  std::weak_ptr<Outbox> outbox_;
  uint32_t id_;
  bool answered_ = false;
};

// Receives what the peer initiates. Called on the thread running dispatch().
class MessageHandler {
public:
  virtual ~MessageHandler() = default;
  virtual void on_notification(std::string_view method, const msgpack::Array& params) = 0;
  virtual void on_request(std::string_view method, const msgpack::Array& params, Responder responder) = 0;
};

class ChannelClosed : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// call() stopped waiting; a response arriving later is dropped.
class CallTimeout : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// msgpack-RPC over a byte stream. A reader thread decodes incoming messages
// and a writer thread drains outgoing ones, so the owning (UI) thread never
// blocks on I/O; it calls dispatch() when `wake` signals there is work. Every
// queued message has been validated on the reader thread; what the peer sends
// that isn't msgpack-RPC is logged and dropped there.
//
// Destruction ends the peer's input after the queued writes, then waits for
// the writer to finish and for the peer to hang up. Both waits are short only
// while the peer reads its input and exits when it ends, as `nvim --embed`
// does; an owner whose peer may not (a wedged process) must end that process
// before destroying the channel.
class Channel {
public:
  Channel(io::ByteStream& stream, std::function<void()> wake);
  ~Channel();
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;

  void request(std::string_view method, msgpack::Array params, ResponseHandler on_response);
  void notify(std::string_view method, msgpack::Array params);
  // Ends the peer's input once everything already sent is written. Never
  // blocks; whatever is sent afterwards is dropped.
  void close_input();

  // Waits for the response to one request; other incoming messages stay
  // queued for the next dispatch(). Throws CallTimeout or ChannelClosed.
  Response call(std::string_view method, msgpack::Array params, std::chrono::milliseconds timeout = std::chrono::seconds(10));

  // Runs response callbacks and forwards peer-initiated messages. A handler
  // that rejects a message's contents (msgpack::TypeError, std::out_of_range)
  // costs only that message.
  void dispatch(MessageHandler& handler);

  // The peer hung up, everything it sent has been dispatched, and requests
  // left unanswered have been failed. Owner thread only.
  [[nodiscard]] bool closed() const { return closed_; }

private:
  struct Request {
    uint32_t id;
    std::string method;
    msgpack::Array params;
  };
  struct Reply {
    uint32_t id;
    Response response;
  };
  struct Notification {
    std::string method;
    msgpack::Array params;
  };
  using Message = std::variant<Request, Reply, Notification>;

  static Message parse_message(msgpack::Value value);
  void read_loop();
  bool queue_messages(msgpack::StreamDecoder& decoder);
  void deliver(Request& request, MessageHandler& handler);
  void deliver(Reply& reply, MessageHandler& handler);
  void deliver(Notification& notification, MessageHandler& handler);
  void fail_pending();
  uint32_t send_request(std::string_view method, msgpack::Array params);

  io::ByteStream& stream_;
  std::function<void()> wake_;
  std::shared_ptr<Outbox> outbox_;

  std::mutex inbox_mutex_;
  std::condition_variable inbox_cv_;
  std::deque<Message> inbox_;
  bool hung_up_ = false;  // the reader has stopped; guarded by inbox_mutex_

  uint32_t next_id_ = 1;
  std::unordered_map<uint32_t, ResponseHandler> pending_;
  bool closed_ = false;

  std::thread reader_;
};

} // namespace sailfish::rpc
