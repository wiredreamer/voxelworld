module;

#ifndef VW_BUILD_CONFIG
#define VW_BUILD_CONFIG "Unspecified"
#endif

export module vw.core:build_info;

import std;

export namespace vw::build {

inline constexpr std::string_view config = VW_BUILD_CONFIG;

[[nodiscard]] inline auto titled(std::string_view title) -> std::string {
    return std::format("{} [{}]", title, config);
}

}  // namespace vw::build
