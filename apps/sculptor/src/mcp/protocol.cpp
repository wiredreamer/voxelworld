module;

#include <sculptor_version.h>

module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

namespace {

constexpr log::log_category lc_{"mcp"};

constexpr std::string_view endpoint_path   = "/mcp";
constexpr std::string_view json_media_type = "application/json";
constexpr std::string_view latest_protocol = "2025-06-18";

constexpr std::array<std::string_view, 4> known_protocols{
    "2025-11-25",
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
};

constexpr std::string_view server_instructions =
    "Sculptor is a voxel editor. Every tool acts on the running editor, and the user sees each "
    "change at once and can undo it. Call editor_state first to learn what is open.";

constexpr int64 parse_error_code      = -32700;
constexpr int64 invalid_request_code  = -32600;
constexpr int64 method_not_found_code = -32601;
constexpr int64 invalid_params_code   = -32602;

[[nodiscard]] auto path_of(std::string_view target) -> std::string_view {
    return target.substr(0, target.find('?'));
}

[[nodiscard]] auto json_reply(uint32 status, const json::value& payload) -> http_response {
    return http_response{
        .status       = status,
        .content_type = std::string{json_media_type},
        .allow        = {},
        .body         = json::dump(payload),
    };
}

[[nodiscard]] auto accepted() -> http_response {
    return http_response{.status = 202, .content_type = {}, .allow = {}, .body = {}};
}

[[nodiscard]] auto rpc_result(const json::value& id, json::value result) -> json::value {
    return json::object{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    };
}

[[nodiscard]] auto rpc_error(const json::value& id, int64 code, std::string_view message)
    -> json::value {
    return json::object{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", json::object{{"code", code}, {"message", message}}},
    };
}

[[nodiscard]] auto base64_of(std::span<const uint8> bytes) -> std::string {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string text;
    text.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const std::size_t left = bytes.size() - at;

        const uint32 group = (static_cast<uint32>(bytes[at]) << 16) |
            (left > 1 ? static_cast<uint32>(bytes[at + 1]) << 8 : 0U) |
            (left > 2 ? static_cast<uint32>(bytes[at + 2]) : 0U);

        text.push_back(alphabet[(group >> 18) & 0x3F]);
        text.push_back(alphabet[(group >> 12) & 0x3F]);
        text.push_back(left > 1 ? alphabet[(group >> 6) & 0x3F] : '=');
        text.push_back(left > 2 ? alphabet[group & 0x3F] : '=');
    }
    return text;
}

[[nodiscard]] auto tool_result(const tool_outcome& outcome) -> json::value {
    json::array content;
    if (outcome.image) {
        content.emplace_back(json::object{
            {"type", "image"},
            {"data", base64_of(outcome.image->bytes)},
            {"mimeType", outcome.image->media_type},
        });
    }
    if (!outcome.text.empty() || !outcome.image) {
        content.emplace_back(json::object{{"type", "text"}, {"text", outcome.text}});
    }

    return json::object{
        {"content", std::move(content)},
        {"isError", outcome.failed},
    };
}

}  // namespace

auto tool_success(const json::value& payload) -> tool_outcome {
    return tool_outcome{.text = json::dump(payload), .failed = false, .image = {}, .later = {}};
}

auto tool_failure(std::string reason) -> tool_outcome {
    return tool_outcome{.text = std::move(reason), .failed = true, .image = {}, .later = {}};
}

mcp_dispatcher::mcp_dispatcher(std::vector<mcp_tool> tools) {
    tools_.reserve(tools.size());
    for (auto& tool : tools) {
        auto schema = json::parse(tool.input_schema);
        if (!schema) {
            log::error(
                lc_, "tool {} has a broken input schema: {}", tool.name,
                json::describe(schema.error())
            );
            schema = json::value{json::object{{"type", "object"}}};
        }
        tools_.push_back(registered_tool{.tool = std::move(tool), .input_schema = std::move(*schema)});
    }
}

auto mcp_dispatcher::request_count() const -> uint64 {
    return request_count_;
}

auto mcp_dispatcher::last_tool() const -> const std::string& {
    return last_tool_;
}

auto mcp_dispatcher::handle(const http_request& request) -> dispatch_result {
    const auto at_once = [](http_response response) {
        return dispatch_result{.ready = std::move(response), .later = {}};
    };

    if (path_of(request.target) != endpoint_path) {
        return at_once(http_response{
            .status       = 404,
            .content_type = "text/plain; charset=utf-8",
            .allow        = {},
            .body         = "the MCP endpoint is /mcp",
        });
    }
    if (request.method != "POST") {
        return at_once(http_response{
            .status       = 405,
            .content_type = "text/plain; charset=utf-8",
            .allow        = "POST",
            .body         = "the MCP endpoint answers POST only",
        });
    }

    ++request_count_;

    const auto message = json::parse(request.body);
    if (!message) {
        return at_once(json_reply(
            400, rpc_error(json::value{}, parse_error_code, json::describe(message.error()))
        ));
    }
    if (!message->is_object()) {
        return at_once(json_reply(
            400, rpc_error(json::value{}, invalid_request_code, "expected a single request object")
        ));
    }

    auto answer = answer_(*message);
    if (answer.later) {
        return dispatch_result{
            .ready = {},
            .later = [later = std::move(answer.later)]() -> std::optional<http_response> {
                const auto reply = later();
                return reply ? std::optional{json_reply(200, *reply)} : std::nullopt;
            },
        };
    }
    return at_once(answer.reply ? json_reply(200, *answer.reply) : accepted());
}

