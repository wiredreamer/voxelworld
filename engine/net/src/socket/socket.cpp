module;

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

module vw.net;

import std;

import vw.core;

namespace vw::net {

namespace {

#ifdef _WIN32
using native_socket               = SOCKET;
using address_length              = int;
constexpr native_socket no_socket = INVALID_SOCKET;
#else
using native_socket               = int;
using address_length              = socklen_t;
constexpr native_socket no_socket = -1;
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

[[nodiscard]] auto runtime_started() -> bool {
    static const socket_runtime runtime;
    return runtime.started();
}

[[nodiscard]] auto last_system_code() -> int32 {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

[[nodiscard]] auto is_address_in_use(
    int32 code
) -> bool {
#ifdef _WIN32
    return code == WSAEADDRINUSE || code == WSAEACCES;
#else
    return code == EADDRINUSE;
#endif
}

[[nodiscard]] auto failed_at(
    socket_step step
) -> std::unexpected<socket_error> {
    const int32 code = last_system_code();
    return std::unexpected(
        socket_error{
            .step              = step,
            .system_code       = code,
            .is_address_in_use = step == socket_step::bind && is_address_in_use(code),
        }
    );
}

[[nodiscard]] auto native_of(
    const socket_handle& socket
) -> native_socket {
    return static_cast<native_socket>(socket.native());
}

[[nodiscard]] auto created(
    int type, int protocol
) -> std::expected<socket_handle, socket_error> {
    if (!runtime_started()) {
        return std::unexpected(
            socket_error{
                .step              = socket_step::start,
                .system_code       = 0,
                .is_address_in_use = false,
            }
        );
    }

    const native_socket socket = ::socket(AF_INET, type, protocol);
    if (socket == no_socket) {
        return failed_at(socket_step::create);
    }
    return socket_handle{static_cast<uint64>(socket)};
}

[[nodiscard]] auto system_address(
    const endpoint& at
) -> sockaddr_in {
    const auto& octets      = at.address.octets;
    const uint32 host_order = (static_cast<uint32>(octets[0]) << 24U) |
        (static_cast<uint32>(octets[1]) << 16U) | (static_cast<uint32>(octets[2]) << 8U) |
        static_cast<uint32>(octets[3]);

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(at.port);
    address.sin_addr.s_addr = htonl(host_order);
    return address;
}

[[nodiscard]] auto endpoint_of(
    const sockaddr_in& address
) -> endpoint {
    const uint32 host_order = ntohl(address.sin_addr.s_addr);
    return endpoint{
        .address = ipv4_address{{
            static_cast<uint8>(host_order >> 24U),
            static_cast<uint8>(host_order >> 16U),
            static_cast<uint8>(host_order >> 8U),
            static_cast<uint8>(host_order),
        }},
        .port    = ntohs(address.sin_port),
    };
}

[[nodiscard]] auto bound(
    socket_handle socket, const endpoint& local
) -> std::expected<socket_handle, socket_error> {
    const sockaddr_in address = system_address(local);
    const auto* generic       = reinterpret_cast<const sockaddr*>(&address);
    if (::bind(native_of(socket), generic, sizeof(address)) != 0) {
        return failed_at(socket_step::bind);
    }
    return socket;
}

[[nodiscard]] auto local_endpoint_of(
    const socket_handle& socket
) -> endpoint {
    sockaddr_in address{};
    address_length length = sizeof(address);
    if (::getsockname(native_of(socket), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return endpoint{};
    }
    return endpoint_of(address);
}

[[nodiscard]] auto is_readable(
    const socket_handle& socket, std::chrono::milliseconds timeout
) -> bool {
    const auto timeout_ms = static_cast<int>(timeout.count());
#ifdef _WIN32
    WSAPOLLFD entry{};
    entry.fd     = native_of(socket);
    entry.events = POLLRDNORM;
    return WSAPoll(&entry, 1, timeout_ms) > 0;
#else
    pollfd entry{};
    entry.fd     = native_of(socket);
    entry.events = POLLIN;
    return ::poll(&entry, 1, timeout_ms) > 0;
#endif
}

auto forbid_address_sharing(
    const socket_handle& socket
) -> void {
    const int enabled = 1;
#ifdef _WIN32
    setsockopt(
        native_of(socket),
        SOL_SOCKET,
        SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))
    );
#else
    setsockopt(native_of(socket), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
}

#ifdef _WIN32
[[nodiscard]] auto clamped_length(
    std::size_t size
) -> int {
    return static_cast<int>(std::min<std::size_t>(size, std::numeric_limits<int>::max()));
}

constexpr int send_flags = 0;
#else
[[nodiscard]] auto clamped_length(
    std::size_t size
) -> std::size_t {
    return size;
}

constexpr int send_flags = MSG_NOSIGNAL;
#endif

[[nodiscard]] auto step_name(
    socket_step step
) -> std::string_view {
    switch (step) {
        case socket_step::start:
            return "start the socket library";
        case socket_step::create:
            return "create a socket";
        case socket_step::bind:
            return "bind";
        case socket_step::listen:
            return "listen";
        case socket_step::connect:
            return "connect";
        case socket_step::send:
            return "send";
        case socket_step::receive:
            return "receive";
    }
    return "use a socket";
}

}  // namespace

socket_handle::socket_handle(
    uint64 native
)
    : native_(native) {}

socket_handle::~socket_handle() {
    close();
}

socket_handle::socket_handle(
    socket_handle&& other
) noexcept
    : native_(std::exchange(other.native_, closed)) {}

auto socket_handle::operator=(
    socket_handle&& other
) noexcept -> socket_handle& {
    if (this != &other) {
        close();
        native_ = std::exchange(other.native_, closed);
    }
    return *this;
}

auto socket_handle::is_open() const -> bool {
    return native_ != closed;
}

auto socket_handle::native() const -> uint64 {
    return native_;
}

auto socket_handle::close() -> void {
    if (native_ == closed) {
        return;
    }
#ifdef _WIN32
    closesocket(static_cast<native_socket>(native_));
#else
    ::close(static_cast<native_socket>(native_));
#endif
    native_ = closed;
}

auto describe(
    const socket_error& error
) -> std::string {
    if (error.step == socket_step::start) {
        return "the socket library did not start";
    }
    if (error.is_address_in_use) {
        return "the address is taken";
    }
    return std::format("cannot {}, socket error {}", step_name(error.step), error.system_code);
}

tcp_stream::tcp_stream(
    socket_handle socket
)
    : socket_(std::move(socket)) {}

auto tcp_stream::connect(
    const endpoint& remote
) -> std::expected<tcp_stream, socket_error> {
    auto socket = created(SOCK_STREAM, IPPROTO_TCP);
    if (!socket) {
        return std::unexpected(socket.error());
    }

    const sockaddr_in address = system_address(remote);
    const auto* generic       = reinterpret_cast<const sockaddr*>(&address);
    if (::connect(native_of(*socket), generic, sizeof(address)) != 0) {
        return failed_at(socket_step::connect);
    }
    return tcp_stream{std::move(*socket)};
}

auto tcp_stream::wait_readable(
    std::chrono::milliseconds timeout
) const -> bool {
    return is_readable(socket_, timeout);
}

auto tcp_stream::receive(
    std::span<std::byte> into
) -> std::expected<std::size_t, socket_error> {
    auto* bytes         = reinterpret_cast<char*>(into.data());
    const auto received = ::recv(native_of(socket_), bytes, clamped_length(into.size()), 0);
    if (received < 0) {
        return failed_at(socket_step::receive);
    }
    return static_cast<std::size_t>(received);
}

auto tcp_stream::send_all(
    std::span<const std::byte> bytes
) -> std::expected<void, socket_error> {
    while (!bytes.empty()) {
        const auto* data = reinterpret_cast<const char*>(bytes.data());
        const auto sent =
            ::send(native_of(socket_), data, clamped_length(bytes.size()), send_flags);
        if (sent <= 0) {
            return failed_at(socket_step::send);
        }
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
    return {};
}

auto tcp_stream::finish_sending() -> void {
#ifdef _WIN32
    shutdown(native_of(socket_), SD_SEND);
#else
    ::shutdown(native_of(socket_), SHUT_WR);
#endif
}

tcp_listener::tcp_listener(
    socket_handle socket
)
    : socket_(std::move(socket)) {}

auto tcp_listener::open(
    const endpoint& local, int32 backlog
) -> std::expected<tcp_listener, socket_error> {
    auto socket = created(SOCK_STREAM, IPPROTO_TCP);
    if (!socket) {
        return std::unexpected(socket.error());
    }
    forbid_address_sharing(*socket);

    auto listening = bound(std::move(*socket), local);
    if (!listening) {
        return std::unexpected(listening.error());
    }
    if (::listen(native_of(*listening), backlog) != 0) {
        return failed_at(socket_step::listen);
    }
    return tcp_listener{std::move(*listening)};
}

auto tcp_listener::local_endpoint() const -> endpoint {
    return local_endpoint_of(socket_);
}

auto tcp_listener::wait_readable(
    std::chrono::milliseconds timeout
) const -> bool {
    return is_readable(socket_, timeout);
}

auto tcp_listener::accept() -> std::optional<tcp_stream> {
    const native_socket client = ::accept(native_of(socket_), nullptr, nullptr);
    if (client == no_socket) {
        return std::nullopt;
    }
    return tcp_stream{socket_handle{static_cast<uint64>(client)}};
}

udp_socket::udp_socket(
    socket_handle socket
)
    : socket_(std::move(socket)) {}

auto udp_socket::open(
    const endpoint& local
) -> std::expected<udp_socket, socket_error> {
    auto socket = created(SOCK_DGRAM, IPPROTO_UDP);
    if (!socket) {
        return std::unexpected(socket.error());
    }

    auto ready = bound(std::move(*socket), local);
    if (!ready) {
        return std::unexpected(ready.error());
    }
    return udp_socket{std::move(*ready)};
}

auto udp_socket::local_endpoint() const -> endpoint {
    return local_endpoint_of(socket_);
}

auto udp_socket::wait_readable(
    std::chrono::milliseconds timeout
) const -> bool {
    return is_readable(socket_, timeout);
}

auto udp_socket::send_to(
    const endpoint& remote, std::span<const std::byte> bytes
) -> std::expected<std::size_t, socket_error> {
    const sockaddr_in address = system_address(remote);
    const auto* generic       = reinterpret_cast<const sockaddr*>(&address);
    const auto* data          = reinterpret_cast<const char*>(bytes.data());

    const auto sent = ::sendto(
        native_of(socket_), data, clamped_length(bytes.size()), send_flags, generic, sizeof(address)
    );
    if (sent < 0) {
        return failed_at(socket_step::send);
    }
    return static_cast<std::size_t>(sent);
}

auto udp_socket::receive_from(
    std::span<std::byte> into
) -> std::expected<datagram, socket_error> {
    sockaddr_in address{};
    address_length length = sizeof(address);
    auto* bytes           = reinterpret_cast<char*>(into.data());

    const auto received = ::recvfrom(
        native_of(socket_),
        bytes,
        clamped_length(into.size()),
        0,
        reinterpret_cast<sockaddr*>(&address),
        &length
    );
    if (received < 0) {
        return failed_at(socket_step::receive);
    }
    return datagram{.size = static_cast<std::size_t>(received), .sender = endpoint_of(address)};
}

}  // namespace vw::net
