module vw.net;

import std;

import vw.core;

namespace vw::net {

namespace {

template <typename Number>
[[nodiscard]] auto parse_decimal(
    std::string_view text
) -> std::optional<Number> {
    if (text.empty() || text.size() > 5) {
        return std::nullopt;
    }
    if (text.size() > 1 && text.front() == '0') {
        return std::nullopt;
    }

    Number number{};
    const auto* end   = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, number);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return number;
}

}  // namespace

auto parse_ipv4(
    std::string_view text
) -> std::optional<ipv4_address> {
    ipv4_address address;

    std::size_t filled = 0;
    for (const auto part_range : std::views::split(text, '.')) {
        const std::string_view part{part_range.begin(), part_range.end()};
        if (filled == address.octets.size()) {
            return std::nullopt;
        }

        const auto octet = parse_decimal<uint8>(part);
        if (!octet) {
            return std::nullopt;
        }
        address.octets[filled] = *octet;
        ++filled;
    }

    return filled == address.octets.size() ? std::optional{address} : std::nullopt;
}

auto parse_endpoint(
    std::string_view text
) -> std::optional<endpoint> {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    const auto address = parse_ipv4(text.substr(0, colon));
    const auto port    = parse_decimal<uint16>(text.substr(colon + 1));
    if (!address || !port) {
        return std::nullopt;
    }
    return endpoint{.address = *address, .port = *port};
}

auto to_string(
    const ipv4_address& address
) -> std::string {
    const auto& octets = address.octets;
    return std::format("{}.{}.{}.{}", octets[0], octets[1], octets[2], octets[3]);
}

auto to_string(
    const endpoint& at
) -> std::string {
    return std::format("{}:{}", to_string(at.address), at.port);
}

}  // namespace vw::net
