module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

namespace {

constexpr std::string_view mcp_flag  = "--mcp";
constexpr std::string_view help_flag = "--help";

[[nodiscard]] auto parse_port(std::string_view text) -> std::optional<uint16> {
    uint16 port     = 0;
    const auto* end = text.data() + text.size();
    const auto done = std::from_chars(text.data(), end, port);

    if (done.ec != std::errc{} || done.ptr != end || port == 0) {
        return std::nullopt;
    }
    return port;
}

}  // namespace

auto parse_launch_options(std::span<const std::string_view> arguments)
    -> std::expected<launch_options, std::string> {
    launch_options options;

    for (const std::string_view argument : arguments) {
        if (argument == help_flag) {
            options.show_usage = true;
            continue;
        }
        if (argument == mcp_flag) {
            options.mcp_port = mcp::default_port;
            continue;
        }
        if (argument.starts_with(mcp_flag) && argument.substr(mcp_flag.size()).starts_with('=')) {
            const auto port = parse_port(argument.substr(mcp_flag.size() + 1));
            if (!port) {
                return std::unexpected(std::format("{}: the port must be 1..65535", argument));
            }
            options.mcp_port = port;
            continue;
        }
        return std::unexpected(std::format("unknown option {}", argument));
    }

    if (options.mcp_port && !mcp::server::compiled_in()) {
        return std::unexpected(std::string{"--mcp: this build has no MCP server"});
    }

    return options;
}

auto launch_usage() -> std::string_view {
    return "usage: sculptor [--mcp[=PORT]] [--help]\n"
           "  --mcp[=PORT]  serve MCP on 127.0.0.1, port 17800 unless given\n"
           "  --help        print this and exit";
}

}  // namespace vw::sculptor
