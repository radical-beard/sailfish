#!/usr/bin/env python3
"""A stand-in for the Claude Messages API, for testing Sailfish's AI features
without network access or cost.

    python3 tests/mock_claude.py 8765 /tmp/last_request.json

then run Sailfish with ANTHROPIC_BASE_URL=http://127.0.0.1:8765 and
ANTHROPIC_API_KEY=sailfish-mock-key (any value; the mock only checks one is sent).

The last user message chooses the reply:
  contains "REFUSE"    -> a streamed refusal (stop_reason "refusal")
  contains "HTTPERR"   -> HTTP 400 with a JSON error body
  contains "STREAMERR" -> a stream that ends with an error event
  contains "<instruction>" (an edit) -> replacement code, wrapped in a fence
  otherwise            -> a short Markdown answer, streamed in pieces
Each request (headers and body) is written to the given JSON file.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RECORD = sys.argv[2] if len(sys.argv) > 2 else "last_request.json"


def last_user_text(body):
    content = body["messages"][-1]["content"]
    if isinstance(content, str):
        return content
    return "".join(block.get("text", "") for block in content)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def event(self, kind, data):
        self.wfile.write(f"event: {kind}\ndata: {json.dumps(data)}\n\n".encode())
        self.wfile.flush()

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["content-length"])))
        with open(RECORD, "w") as f:
            json.dump({"path": self.path, "headers": dict(self.headers), "body": body}, f, indent=2)
        text = last_user_text(body)

        if "HTTPERR" in text or not (self.headers.get("x-api-key") or self.headers.get("authorization")):
            payload = json.dumps({"type": "error", "error": {"type": "invalid_request_error", "message": "mock: bad request"}}).encode()
            self.send_response(400)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()
        self.event("message_start", {"type": "message_start", "message": {"id": "msg_mock", "type": "message", "role": "assistant", "model": body["model"], "content": []}})
        # Opus-class models think first; display "omitted" streams it empty.
        self.event("content_block_start", {"type": "content_block_start", "index": 0, "content_block": {"type": "thinking", "thinking": "", "signature": ""}})
        self.event("content_block_delta", {"type": "content_block_delta", "index": 0, "delta": {"type": "signature_delta", "signature": "mock-signature"}})
        self.event("content_block_stop", {"type": "content_block_stop", "index": 0})

        if "REFUSE" in text:
            self.event("message_delta", {"type": "message_delta", "delta": {"stop_reason": "refusal"}, "usage": {"output_tokens": 0}})
            self.event("message_stop", {"type": "message_stop"})
            return

        if "<instruction>" in text:
            pieces = ["```lua\n", "local function greet(name)\n", "  -- greets politely\n", "  print(\"Hello, \" .. name .. \"!\")\n", "end\n", "```"]
        else:
            pieces = ["Here is ", "what that code does:\n\n", "- It defines `greet`\n", "- Then calls it with `\"sailfish\"`.\n"]
        self.event("content_block_start", {"type": "content_block_start", "index": 1, "content_block": {"type": "text", "text": ""}})
        for piece in pieces:
            time.sleep(0.05)
            self.event("content_block_delta", {"type": "content_block_delta", "index": 1, "delta": {"type": "text_delta", "text": piece}})
        if "STREAMERR" in text:
            self.event("error", {"type": "error", "error": {"type": "overloaded_error", "message": "mock: overloaded"}})
            return
        self.event("content_block_stop", {"type": "content_block_stop", "index": 1})
        self.event("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn"}, "usage": {"output_tokens": 42}})
        self.event("message_stop", {"type": "message_stop"})


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
