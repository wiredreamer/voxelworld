export module vw.asset:anim.edit;

import std;

import vw.core;
import :anim.keyframe;
import :anim.channel;

export namespace vw::asset {

inline constexpr float32 same_instant_seconds = 1e-3F;

struct pose_key {
    float32 time = 0.0F;

    std::optional<vec3f> position;
    std::optional<quat> rotation;
    std::optional<vec3f> scale;

    math::interpolation_type interp = math::interpolation_type::linear;
    float32 tangent_in              = 0.0F;
    float32 tangent_out             = 1.0F;
};

auto put_key(animation_track& track, const pose_key& key) -> void;

auto drop_keys(
    animation_track& track, std::optional<animation_property> property, float32 from, float32 to
) -> uint32;

[[nodiscard]] auto count_keys(const animation_track& track) -> std::size_t;

}  // namespace vw::asset
