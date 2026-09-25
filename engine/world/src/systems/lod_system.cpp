module vw.world;

import std;
import vw.core;
import vw.asset;
import vw.ecs;

namespace vw::ecs {

lod_system::lod_system(
    world& w
)
    : world_{&w} {}

auto lod_system::set_default_base_distance(
    float32 distance
) -> void {
    default_base_distance_ = std::max(distance, 0.0F);
    level_distance_        = geometric_ladder(default_base_distance_);
}

auto lod_system::get_default_base_distance() const -> float32 {
    return default_base_distance_;
}

auto lod_system::set_level_distance(
    uint32 level, float32 distance
) -> void {
    if (level == 0 || level >= level_distance_.size()) {
        return;
    }
    level_distance_[level] = std::max(distance, 0.0F);
}

auto lod_system::get_level_distances() const -> const level_distances& {
    return level_distance_;
}

auto lod_system::geometric_ladder(
    float32 base
) -> level_distances {
    level_distances out{};
    for (uint32 level = 1; level < out.size(); ++level) {
        out[level] =
            base * static_cast<float32>(asset::lod_step_of(static_cast<int32>(level)));
    }
    return out;
}

auto lod_system::ladder_reaches_anything_() const -> bool {
    return std::ranges::any_of(level_distance_, [](float32 d) { return d > 0.0F; });
}

auto lod_system::set_forced_level(
    int32 level
) -> void {
    forced_level_ = level < 0 ? -1 : std::min(level, asset::lod_level_count - 1);
}

auto lod_system::get_forced_level() const -> int32 {
    return forced_level_;
}

auto lod_system::set_base_distance(
    entity ent, float32 distance
) -> void {
    auto& reg = world_->registry();
    if (!reg.has<lod_component>(ent)) {
        return;
    }

    reg.get<lod_component>(ent).base_distance_ = std::max(distance, 0.0F);
    any_override_                              = true;
}

auto lod_system::get_stats() const -> const lod_system_stats& {
    return stats_;
}

auto lod_system::levels_changed_this_frame() const -> std::span<const entity> {
    return changed_;
}

auto lod_system::pick_level(
    float32 distance, float32 base, uint32 current
) -> uint32 {
    const auto ladder = geometric_ladder(base);
    return pick_level(distance, ladder, current);
}

auto lod_system::pick_level(
    float32 distance, std::span<const float32> thresholds, uint32 current
) -> uint32 {
    if (thresholds.size() < 2) {
        return 0;
    }

    const auto top = static_cast<uint32>(thresholds.size()) - 1;

    uint32 level = std::min(current, top);

    while (level < top && thresholds[level + 1] > 0.0F &&
           distance >= thresholds[level + 1]) {
        ++level;
    }
    while (level > 0 &&
           (thresholds[level] <= 0.0F || distance < thresholds[level] * hysteresis_release)) {
        --level;
    }

    return level;
}

auto lod_system::viewer_position_() const -> std::optional<vec3f> {
    std::optional<vec3f> found;

    world_->registry().for_each<world_view_component, transform_component>(
        [&found](entity, const world_view_component&, const transform_component& tc) {
            if (!found) {
                found = tc.get_position();
            }
        }
    );

    return found;
}

auto lod_system::update(
    float32
) -> void {
    stats_ = {};

    if (forced_level_ < 0 && !ladder_reaches_anything_() && !any_override_) {
        return;
    }

    const auto viewer = viewer_position_();
    if (!viewer && forced_level_ < 0) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();

    auto& reg       = world_->registry();
    const vec3f eye = viewer.value_or(vec3f{});

    changed_.clear();

    reg.for_each<lod_component, model_component, transform_component>(
        [&](entity ent, const lod_component& lc, model_component& mc,
            const transform_component& tc) {
            const bool overridden = lc.base_distance_ > 0.0F;
            if (forced_level_ < 0 && !overridden && !ladder_reaches_anything_()) {
                return;
            }

            ++stats_.entities;

            const vec3f at = tc.get_position();

            const float32 dx       = at.x - eye.x;
            const float32 dz       = at.z - eye.z;
            const float32 distance = std::sqrt((dx * dx) + (dz * dz));

            const uint32 level = forced_level_ >= 0 ? static_cast<uint32>(forced_level_)
                : overridden ? pick_level(distance, lc.base_distance_, mc.lod_level_)
                             : pick_level(distance, level_distance_, mc.lod_level_);

            ++stats_.at_level[level];

            if (level == mc.lod_level_) {
                return;
            }

            if (level > mc.lod_level_) {
                ++stats_.raised;
            } else {
                ++stats_.lowered;
            }

            mc.lod_level_ = level;
            changed_.push_back(ent);
        }
    );

    for (const entity ent : changed_) {
        reg.notify_changed<model_component>(ent);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    );
    stats_.pick_ms = static_cast<float32>(elapsed.count()) / 1000.0F;
}

}  // namespace vw::ecs
