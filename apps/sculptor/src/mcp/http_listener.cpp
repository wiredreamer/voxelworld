module;

#ifdef _WIN32
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <cerrno>
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor {

namespace {

constexpr log::log_category lc_{"mcp"};

constexpr std::size_t max_head_bytes = 16 * 1024;
constexpr std::size_t max_body_bytes = 16 * 1024 * 1024;

constexpr std::chrono::milliseconds accept_tick{200};
constexpr std::chrono::milliseconds reply_tick{20};
constexpr std::chrono::seconds read_timeout{10};
constexpr std::chrono::seconds reply_timeout{30};

constexpr std::string_view head_end = "\r\n\r\n";
constexpr std::string_view line_end = "\r\n";

#ifdef _WIN32
using socket_handle = SOCKET;
constexpr socket_handle no_socket = INVALID_SOCKET;
#else
using socket_handle = int;
constexpr socket_handle no_socket = -1;
#endif

class socket_runtime final {
public:
    socket_runtime() {
#ifdef _WIN32
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }

    ~socket_runtime() {
#ifdef _WIN32
        if (started_) {
            WSACleanup();
        }
#endif
    }

    socket_runtime(const socket_runtime&)                    = delete;
    auto operator=(const socket_runtime&) -> socket_runtime& = delete;

    [[nodiscard]] auto started() const -> bool {
        return started_;
    }

private:
    bool started_ = true;
};

[[nodiscard]] auto last_socket_error() -> int32 {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

[[nodiscard]] auto is_address_in_use(int32 error) -> bool {
#ifdef _WIN32
    return error == WSAEADDRINUSE || error == WSAEACCES;
#else
    return error == EADDRINUSE;
#endif
}

auto close_socket(socket_handle socket) -> void {
    if (socket == no_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

auto finish_sending(socket_handle socket) -> void {
#ifdef _WIN32
    shutdown(socket, SD_SEND);
#else
    ::shutdown(socket, SHUT_WR);
#endif
}

[[nodiscard]] auto wait_readable(socket_handle socket, std::chrono::milliseconds timeout) -> bool {
    const auto timeout_ms = static_cast<int>(timeout.count());
#ifdef _WIN32
    WSAPOLLFD entry{};
    entry.fd     = socket;
    entry.events = POLLRDNORM;
    return WSAPoll(&entry, 1, timeout_ms) > 0;
#else
    pollfd entry{};
    entry.fd     = socket;
    entry.events = POLLIN;
    return ::poll(&entry, 1, timeout_ms) > 0;
#endif
}

[[nodiscard]] auto receive_some(socket_handle socket, std::span<char> into) -> int64 {
#ifdef _WIN32
    return ::recv(socket, into.data(), static_cast<int>(into.size()), 0);
#else
    return ::recv(socket, into.data(), into.size(), 0);
#endif
}

[[nodiscard]] auto send_some(socket_handle socket, std::string_view bytes) -> int64 {
#ifdef _WIN32
    return ::send(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
#else
    return ::send(socket, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#endif
}

auto send_all(socket_handle socket, std::string_view bytes) -> void {
    while (!bytes.empty()) {
        const int64 sent = send_some(socket, bytes);
        if (sent <= 0) {
            return;
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
}

[[nodiscard]] auto open_loopback_listener(uint16 port) -> std::expected<socket_handle, std::string> {
    const socket_handle listening = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listening == no_socket) {
        return std::unexpected(std::format("cannot create a socket, socket error {}", last_socket_error()));
    }

    const int enabled = 1;
#ifdef _WIN32
    setsockopt(
        listening, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))
    );
#else
    setsockopt(listening, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(0x7F000001U);

    const auto* generic = reinterpret_cast<const sockaddr*>(&address);
    if (::bind(listening, generic, sizeof(address)) != 0) {
        const int32 error = last_socket_error();
        close_socket(listening);
        if (is_address_in_use(error)) {
            return std::unexpected(std::format("port {} is taken", port));
        }
        return std::unexpected(std::format("cannot bind port {}, socket error {}", port, error));
    }
    if (::listen(listening, 8) != 0) {
        const int32 error = last_socket_error();
        close_socket(listening);
        return std::unexpected(std::format("cannot listen on port {}, socket error {}", port, error));
    }
    return listening;
}

[[nodiscard]] auto reason_of(uint32 status) -> std::string_view {
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 411:
            return "Length Required";
        case 413:
            return "Content Too Large";
        case 431:
            return "Request Header Fields Too Large";
        case 504:
            return "Gateway Timeout";
        default:
            return "Error";
    }
}

[[nodiscard]] auto plain(uint32 status, std::string_view message) -> http_response {
    return http_response{
        .status       = status,
        .content_type = "text/plain; charset=utf-8",
        .allow        = {},
        .body         = std::string{message},
    };
}

[[nodiscard]] auto serialize(const http_response& response) -> std::string {
    std::string text = std::format("HTTP/1.1 {} {}\r\n", response.status, reason_of(response.status));
    if (!response.content_type.empty()) {
        text += std::format("Content-Type: {}\r\n", response.content_type);
    }
    if (!response.allow.empty()) {
        text += std::format("Allow: {}\r\n", response.allow);
    }
    text += std::format("Content-Length: {}\r\n", response.body.size());
    text += "Connection: close\r\n\r\n";
    text += response.body;
    return text;
}

[[nodiscard]] auto trimmed(std::string_view text) -> std::string_view {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

[[nodiscard]] auto lowered(std::string_view text) -> std::string {
    std::string out{text};
    for (char& symbol : out) {
        if (symbol >= 'A' && symbol <= 'Z') {
            symbol = static_cast<char>(symbol - 'A' + 'a');
        }
    }
    return out;
}

[[nodiscard]] auto is_local_authority(std::string_view authority) -> bool {
    std::string_view host = authority;
    if (host.starts_with('[')) {
        const auto closing = host.find(']');
        host = closing == std::string_view::npos ? host : host.substr(0, closing + 1);
    } else if (const auto colon = host.rfind(':'); colon != std::string_view::npos) {
        host = host.substr(0, colon);
    }

    const std::string name = lowered(host);
    return name == "127.0.0.1" || name == "localhost" || name == "[::1]";
}

[[nodiscard]] auto is_local_origin(std::string_view origin) -> bool {
    for (const std::string_view scheme : {"http://", "https://"}) {
        if (origin.starts_with(scheme)) {
            return is_local_authority(origin.substr(scheme.size()));
        }
    }
    return false;
}

struct request_head {
    std::string method;
    std::string target;
    string_map<std::string> fields;

    [[nodiscard]] auto field(std::string_view name) const -> std::string_view {
        const auto found = fields.find(name);
        return found == fields.end() ? std::string_view{} : std::string_view{found->second};
    }
};

[[nodiscard]] auto parse_head(std::string_view head) -> std::optional<request_head> {
    request_head parsed;

    bool first_line = true;
    for (const auto line_range : std::views::split(head, line_end)) {
        const std::string_view line{line_range.begin(), line_range.end()};

        if (first_line) {
            first_line = false;

            const auto method_end = line.find(' ');
            const auto target_end = line.rfind(' ');
            if (method_end == std::string_view::npos || target_end <= method_end) {
                return std::nullopt;
            }
            parsed.method = line.substr(0, method_end);
            parsed.target = trimmed(line.substr(method_end + 1, target_end - method_end - 1));
            if (parsed.method.empty() || parsed.target.empty()) {
                return std::nullopt;
            }
            continue;
        }

        if (line.empty()) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        parsed.fields[lowered(trimmed(line.substr(0, colon)))] = trimmed(line.substr(colon + 1));
    }

    return first_line ? std::nullopt : std::optional{std::move(parsed)};
}

class connection final {
public:
    connection(socket_handle socket, std::stop_token stop)
        : socket_(socket),
          stop_(std::move(stop)) {}

    [[nodiscard]] auto read_request() -> std::expected<http_request, http_response> {
        const auto deadline = std::chrono::steady_clock::now() + read_timeout;

        std::size_t head_size = std::string::npos;
        while (head_size == std::string::npos) {
            if (buffer_.size() > max_head_bytes) {
                return std::unexpected(plain(431, "request head is too large"));
            }
            if (!receive_more_(deadline)) {
                return std::unexpected(plain(400, "incomplete request"));
            }
            head_size = buffer_.find(head_end);
        }

        const auto head = parse_head(std::string_view{buffer_}.substr(0, head_size));
        if (!head) {
            return std::unexpected(plain(400, "malformed request head"));
        }

        if (const auto host = head->field("host"); !host.empty() && !is_local_authority(host)) {
            return std::unexpected(plain(403, "only local hosts are served"));
        }
        if (const auto origin = head->field("origin"); !origin.empty() && !is_local_origin(origin)) {
            return std::unexpected(plain(403, "only local origins are served"));
        }
        if (!head->field("transfer-encoding").empty()) {
            return std::unexpected(plain(411, "send Content-Length, chunked bodies are not read"));
        }

        std::size_t body_size = 0;
        if (const auto length = head->field("content-length"); !length.empty()) {
            const auto* end   = length.data() + length.size();
            const auto parsed = std::from_chars(length.data(), end, body_size);
            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                return std::unexpected(plain(400, "malformed Content-Length"));
            }
        }
        if (body_size > max_body_bytes) {
            return std::unexpected(plain(413, "request body is too large"));
        }

        if (lowered(head->field("expect")) == "100-continue") {
            send_all(socket_, "HTTP/1.1 100 Continue\r\n\r\n");
        }

        const std::size_t body_from = head_size + head_end.size();
        while (buffer_.size() < body_from + body_size) {
            if (!receive_more_(deadline)) {
                return std::unexpected(plain(400, "incomplete request body"));
            }
        }

        return http_request{
            .method = head->method,
            .target = head->target,
            .body   = buffer_.substr(body_from, body_size),
        };
    }

    auto respond(const http_response& response) const -> void {
        send_all(socket_, serialize(response));
        finish_sending(socket_);
    }

private:
    [[nodiscard]] auto receive_more_(std::chrono::steady_clock::time_point deadline) -> bool {
        while (!wait_readable(socket_, accept_tick)) {
            if (stop_.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
        }

        std::array<char, 8192> chunk{};
        const int64 received = receive_some(socket_, chunk);
        if (received <= 0) {
            return false;
        }
        buffer_.append(chunk.data(), static_cast<std::size_t>(received));
        return true;
    }

    socket_handle socket_;
    std::stop_token stop_;
    std::string buffer_;
};

}  // namespace

struct http_listener::impl {
    socket_runtime runtime;
    socket_handle listening = no_socket;
    std::string failure;

    std::mutex mutex;
    std::vector<std::shared_ptr<http_exchange>> pending;

    std::jthread worker;

    auto serve(const std::stop_token& stop) -> void {
        while (!stop.stop_requested()) {
            if (!wait_readable(listening, accept_tick)) {
                continue;
            }

            const socket_handle client = ::accept(listening, nullptr, nullptr);
            if (client == no_socket) {
                continue;
            }

            answer(client, stop);
            close_socket(client);
        }
    }

    auto answer(socket_handle client, const std::stop_token& stop) -> void {
        connection peer{client, stop};

        auto request = peer.read_request();
        if (!request) {
            peer.respond(request.error());
            return;
        }

        auto exchange     = std::make_shared<http_exchange>();
        exchange->request = std::move(*request);
        auto reply        = exchange->reply.get_future();

        {
            const std::scoped_lock lock{mutex};
            pending.push_back(exchange);
        }

        const auto deadline = std::chrono::steady_clock::now() + reply_timeout;
        while (reply.wait_for(reply_tick) != std::future_status::ready) {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                exchange->abandoned = true;
                peer.respond(plain(504, "the editor did not answer in time"));
                return;
            }
        }

        peer.respond(reply.get());
    }
};

http_listener::http_listener(uint16 port)
    : impl_(std::make_unique<impl>()) {
    if (!impl_->runtime.started()) {
        impl_->failure = "the socket library did not start";
        return;
    }

    auto listening = open_loopback_listener(port);
    if (!listening) {
        impl_->failure = std::move(listening.error());
        return;
    }

    impl_->listening = *listening;
    impl_->worker    = std::jthread{[state = impl_.get()](const std::stop_token& stop) {
        state->serve(stop);
    }};

    log::info(lc_, "listening on 127.0.0.1:{}", port);
}

http_listener::~http_listener() {
    if (impl_->worker.joinable()) {
        impl_->worker.request_stop();
        impl_->worker.join();
    }
    close_socket(impl_->listening);
}

auto http_listener::is_listening() const -> bool {
    return impl_->listening != no_socket;
}

auto http_listener::failure() const -> const std::string& {
    return impl_->failure;
}

auto http_listener::take_pending() -> std::vector<std::shared_ptr<http_exchange>> {
    const std::scoped_lock lock{impl_->mutex};
    return std::exchange(impl_->pending, {});
}

}  // namespace vw::sculptor
