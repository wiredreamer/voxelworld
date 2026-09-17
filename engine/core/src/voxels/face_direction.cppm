export module vw.core:voxels.face_direction;

import std;

import :types;
import :vector;

export namespace vw {

enum class face_direction : uint8 {
    pos_x,
    neg_x,
    pos_y,
    neg_y,
    pos_z,
    neg_z,
};

inline constexpr int32 face_direction_count = 6;

inline constexpr std::array<face_direction, face_direction_count> all_face_directions{
    face_direction::pos_x, face_direction::neg_x, face_direction::pos_y,
    face_direction::neg_y, face_direction::pos_z, face_direction::neg_z,
};

[[nodiscard]] constexpr auto axis_of(face_direction face) -> int32 {
    return std::to_underlying(face) / 2;
}

[[nodiscard]] constexpr auto is_positive(face_direction face) -> bool {
    return std::to_underlying(face) % 2 == 0;
}

[[nodiscard]] constexpr auto opposite(face_direction face) -> face_direction {
    return static_cast<face_direction>(std::to_underlying(face) ^ 1U);
}

[[nodiscard]] constexpr auto offset_of(face_direction face) -> vec3i {
    const int32 step = is_positive(face) ? 1 : -1;
    switch (axis_of(face)) {
        case 0:
            return {step, 0, 0};
        case 1:
            return {0, step, 0};
        default:
            return {0, 0, step};
    }
}

[[nodiscard]] constexpr auto boundary_layer(face_direction face, int32 extent) -> int32 {
    return is_positive(face) ? extent - 1 : 0;
}

[[nodiscard]] constexpr auto project_onto_face_plane(face_direction face, vec3i point) -> vec2i {
    switch (axis_of(face)) {
        case 0:
            return {point.y, point.z};
        case 1:
            return {point.x, point.z};
        default:
            return {point.x, point.y};
    }
}

[[nodiscard]] constexpr auto lift_off_face_plane(
    face_direction face, vec2i on_plane, int32 depth
) -> vec3i {
    switch (axis_of(face)) {
        case 0:
            return {depth, on_plane.x, on_plane.y};
        case 1:
            return {on_plane.x, depth, on_plane.y};
        default:
            return {on_plane.x, on_plane.y, depth};
    }
}

[[nodiscard]] constexpr auto face_bit(face_direction face) -> uint8 {
    return static_cast<uint8>(1U << std::to_underlying(face));
}

inline constexpr auto all_faces_mask = static_cast<uint8>((1U << face_direction_count) - 1U);

template <typename T>
struct per_face {
    using iterator       = typename std::array<T, face_direction_count>::iterator;
    using const_iterator = typename std::array<T, face_direction_count>::const_iterator;

    std::array<T, face_direction_count> values{};

    [[nodiscard]] constexpr auto operator[](face_direction face) -> T& {
        return values[std::to_underlying(face)];
    }

    [[nodiscard]] constexpr auto operator[](face_direction face) const -> const T& {
        return values[std::to_underlying(face)];
    }

    [[nodiscard]] constexpr auto begin() -> iterator {
        return values.begin();
    }

    [[nodiscard]] constexpr auto end() -> iterator {
        return values.end();
    }

    [[nodiscard]] constexpr auto begin() const -> const_iterator {
        return values.begin();
    }

    [[nodiscard]] constexpr auto end() const -> const_iterator {
        return values.end();
    }

    auto operator==(const per_face&) const -> bool = default;
};

}  // namespace vw
