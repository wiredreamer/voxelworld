module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

struct mcp_server::impl {};

auto mcp_server::compiled_in() -> bool {
    return false;
}

mcp_server::mcp_server(uint16, const mcp_bindings&) {}

mcp_server::~mcp_server() = default;

auto mcp_server::poll() -> void {}

auto mcp_server::status() const -> mcp_status {
    return mcp_status{
        .enabled   = true,
        .listening = false,
        .port      = 0,
        .requests  = 0,
        .last_tool = {},
        .failure   = "this build has no MCP server",
    };
}

}  // namespace vw::sculptor
