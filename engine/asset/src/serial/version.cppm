module vw.asset:serial.version;

import std;

import vw.core;

namespace vw::asset::detail {

inline auto read_header_version(std::istringstream& iss) -> std::optional<std::string> {
    std::string token;
    while (iss >> token) {
        if (token != "Version") {
            continue;
        }

        std::string version;
        if (!(iss >> version)) {
            return std::nullopt;
        }
        return version;
    }
    return std::nullopt;
}

inline auto major_version_differs(std::string_view version, std::string_view expected) -> bool {
    const auto major_of = [](std::string_view text) -> std::string_view {
        return text.substr(0, text.find('.'));
    };
    return major_of(version) != major_of(expected);
}

}  // namespace vw::asset::detail
