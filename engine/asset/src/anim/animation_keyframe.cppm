export module vw.asset:anim.keyframe;

import std;

import vw.core;

export namespace vw::asset {

enum class animation_state : uint8 { stopped, playing, paused };

enum class animation_loop_mode : uint8 { once, loop, ping_pong };

enum class animation_property : uint8 { position, rotation, scale };

struct transition {
    float32 duration                = 0.0F;
    math::interpolation_type interp = math::interpolation_type::linear;
    float32 tangent_in              = 0.0F;
    float32 tangent_out             = 1.0F;

    [[nodiscard]] auto operator==(const transition& other) const -> bool = default;
};

template <animation_property Prop>
struct animation_property_traits;

template <>
struct animation_property_traits<animation_property::position> {
    using type = vec3f;
};

template <>
struct animation_property_traits<animation_property::rotation> {
    using type = quat;
};

template <>
struct animation_property_traits<animation_property::scale> {
    using type = vec3f;
};

inline constexpr uint32 invalid_keyframe_id = std::numeric_limits<uint32>::max();

template <typename T>
class animation_channel;

template <typename T>
struct keyframe {
    keyframe() = default;
    keyframe(float32 time, T value) : time(time), value(std::move(value)) {}

    float32 time = 0.0F;
    T value{};
    math::interpolation_type interp = math::interpolation_type::linear;
    float32 tangent_in             = 0.0F;
    float32 tangent_out            = 1.0F;

    [[nodiscard]] auto id() const -> uint32 {
        return id_;
    }

    [[nodiscard]] auto operator<(const keyframe& other) const -> bool {
        return time < other.time;
    }

    [[nodiscard]] auto operator==(const keyframe& other) const -> bool {
        return time == other.time;
    }

private:
    friend class animation_channel<T>;
    uint32 id_ = invalid_keyframe_id;
};

using keyframe_vec3f = keyframe<vec3f>;
using keyframe_quat  = keyframe<quat>;

}  // namespace vw::asset
