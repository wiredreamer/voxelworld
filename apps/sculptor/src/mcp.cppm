export module vw.sculptor:mcp;

import std;

import vw.core;
import vw.asset;
import vw.gfx;
import :state;
import :operations;
import :services;

export namespace vw::sculptor {

inline constexpr uint16 default_mcp_port = 17800;

struct mcp_bindings {
    gfx::engine* engine           = nullptr;
    app_state* state              = nullptr;
    operation_manager* operations = nullptr;
    file_service* files           = nullptr;
    node_service* nodes           = nullptr;
    volume_service* volumes       = nullptr;
};

class mcp_server final {
public:
    [[nodiscard]] static auto compiled_in() -> bool;

    mcp_server(uint16 port, const mcp_bindings& bindings);
    ~mcp_server();

    mcp_server(const mcp_server&)                    = delete;
    auto operator=(const mcp_server&) -> mcp_server& = delete;

    auto poll() -> void;

    [[nodiscard]] auto status() const -> mcp_status;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vw::sculptor

namespace vw::sculptor {

struct http_request {
    std::string method;
    std::string target;
    std::string body;
};

struct http_response {
    uint32 status = 200;
    std::string content_type;
    std::string allow;
    std::string body;
};

struct http_exchange {
    http_request request;
    std::promise<http_response> reply;
    std::atomic<bool> abandoned{false};
};

class http_listener final {
public:
    explicit http_listener(uint16 port);
    ~http_listener();

    http_listener(const http_listener&)                    = delete;
    auto operator=(const http_listener&) -> http_listener& = delete;

    [[nodiscard]] auto is_listening() const -> bool;
    [[nodiscard]] auto failure() const -> const std::string&;

    [[nodiscard]] auto take_pending() -> std::vector<std::shared_ptr<http_exchange>>;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

struct tool_outcome {
    std::string text;
    bool failed = false;
};

[[nodiscard]] auto tool_success(const json::value& payload) -> tool_outcome;
[[nodiscard]] auto tool_failure(std::string reason) -> tool_outcome;

struct mcp_tool {
    std::string_view name;
    std::string_view description;
    std::string_view input_schema;
    std::function<tool_outcome(const json::value& arguments)> run;
};

using tool_body = std::function<tool_outcome(const json::value& arguments)>;

[[nodiscard]] auto make_editor_tools(const mcp_bindings& bindings) -> std::vector<mcp_tool>;

auto append_prefab_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void;
auto append_node_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void;
auto append_volume_tools(std::vector<mcp_tool>& tools, const mcp_bindings& bindings) -> void;

[[nodiscard]] auto describe_node(const mcp_bindings& bindings, std::string_view name)
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
[[nodiscard]] auto when_idle(const mcp_bindings& bindings, tool_body body) -> tool_body;

[[nodiscard]] auto json_of(float32 number) -> json::value;
[[nodiscard]] auto json_of(const vec3f& vector) -> json::value;
[[nodiscard]] auto json_of(const vec3i& vector) -> json::value;
[[nodiscard]] auto json_or_null(std::string_view text) -> json::value;

class mcp_dispatcher final {
public:
    explicit mcp_dispatcher(std::vector<mcp_tool> tools);

    [[nodiscard]] auto handle(const http_request& request) -> http_response;

    [[nodiscard]] auto request_count() const -> uint64;
    [[nodiscard]] auto last_tool() const -> const std::string&;

private:
    struct registered_tool {
        mcp_tool tool;
        json::value input_schema;
    };

    [[nodiscard]] auto answer_(const json::value& message) -> std::optional<json::value>;
    [[nodiscard]] auto initialize_(const json::value& params) const -> json::value;
    [[nodiscard]] auto list_tools_() const -> json::value;
    [[nodiscard]] auto call_tool_(const json::value& params)
        -> std::expected<json::value, std::string>;

    std::vector<registered_tool> tools_;
    uint64 request_count_ = 0;
    std::string last_tool_;
};

}  // namespace vw::sculptor
