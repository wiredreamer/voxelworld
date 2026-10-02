export module vw.world:systems.extension;

import std;

import vw.core;
import vw.ecs;

namespace vw::ecs::detail {

auto next_system_id() -> uint32;

struct extension_slot {
    void* instance                 = nullptr;
    void (*update)(void*, float32) = nullptr;
    void (*shutdown)(void*)        = nullptr;
    void (*destroy)(void*)         = nullptr;
    std::string_view name;
};

struct component_hook {
    void* system                  = nullptr;
    void (*invoke)(void*, entity) = nullptr;
};

using component_hook_table = std::vector<std::vector<component_hook>>;

template <typename S, typename Tuple>
inline constexpr bool tuple_has = false;

template <typename S, typename... Ts>
inline constexpr bool tuple_has<S, std::tuple<Ts...>> = (std::same_as<S, Ts> || ...);

}  // namespace vw::ecs::detail

// см. docs/ENGINE.md#расширение-мира
export namespace vw::ecs {

enum class tick_stage : uint8 { before_engine, after_engine };

inline constexpr std::size_t tick_stage_count = 2;

template <typename... Cs>
struct component_list final {};

template <typename S>
auto system_id_of() -> uint32 {
    static const uint32 id = detail::next_system_id();
    return id;
}

template <typename S>
concept extension_system = requires(S& s, float32 delta_time) {
    { S::system_name } -> std::convertible_to<std::string_view>;
    { s.update(delta_time) } -> std::same_as<void>;
};

template <typename S>
concept has_observed_components = requires { typename S::observed_components; };

struct extension_timing {
    std::string_view name;
    tick_stage stage = tick_stage::before_engine;
    float32 ms       = 0.0f;
};

}  // namespace vw::ecs
