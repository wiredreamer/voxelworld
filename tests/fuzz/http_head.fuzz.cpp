import std;

import vw.core;
import vw.net;

extern "C" auto LLVMFuzzerTestOneInput(
    const vw::uint8* data, std::size_t size
) -> int {
    const std::string_view text{reinterpret_cast<const char*>(data), size};

    const auto parsed = vw::net::http::parse_head(text);
    if (!parsed.has_value()) {
        return 0;
    }

    if (parsed->method.empty() || parsed->target.empty() || !parsed->body.empty()) {
        std::abort();
    }

    for (const auto& [name, value] : parsed->fields) {
        const bool has_upper =
            std::ranges::any_of(name, [](char symbol) { return symbol >= 'A' && symbol <= 'Z'; });
        if (has_upper || parsed->value_of(name) != value) {
            std::abort();
        }
    }

    static_cast<void>(vw::net::http::is_local_authority(parsed->value_of("host")));
    static_cast<void>(vw::net::http::is_local_origin(parsed->value_of("origin")));

    return 0;
}