auto mcp_dispatcher::answer_(const json::value& message) -> rpc_answer {
    const auto at_once = [](std::optional<json::value> reply) {
        return rpc_answer{.reply = std::move(reply), .later = {}};
    };

    const json::value* id_field     = message.find("id");
    const json::value* method_field = message.find("method");
    const std::string* method = method_field == nullptr ? nullptr : method_field->as_string();

    if (method == nullptr) {
        if (message.find("result") != nullptr || message.find("error") != nullptr) {
            return at_once(std::nullopt);
        }
        return at_once(rpc_error(
            id_field == nullptr ? json::value{} : *id_field, invalid_request_code,
            "the request has no method"
        ));
    }

    if (id_field == nullptr) {
        return at_once(std::nullopt);
    }

    const json::value& id        = *id_field;
    const json::value* params    = message.find("params");
    const json::value no_params  = json::object{};
    const json::value& arguments = params == nullptr ? no_params : *params;

    if (*method == "initialize") {
        return at_once(rpc_result(id, initialize_(arguments)));
    }
    if (*method == "ping") {
        return at_once(rpc_result(id, json::object{}));
    }
    if (*method == "tools/list") {
        return at_once(rpc_result(id, list_tools_()));
    }
    if (*method == "tools/call") {
        auto called = call_tool_(arguments);
        if (!called) {
            return at_once(rpc_error(id, invalid_params_code, called.error()));
        }
        if (!called->later) {
            return at_once(rpc_result(id, tool_result(*called)));
        }

        return rpc_answer{
            .reply = {},
            .later = [id, later = std::move(called->later)]() -> std::optional<json::value> {
                const auto finished = later();
                if (!finished) {
                    return std::nullopt;
                }
                if (finished->failed) {
                    log::warn(lc_, "a deferred tool failed: {}", finished->text);
                }
                return rpc_result(id, tool_result(*finished));
            },
        };
    }

    return at_once(
        rpc_error(id, method_not_found_code, std::format("unknown method {}", *method))
    );
}

auto mcp_dispatcher::initialize_(const json::value& params) const -> json::value {
    std::string_view protocol = latest_protocol;
    if (const auto* asked = params.find("protocolVersion"); asked != nullptr) {
        if (const auto* text = asked->as_string(); text != nullptr) {
            const auto known = std::ranges::find(known_protocols, std::string_view{*text});
            if (known != known_protocols.end()) {
                protocol = *known;
            }
        }
    }

    return json::object{
        {"protocolVersion", protocol},
        {"capabilities", json::object{{"tools", json::object{{"listChanged", false}}}}},
        {"serverInfo",
         json::object{
             {"name", "sculptor"},
             {"title", "Sculptor"},
             {"version", version_string},
         }},
        {"instructions", server_instructions},
    };
}

auto mcp_dispatcher::list_tools_() const -> json::value {
    json::array listed;
    listed.reserve(tools_.size());
    for (const auto& [tool, input_schema] : tools_) {
        listed.emplace_back(json::object{
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", input_schema},
        });
    }
    return json::object{{"tools", std::move(listed)}};
}

auto mcp_dispatcher::call_tool_(const json::value& params)
    -> std::expected<tool_outcome, std::string> {
    const auto name = json::cursor{params, "params"}["name"].string();
    if (!name) {
        return std::unexpected(json::describe(name.error()));
    }

    const auto found = std::ranges::find(tools_, *name, [](const registered_tool& entry) {
        return entry.tool.name;
    });
    if (found == tools_.end()) {
        return std::unexpected(std::format("unknown tool {}", *name));
    }

    const json::value* given    = params.find("arguments");
    const json::value no_fields = json::object{};
    const json::value& fields   = given == nullptr || given->is_null() ? no_fields : *given;
    if (!fields.is_object()) {
        return std::unexpected(std::string{"params.arguments: expected object"});
    }

    last_tool_ = found->tool.name;

    tool_outcome outcome = found->tool.run(fields);
    if (outcome.failed) {
        log::warn(lc_, "{} failed: {}", found->tool.name, outcome.text);
    }
    return outcome;
}

}  // namespace vw::sculptor
