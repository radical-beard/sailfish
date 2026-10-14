#include <doctest/doctest.h>

#include "platform/child_process.h"
#include "rpc/channel.h"

using namespace sailfish;
using msgpack::Array;

TEST_CASE("a real nvim --embed answers API calls over the channel") {
  if (!platform::find_executable("nvim")) {
    MESSAGE("nvim not on PATH; skipping");
    return;
  }
  auto nvim = platform::ChildProcess::spawn({.program = "nvim", .args = {"--embed", "--headless", "--clean", "-n"}});
  {
    rpc::Channel channel(*nvim, [] {});
    const rpc::Response info = channel.call("nvim_get_api_info", Array{});
    REQUIRE(info.ok());
    CHECK(info.result.as_array().at(0).as_int() > 0);  // our channel id

    const rpc::Response sum = channel.call("nvim_eval", Array{"40 + 2"});
    CHECK(sum.result.as_int() == 42);

    const rpc::Response bad = channel.call("nvim_eval", Array{"undefined_function_xyz()"});
    CHECK_FALSE(bad.ok());
    CHECK(bad.error_message().find("undefined_function_xyz") != std::string::npos);

    channel.notify("nvim_command", Array{"qa!"});
    CHECK(nvim->wait() == 0);
  }
}

TEST_CASE("run_captured collects output and exit status") {
  const auto run = platform::run_captured({.program = "sh", .args = {"-c", "cat; echo err >&2; exit 3"}, .stderr_mode = platform::StderrMode::MergeIntoStdout}, "hello\n");
  CHECK(run.exit_code == 3);
  CHECK(run.output == "hello\nerr\n");
}
