export module vw.sculptor:mcp;

import std;

import vw.core;
import vw.asset;
import vw.gfx;
import vw.net;
import :state;
import :operations;
import :services;

export namespace vw::sculptor::mcp {

inline constexpr uint16 default_port = 17800;

struct editor_bindings {
    gfx::engine* engine           = nullptr;
    app_state* state              = nullptr;
    operation_manager* operations = nullptr;
    file_service* files           = nullptr;
    node_service* nodes           = nullptr;
    volume_service* volumes       = nullptr;
    clip_service* clips           = nullptr;
    fsm_service* machines         = nullptr;
    view_service* views           = nullptr;
};

class server final {
public:
    [[nodiscard]] static auto compiled_in() -> bool;

    server(uint16 port, const editor_bindings& bindings);
    ~server();

    server(const server&)                    = delete;
    auto operator=(const server&) -> server& = delete;

    auto poll() -> void;

    [[nodiscard]] auto status() const -> server_status;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vw::sculptor::mcp

namespace vw::sculptor::mcp {

struct tool_image {
    std::string media_type;
    std::vector<uint8> bytes;
};

struct tool_outcome {
    std::string text;
    bool failed = false;
    std::optional<tool_image> image;
    std::function<std::optional<tool_outcome>()> later;
};

struct dispatch_result {
    std::optional<net::http::response> ready;
    std::function<std::optional<net::http::response>()> later;
};

[[nodiscard]] auto tool_success(const json::value& payload) -> tool_outcome;
[[nodiscard]] auto tool_failure(std::string reason) -> tool_outcome;

struct tool {
    std::string_view name;
    std::string_view description;
    std::string_view input_schema;
    std::function<tool_outcome(const json::value& arguments)> run;
};

using tool_body = std::function<tool_outcome(const json::value& arguments)>;

[[nodiscard]] auto make_editor_tools(const editor_bindings& bindings) -> std::vector<tool>;

auto append_prefab_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;
auto append_node_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;
auto append_volume_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;
auto append_view_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;
auto append_clip_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;
auto append_fsm_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void;

[[nodiscard]] auto describe_node(const editor_bindings& bindings, std::string_view name)
    -> json::value;

class argument_reader final {
public:
    explicit argument_reader(const json::value& arguments);
    explicit argument_reader(json::cursor at);

    auto allow(std::initializer_list<std::string_view> known) -> void;

    [[nodiscard]] auto has(std::string_view key) const -> bool;
    [[nodiscard]] auto is_null(std::string_view key) const -> bool;
    [[nodiscard]] auto at(std::string_view key) const -> json::cursor;

    [[nodiscard]] auto text(std::string_view key) -> std::string;
    [[nodiscard]] auto optional_text(std::string_view key) -> std::optional<std::string>;
    [[nodiscard]] auto flag_or(std::string_view key, bool fallback) -> bool;
    [[nodiscard]] auto optional_index(std::string_view key) -> std::optional<std::size_t>;
    [[nodiscard]] auto optional_vec3f(std::string_view key) -> std::optional<vec3f>;
    [[nodiscard]] auto optional_vec3i(std::string_view key) -> std::optional<vec3i>;
    [[nodiscard]] auto text_list(std::string_view key) -> std::vector<std::string>;

    auto fail(std::string message) -> void;

    [[nodiscard]] auto failed() const -> bool;
    [[nodiscard]] auto error() const -> const std::string&;

private:
    auto note_(const json::access_error& error) -> void;

    json::cursor at_;
    std::string error_;
};

[[nodiscard]] auto editor_busy_reason(const app_state& state) -> std::string_view;
[[nodiscard]] auto when_idle(const editor_bindings& bindings, tool_body body) -> tool_body;

[[nodiscard]] auto json_of(float32 number) -> json::value;
[[nodiscard]] auto json_of(const vec3f& vector) -> json::value;
[[nodiscard]] auto json_of(const vec3i& vector) -> json::value;
[[nodiscard]] auto json_or_null(std::string_view text) -> json::value;

class dispatcher final {
public:
    explicit dispatcher(std::vector<tool> tools);

    [[nodiscard]] auto handle(const net::http::request& request) -> dispatch_result;

    [[nodiscard]] auto request_count() const -> uint64;
    [[nodiscard]] auto last_tool() const -> const std::string&;

private:
    struct registered_tool {
        tool definition;
        json::value input_schema;
    };

    struct rpc_answer {
        std::optional<json::value> reply;
        std::function<std::optional<json::value>()> later;
    };

    [[nodiscard]] auto answer_(const json::value& message) -> rpc_answer;
    [[nodiscard]] auto initialize_(const json::value& params) const -> json::value;
    [[nodiscard]] auto list_tools_() const -> json::value;
    [[nodiscard]] auto call_tool_(const json::value& params)
        -> std::expected<tool_outcome, std::string>;

    std::vector<registered_tool> tools_;
    uint64 request_count_ = 0;
    std::string last_tool_;
};

}  // namespace vw::sculptor::mcp
