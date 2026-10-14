#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "loopback_stream.h"
#include "msgpack/encoder.h"
#include "rpc/channel.h"

using namespace sailfish;
using msgpack::Array;
using msgpack::Value;

namespace {

// Answers "add" synchronously, keeps "later" requests for the test to answer,
// and ignores "ignore" requests (dropping the responder).
struct ServerHandler final : rpc::MessageHandler {
  std::vector<std::string> notifications;
  std::vector<rpc::Responder> deferred;

  void on_notification(std::string_view method, const Array&) override { notifications.emplace_back(method); }

  void on_request(std::string_view method, const Array& params, rpc::Responder responder) override {
    if (method == "add") {
      responder.ok(params[0].as_int() + params[1].as_int());
    } else if (method == "later") {
      deferred.push_back(std::move(responder));
    }
  }
};

struct NullHandler final : rpc::MessageHandler {
  void on_notification(std::string_view, const Array&) override {}
  void on_request(std::string_view, const Array&, rpc::Responder) override {}
};

// Records notifications, except "bad", whose missing argument it trips over.
struct RecordingHandler final : rpc::MessageHandler {
  std::vector<std::string> notifications;

  void on_notification(std::string_view method, const Array& params) override {
    if (method == "bad") {
      (void)params.at(0);
    }
    notifications.emplace_back(method);
  }
  void on_request(std::string_view, const Array&, rpc::Responder) override {}
};

template <typename Pred>
void pump_until(Pred done, rpc::Channel& channel, rpc::MessageHandler& handler) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    channel.dispatch(handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template <typename Pred>
void pump_until(Pred done, rpc::Channel& a, rpc::MessageHandler& ha, rpc::Channel& b, rpc::MessageHandler& hb) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done()) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    a.dispatch(ha);
    b.dispatch(hb);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

} // namespace

TEST_CASE("requests, deferred answers, dropped answers and notifications") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  rpc::Channel server(*server_stream, [] {});
  NullHandler client_handler;
  ServerHandler server_handler;

  std::optional<rpc::Response> sum, later, ignored;
  client.request("add", Array{2, 3}, [&](const rpc::Response& r) { sum = r; });
  client.request("later", Array{}, [&](const rpc::Response& r) { later = r; });
  client.request("ignore", Array{}, [&](const rpc::Response& r) { ignored = r; });
  client.notify("hello", Array{});

  pump_until([&] { return sum && ignored && server_handler.deferred.size() == 1; }, client, client_handler, server, server_handler);
  CHECK(sum->ok());
  CHECK(sum->result.as_int() == 5);
  CHECK_FALSE(ignored->ok());
  CHECK(server_handler.notifications == std::vector<std::string>{"hello"});

  server_handler.deferred.front().ok("done");
  CHECK_THROWS_AS(server_handler.deferred.front().ok("again"), std::logic_error);
  pump_until([&] { return later.has_value(); }, client, client_handler, server, server_handler);
  CHECK(later->result.as_string() == "done");
}

TEST_CASE("call() blocks for its own response and leaves other traffic queued") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  rpc::Channel server(*server_stream, [] {});
  ServerHandler server_handler;

  std::atomic<bool> stop = false;
  std::thread server_pump([&] {
    while (!stop) {
      server.dispatch(server_handler);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  const rpc::Response r = client.call("add", Array{40, 2});
  stop = true;
  server_pump.join();
  CHECK(r.result.as_int() == 42);
}

TEST_CASE("call() gives up with CallTimeout when no response comes") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  CHECK_THROWS_AS((void)client.call("silence", Array{}, std::chrono::milliseconds(20)), rpc::CallTimeout);
}

TEST_CASE("closed() turns true only once a dispatch has failed the pending requests") {
  auto [client_stream, server_stream] = test::make_loopback();
  std::atomic<bool> woken = false;
  rpc::Channel client(*client_stream, [&] { woken = true; });
  NullHandler handler;
  std::optional<rpc::Response> pending;
  client.request("never", Array{}, [&](const rpc::Response& r) { pending = r; });
  server_stream->close_write();

  // With nothing queued, the reader's only wake-up is the hang-up.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!woken) {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK_FALSE(client.closed());
  CHECK_FALSE(pending);

  client.dispatch(handler);
  REQUIRE(pending);
  CHECK_FALSE(pending->ok());
  CHECK(client.closed());
}

TEST_CASE("close_input ends the peer's input after what was already sent") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  rpc::Channel server(*server_stream, [] {});
  NullHandler client_handler;
  RecordingHandler server_handler;

  client.notify("first", Array{});
  client.close_input();
  client.notify("dropped", Array{});
  pump_until([&] { return server.closed(); }, client, client_handler, server, server_handler);
  CHECK(server_handler.notifications == std::vector<std::string>{"first"});
}

TEST_CASE("objects that aren't msgpack-RPC messages are dropped at the boundary") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  RecordingHandler handler;
  std::optional<rpc::Response> reply;
  client.request("x", Array{}, [&](const rpc::Response& r) { reply = r; });  // message id 1

  for (const Value& junk : {Value(Array{1, 7}), Value("not a message"), Value(Array{}), Value(Array{2, "m", "params"}),
         Value(Array{9, 1, 2}), Value(Array{1, uint64_t{1} << 32 | 1, Value(), "id wraps to 1"})}) {
    server_stream->write(msgpack::encode(junk));
  }
  server_stream->write(msgpack::encode(Value(Array{1, 1, Value(), "right"})));
  server_stream->write(msgpack::encode(Value(Array{2, "ok", Array{}})));

  pump_until([&] { return reply && !handler.notifications.empty(); }, client, handler);
  CHECK(reply->result.as_string() == "right");
  CHECK(handler.notifications == std::vector<std::string>{"ok"});
}

TEST_CASE("a message nested too deeply is dropped without losing the stream") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  RecordingHandler handler;
  server_stream->write(std::string(100000, '\x91') + '\xc0');
  server_stream->write(msgpack::encode(Value(Array{2, "ok", Array{}})));

  pump_until([&] { return !handler.notifications.empty(); }, client, handler);
  CHECK(handler.notifications == std::vector<std::string>{"ok"});
}

TEST_CASE("a handler rejecting one message's contents costs only that message") {
  auto [client_stream, server_stream] = test::make_loopback();
  rpc::Channel client(*client_stream, [] {});
  RecordingHandler handler;
  bool second_answered = false;
  client.request("first", Array{}, [](const rpc::Response& r) { (void)r.result.as_string(); });
  client.request("second", Array{}, [&](const rpc::Response&) { second_answered = true; });

  server_stream->write(msgpack::encode(Value(Array{2, "bad", Array{}})));
  server_stream->write(msgpack::encode(Value(Array{1, 1, Value(), 42})));
  server_stream->write(msgpack::encode(Value(Array{2, "good", Array{}})));
  server_stream->write(msgpack::encode(Value(Array{1, 2, Value(), Value()})));

  pump_until([&] { return second_answered; }, client, handler);
  CHECK(handler.notifications == std::vector<std::string>{"good"});
}
