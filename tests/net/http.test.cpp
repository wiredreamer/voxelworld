#include <catch2/catch_test_macros.hpp>

import std;
import vw.core;
import vw.net;

using namespace vw;
namespace http = vw::net::http;

namespace {

constexpr std::chrono::milliseconds patience{3000};

[[nodiscard]] auto in_lines(
    std::initializer_list<std::string_view> lines
) -> std::string {
    std::string text;
    for (const std::string_view line : lines) {
        text += line;
        text += "\r\n";
    }
    return text;
}

[[nodiscard]] auto read_to_end(
    net::tcp_stream& stream
) -> std::string {
    std::string text;
    while (stream.wait_readable(patience)) {
        std::array<char, 1024> chunk{};
        const auto received = stream.receive(std::as_writable_bytes(std::span{chunk}));
        if (!received || *received == 0) {
            break;
        }
        text.append(chunk.data(), *received);
    }
    return text;
}

[[nodiscard]] auto call(
    const http::server& server, std::string_view request_text
) -> net::tcp_stream {
    auto client = net::tcp_stream::connect(server.local_endpoint());
    REQUIRE(client.has_value());
    REQUIRE(client->send_all(std::as_bytes(std::span{request_text})).has_value());
    return std::move(*client);
}

[[nodiscard]] auto next_exchange(
    http::server& server
) -> std::shared_ptr<http::exchange> {
    const auto deadline = std::chrono::steady_clock::now() + patience;
    while (std::chrono::steady_clock::now() < deadline) {
        auto pending = server.take_pending();
        if (!pending.empty()) {
            return pending.front();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return nullptr;
}

[[nodiscard]] auto refusal_of(
    std::string_view request_text, const http::server_options& options = {}
) -> std::string {
    http::server server{options};
    REQUIRE(server.is_listening());

    auto client = call(server, request_text);
    return read_to_end(client);
}

}  // namespace

TEST_CASE(
    "a request head is a line and its fields", "[net][http]"
) {
    const std::string head = in_lines({
        "POST /mcp HTTP/1.1",
        "Host: 127.0.0.1:17800",
        "Content-TYPE:   application/json  ",
        "X-Empty:",
    });

    const auto parsed = http::parse_head(head);
    REQUIRE(parsed.has_value());

    CHECK(parsed->method == "POST");
    CHECK(parsed->target == "/mcp");
    CHECK(parsed->value_of("host") == "127.0.0.1:17800");
    CHECK(parsed->value_of("content-type") == "application/json");
    CHECK(parsed->value_of("x-empty").empty());
    CHECK(parsed->value_of("absent").empty());
    CHECK(parsed->fields.contains("x-empty"));
    CHECK(parsed->body.empty());
}

TEST_CASE(
    "a head that is not a request is refused", "[net][http]"
) {
    CHECK_FALSE(http::parse_head("").has_value());
    CHECK_FALSE(http::parse_head("GET").has_value());
    CHECK_FALSE(http::parse_head("GET /").has_value());
    CHECK_FALSE(http::parse_head(" / HTTP/1.1").has_value());
    CHECK_FALSE(http::parse_head(in_lines({"GET / HTTP/1.1", "no colon here"})).has_value());
}

TEST_CASE(
    "only this machine counts as a local caller", "[net][http]"
) {
    CHECK(http::is_local_authority("127.0.0.1"));
    CHECK(http::is_local_authority("127.0.0.1:17800"));
    CHECK(http::is_local_authority("LocalHost:80"));
    CHECK(http::is_local_authority("[::1]:17800"));

    CHECK_FALSE(http::is_local_authority("example.com"));
    CHECK_FALSE(http::is_local_authority("127.0.0.1.example.com"));
    CHECK_FALSE(http::is_local_authority("localhost.example.com:80"));
    CHECK_FALSE(http::is_local_authority(""));

    CHECK(http::is_local_origin("http://localhost:3000"));
    CHECK(http::is_local_origin("https://127.0.0.1"));
    CHECK_FALSE(http::is_local_origin("https://example.com"));
    CHECK_FALSE(http::is_local_origin("null"));
    CHECK_FALSE(http::is_local_origin("file://localhost"));
}

TEST_CASE(
    "a response says how long it is and that the connection closes", "[net][http]"
) {
    const http::response answer{
        .status       = 405,
        .content_type = "text/plain",
        .fields       = {http::field{.name = "Allow", .value = "POST"}},
        .body         = "no",
    };

    const std::string expected =
        in_lines({
            "HTTP/1.1 405 Method Not Allowed",
            "Content-Type: text/plain",
            "Allow: POST",
            "Content-Length: 2",
            "Connection: close",
            "",
        }) +
        "no";

    CHECK(http::serialize(answer) == expected);
}

TEST_CASE(
    "a request reaches the owner and its answer reaches the caller", "[net][http]"
) {
    http::server server{http::server_options{}};
    REQUIRE(server.is_listening());
    REQUIRE(server.failure().empty());
    REQUIRE(server.local_endpoint().port != 0);

    const std::string asked =
        in_lines({
            "POST /mcp HTTP/1.1",
            "Host: 127.0.0.1",
            "Content-Length: 11",
            "",
        }) +
        "hello world";
    auto client = call(server, asked);

    const auto exchange = next_exchange(server);
    REQUIRE(exchange != nullptr);
    CHECK(exchange->received().method == "POST");
    CHECK(exchange->received().target == "/mcp");
    CHECK(exchange->received().body == "hello world");
    CHECK(exchange->received().value_of("host") == "127.0.0.1");

    exchange->answer(
        http::response{
            .status       = 200,
            .content_type = "application/json",
            .fields       = {},
            .body         = "{}",
        }
    );

    const std::string answered = read_to_end(client);
    CHECK(answered.starts_with("HTTP/1.1 200 OK\r\n"));
    CHECK(answered.ends_with("\r\n\r\n{}"));
    CHECK_FALSE(exchange->is_abandoned());
}

TEST_CASE(
    "a body that arrives in pieces is put together", "[net][http]"
) {
    http::server server{http::server_options{}};
    REQUIRE(server.is_listening());

    auto client = call(server, in_lines({"POST / HTTP/1.1", "Content-Length: 6", ""}));
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    REQUIRE(client.send_all(std::as_bytes(std::span{std::string_view{"abc"}})).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    REQUIRE(client.send_all(std::as_bytes(std::span{std::string_view{"def"}})).has_value());

    const auto exchange = next_exchange(server);
    REQUIRE(exchange != nullptr);
    CHECK(exchange->received().body == "abcdef");

    exchange->answer(http::plain(200, "ok"));
    CHECK(read_to_end(client).ends_with("ok"));
}

TEST_CASE(
    "a caller that waits for permission to send the body gets it", "[net][http]"
) {
    http::server server{http::server_options{}};
    REQUIRE(server.is_listening());

    auto client = call(
        server, in_lines({"POST / HTTP/1.1", "Content-Length: 2", "Expect: 100-Continue", ""})
    );

    REQUIRE(client.wait_readable(patience));
    std::array<char, 64> chunk{};
    const auto received = client.receive(std::as_writable_bytes(std::span{chunk}));
    REQUIRE(received.has_value());
    CHECK(std::string_view{chunk.data(), *received} == "HTTP/1.1 100 Continue\r\n\r\n");

    REQUIRE(client.send_all(std::as_bytes(std::span{std::string_view{"hi"}})).has_value());

    const auto exchange = next_exchange(server);
    REQUIRE(exchange != nullptr);
    CHECK(exchange->received().body == "hi");
    exchange->answer(http::plain(200, "ok"));
    CHECK(read_to_end(client).starts_with("HTTP/1.1 200 OK"));
}

TEST_CASE(
    "a caller from another host or origin is refused", "[net][http]"
) {
    const std::string foreign_host =
        refusal_of(in_lines({"POST / HTTP/1.1", "Host: example.com", ""}));
    CHECK(foreign_host.starts_with("HTTP/1.1 403 Forbidden"));
    CHECK(foreign_host.ends_with("only local hosts are served"));

    const std::string foreign_origin = refusal_of(
        in_lines({"POST / HTTP/1.1", "Host: localhost", "Origin: https://example.com", ""})
    );
    CHECK(foreign_origin.starts_with("HTTP/1.1 403 Forbidden"));
    CHECK(foreign_origin.ends_with("only local origins are served"));
}

TEST_CASE(
    "a server told to serve anyone does not look at the host", "[net][http]"
) {
    http::server_options options;
    options.only_local_callers = false;

    http::server server{options};
    REQUIRE(server.is_listening());

    auto client = call(server, in_lines({"GET / HTTP/1.1", "Host: example.com", ""}));

    const auto exchange = next_exchange(server);
    REQUIRE(exchange != nullptr);
    exchange->answer(http::plain(200, "ok"));
    CHECK(read_to_end(client).starts_with("HTTP/1.1 200 OK"));
}

TEST_CASE(
    "requests the server cannot read are refused by name", "[net][http]"
) {
    CHECK(refusal_of(in_lines({"nonsense", ""})).starts_with("HTTP/1.1 400 Bad Request"));

    CHECK(refusal_of(in_lines({"POST / HTTP/1.1", "Content-Length: many", ""}))
              .ends_with("malformed Content-Length"));

    CHECK(refusal_of(in_lines({"POST / HTTP/1.1", "Transfer-Encoding: chunked", ""}))
              .starts_with("HTTP/1.1 411 Length Required"));
}

TEST_CASE(
    "a request larger than the limits is refused before it is read", "[net][http]"
) {
    http::server_options options;
    options.max_head_bytes = 64;
    options.max_body_bytes = 8;

    const std::string long_field(200, 'x');
    CHECK(refusal_of(in_lines({"POST / HTTP/1.1", long_field}), options)
              .starts_with("HTTP/1.1 431 Request Header Fields Too Large"));

    CHECK(refusal_of(in_lines({"POST / HTTP/1.1", "Content-Length: 9", ""}), options)
              .starts_with("HTTP/1.1 413 Content Too Large"));
}

TEST_CASE(
    "a request nobody answers is given up on", "[net][http]"
) {
    http::server_options options;
    options.reply_timeout = std::chrono::milliseconds{60};

    http::server server{options};
    REQUIRE(server.is_listening());

    auto client = call(server, in_lines({"POST / HTTP/1.1", "Content-Length: 0", ""}));

    const auto exchange = next_exchange(server);
    REQUIRE(exchange != nullptr);

    const std::string answered = read_to_end(client);
    CHECK(answered.starts_with("HTTP/1.1 504 Gateway Timeout"));
    CHECK(exchange->is_abandoned());
}

TEST_CASE(
    "a caller that never finishes its request is given up on", "[net][http]"
) {
    http::server_options options;
    options.read_timeout = std::chrono::milliseconds{60};

    http::server server{options};
    REQUIRE(server.is_listening());

    auto client = call(server, "POST / HTTP/1.1\r\nContent-Le");

    CHECK(read_to_end(client).starts_with("HTTP/1.1 400 Bad Request"));
    CHECK(server.take_pending().empty());
}

TEST_CASE(
    "a server on a taken port says so and serves nothing", "[net][http]"
) {
    http::server first{http::server_options{}};
    REQUIRE(first.is_listening());

    http::server_options options;
    options.local = first.local_endpoint();

    http::server second{options};
    CHECK_FALSE(second.is_listening());
    CHECK(second.failure() == std::format("port {} is taken", options.local.port));
    CHECK(second.take_pending().empty());
}

TEST_CASE(
    "a server stops while a caller is still waiting", "[net][http]"
) {
    std::optional<net::tcp_stream> client;
    {
        http::server server{http::server_options{}};
        REQUIRE(server.is_listening());

        client.emplace(call(server, in_lines({"POST / HTTP/1.1", "Content-Length: 0", ""})));
        REQUIRE(next_exchange(server) != nullptr);
    }

    CHECK(read_to_end(*client).starts_with("HTTP/1.1 504 Gateway Timeout"));
}
