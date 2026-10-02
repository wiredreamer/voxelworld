export module vw.net:socket;

import std;

import vw.core;
import :endpoint;

namespace vw::net {

class socket_handle final {
public:
    socket_handle() = default;
    explicit socket_handle(uint64 native);
    ~socket_handle();

    socket_handle(socket_handle&& other) noexcept;
    auto operator=(socket_handle&& other) noexcept -> socket_handle&;

    socket_handle(const socket_handle&)                    = delete;
    auto operator=(const socket_handle&) -> socket_handle& = delete;

    [[nodiscard]] auto is_open() const -> bool;
    [[nodiscard]] auto native() const -> uint64;

    auto close() -> void;

private:
    static constexpr uint64 closed = std::numeric_limits<uint64>::max();

    uint64 native_ = closed;
};

}  // namespace vw::net

export namespace vw::net {

enum class socket_step : uint8 {
    start,
    create,
    bind,
    listen,
    connect,
    send,
    receive,
};

struct socket_error {
    socket_step step       = socket_step::create;
    int32 system_code      = 0;
    bool is_address_in_use = false;
};

[[nodiscard]] auto describe(const socket_error& error) -> std::string;

class tcp_stream final {
public:
    [[nodiscard]] static auto connect(const endpoint& remote)
        -> std::expected<tcp_stream, socket_error>;

    [[nodiscard]] auto wait_readable(std::chrono::milliseconds timeout) const -> bool;

    [[nodiscard]] auto receive(std::span<std::byte> into)
        -> std::expected<std::size_t, socket_error>;
    [[nodiscard]] auto send_all(std::span<const std::byte> bytes)
        -> std::expected<void, socket_error>;

    auto finish_sending() -> void;

private:
    friend class tcp_listener;

    explicit tcp_stream(socket_handle socket);

    socket_handle socket_;
};

class tcp_listener final {
public:
    [[nodiscard]] static auto open(const endpoint& local, int32 backlog = 8)
        -> std::expected<tcp_listener, socket_error>;

    [[nodiscard]] auto local_endpoint() const -> endpoint;

    [[nodiscard]] auto wait_readable(std::chrono::milliseconds timeout) const -> bool;
    [[nodiscard]] auto accept() -> std::optional<tcp_stream>;

private:
    explicit tcp_listener(socket_handle socket);

    socket_handle socket_;
};

struct datagram {
    std::size_t size = 0;
    endpoint sender;
};

class udp_socket final {
public:
    [[nodiscard]] static auto open(const endpoint& local)
        -> std::expected<udp_socket, socket_error>;

    [[nodiscard]] auto local_endpoint() const -> endpoint;

    [[nodiscard]] auto wait_readable(std::chrono::milliseconds timeout) const -> bool;

    [[nodiscard]] auto send_to(const endpoint& remote, std::span<const std::byte> bytes)
        -> std::expected<std::size_t, socket_error>;
    [[nodiscard]] auto receive_from(std::span<std::byte> into)
        -> std::expected<datagram, socket_error>;

private:
    explicit udp_socket(socket_handle socket);

    socket_handle socket_;
};

}  // namespace vw::net
