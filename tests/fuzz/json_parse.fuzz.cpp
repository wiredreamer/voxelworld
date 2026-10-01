import std;

import vw.core;

extern "C" auto LLVMFuzzerTestOneInput(const vw::uint8* data, std::size_t size) -> int {
    const std::string_view text{reinterpret_cast<const char*>(data), size};

    const auto parsed = vw::json::parse(text);
    if (!parsed.has_value()) {
        return 0;
    }

    const auto reparsed = vw::json::parse(vw::json::dump(*parsed));
    if (!reparsed.has_value() || !(*reparsed == *parsed)) {
        std::abort();
    }

    const auto indented = vw::json::parse(vw::json::dump(*parsed, {.indent = 2}));
    if (!indented.has_value() || !(*indented == *parsed)) {
        std::abort();
    }

    return 0;
}
