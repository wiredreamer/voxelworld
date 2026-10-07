export module vw.world:systems.lod;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;
import :spatial;

export namespace vw::ecs {

class world;

struct lod_system_stats {
    uint32 entities  = 0;
    uint32 lowered   = 0;
    uint32 raised    = 0;
    float32 pick_ms  = 0.0F;

    std::array<uint32, asset::lod_level_count> at_level{};
};

// см. docs/lod-plan.md#переключение-прячется-в-тумане
inline constexpr float32 fog_near_share = 0.6F;
inline constexpr float32 fog_far_share  = 0.9F;

[[nodiscard]] constexpr auto lod_base_chunks_behind_fog(uint32 view_distance_columns) -> float32 {
    constexpr float32 fog_hiding_the_switch = 0.75F;
    constexpr float32 half_chunk            = 0.5F;
    constexpr float32 first_level_step      = 2.0F;

    const float32 switch_share =
        fog_near_share + ((fog_far_share - fog_near_share) * fog_hiding_the_switch);

    return ((switch_share * static_cast<float32>(view_distance_columns)) + half_chunk) /
           first_level_step;
}

// см. docs/lod-plan.md#выбор-уровня
class lod_system {
public:
    static constexpr std::string_view system_name = "lod";

    static constexpr float32 hysteresis_release = 0.92F;

    explicit lod_system(world& w);

    auto update(float32 dt) -> void;

    using level_distances = std::array<float32, asset::lod_level_count>;

    auto set_default_base_distance(float32 distance) -> void;
    [[nodiscard]] auto get_default_base_distance() const -> float32;

    auto set_level_distance(uint32 level, float32 distance) -> void;
    [[nodiscard]] auto get_level_distances() const -> const level_distances&;

    auto set_forced_level(int32 level) -> void;
    [[nodiscard]] auto get_forced_level() const -> int32;

    auto set_base_distance(entity ent, float32 distance) -> void;

    [[nodiscard]] auto get_stats() const -> const lod_system_stats&;

    [[nodiscard]] auto levels_changed_this_frame() const -> std::span<const entity>;

    [[nodiscard]] static auto pick_level(float32 distance, float32 base, uint32 current)
        -> uint32;

    [[nodiscard]] static auto pick_level(
        float32 distance, std::span<const float32> thresholds, uint32 current
    ) -> uint32;

    [[nodiscard]] static auto geometric_ladder(float32 base) -> level_distances;

private:
    [[nodiscard]] auto viewer_position_() const -> std::optional<vec3f>;
    [[nodiscard]] auto ladder_reaches_anything_() const -> bool;

    world* world_;
    float32 default_base_distance_ = 0.0F;
    level_distances level_distance_{};
    int32 forced_level_ = -1;
    bool any_override_  = false;
    std::vector<entity> changed_;
    lod_system_stats stats_;
};

}  // namespace vw::ecs
