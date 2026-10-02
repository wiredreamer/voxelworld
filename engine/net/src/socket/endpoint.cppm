export module vw.net:endpoint;

import std;

import vw.core;

export namespace vw::net {

struct ipv4_address {
    std::array<uint8, 4> octets{};

    [[nodiscard]] static constexpr auto loopback() -> ipv4_address {
        return ipv4_address{{127, 0, 0, 1}};
    }

    [[nodiscard]] static constexpr auto any() -> ipv4_address {
        return ipv4_address{};
    }

    [[nodiscard]] constexpr auto is_loopback() const -> bool {
        return octets[0] == 127;
    }

    [[nodiscard]] auto operator==(const ipv4_address&) const -> bool = default;
};

struct endpoint {
    ipv4_address address;
    uint16 port = 0;

    [[nodiscard]] auto operator==(const endpoint&) const -> bool = default;
};

[[nodiscard]] constexpr auto loopback(
    uint16 port
) -> endpoint {
    return endpoint{.address = ipv4_address::loopback(), .port = port};
}

[[nodiscard]] constexpr auto any_address(
    uint16 port
) -> endpoint {
    return endpoint{.address = ipv4_address::any(), .port = port};
}

[[nodiscard]] auto parse_ipv4(std::string_view text) -> std::optional<ipv4_address>;
[[nodiscard]] auto parse_endpoint(std::string_view text) -> std::optional<endpoint>;

[[nodiscard]] auto to_string(const ipv4_address& address) -> std::string;
[[nodiscard]] auto to_string(const endpoint& at) -> std::string;

}  // namespace vw::net
