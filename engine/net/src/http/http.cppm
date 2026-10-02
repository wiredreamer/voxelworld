export module vw.net:http;

import std;

import vw.core;
import :endpoint;
import :socket;

export namespace vw::net::http {

struct field {
    std::string name;
    std::string value;
};

struct request {
    std::string method;
    std::string target;
    string_map<std::string> fields;
    std::string body;

    [[nodiscard]] auto value_of(std::string_view lowercase_name) const -> std::string_view;
};

struct response {
    uint32 status = 200;
    std::string content_type;
    std::vector<field> fields;
    std::string body;
};

[[nodiscard]] auto plain(uint32 status, std::string_view message) -> response;

[[nodiscard]] auto parse_head(std::string_view head) -> std::optional<request>;
[[nodiscard]] auto serialize(const response& answer) -> std::string;

[[nodiscard]] auto is_local_authority(std::string_view authority) -> bool;
[[nodiscard]] auto is_local_origin(std::string_view origin) -> bool;

class server;

class exchange final {
public:
    explicit exchange(request received);

    exchange(const exchange&)                    = delete;
    auto operator=(const exchange&) -> exchange& = delete;

    [[nodiscard]] auto received() const -> const request&;
    [[nodiscard]] auto is_abandoned() const -> bool;

    auto answer(response reply) -> void;

private:
    friend class server;

    [[nodiscard]] auto wait_answer_(std::chrono::milliseconds timeout) -> std::optional<response>;
    auto abandon_() -> void;

    request received_;
    std::mutex mutex_;
    std::condition_variable answered_;
    std::optional<response> reply_;
    std::atomic<bool> abandoned_{false};
};

struct server_options {
    endpoint local                          = loopback(0);
    bool only_local_callers                 = true;
    std::size_t max_head_bytes              = 16 * 1024;
    std::size_t max_body_bytes              = 16 * 1024 * 1024;
    std::chrono::milliseconds read_timeout  = std::chrono::seconds{10};
    std::chrono::milliseconds reply_timeout = std::chrono::seconds{30};
};

class server final {
public:
    explicit server(const server_options& options);
    ~server();

    server(const server&)                    = delete;
    auto operator=(const server&) -> server& = delete;

    [[nodiscard]] auto is_listening() const -> bool;
    [[nodiscard]] auto failure() const -> const std::string&;
    [[nodiscard]] auto local_endpoint() const -> endpoint;

    [[nodiscard]] auto take_pending() -> std::vector<std::shared_ptr<exchange>>;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace vw::net::http

namespace vw::net::http {

[[nodiscard]] auto lowered(std::string_view text) -> std::string;

}  // namespace vw::net::http
