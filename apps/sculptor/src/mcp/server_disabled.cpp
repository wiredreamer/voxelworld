module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor::mcp {

struct server::impl {};

auto server::compiled_in() -> bool {
    return false;
}

server::server(uint16, const editor_bindings&) {}

server::~server() = default;

auto server::poll() -> void {}

auto server::status() const -> server_status {
    return server_status{
        .enabled   = true,
        .listening = false,
        .port      = 0,
        .requests  = 0,
        .last_tool = {},
        .failure   = "this build has no MCP server",
    };
}

}  // namespace vw::sculptor::mcp
