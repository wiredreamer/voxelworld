module vw.gfx:vk;

import std;
import vulkan;
import vw.core;

namespace vw::gfx {

[[noreturn]]
inline auto vk_panic(vk::Result result, std::string_view what) -> void {
    constexpr log::log_category lc{"vulkan"};
    log::critical(lc, "{} failed: {}", what, vk::to_string(result));
    std::terminate();
}

template <typename T>
auto vk_must(std::expected<T, vk::Result>&& rv, std::string_view what) -> T {
    if (!rv.has_value()) [[unlikely]] {
        vk_panic(rv.error(), what);
    }
    return std::move(*rv);
}

inline auto vk_must(std::expected<void, vk::Result>&& rv, std::string_view what) -> void {
    if (!rv.has_value()) [[unlikely]] {
        vk_panic(rv.error(), what);
    }
}

inline auto vk_must(vk::Result result, std::string_view what) -> void {
    if (result != vk::Result::eSuccess) [[unlikely]] {
        vk_panic(result, what);
    }
}

template <typename T>
    requires requires(T rv) {
        rv.result;
        rv.value;
    }
auto vk_must(T&& rv, std::string_view what) -> decltype(auto) {
    if (rv.result != vk::Result::eSuccess) [[unlikely]] {
        vk_panic(rv.result, what);
    }
    return std::move(rv.value);
}

}  // namespace vw::gfx
