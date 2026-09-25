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

// см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
inline constexpr int32 shell_edge_count      = 12;
inline constexpr int32 shell_corner_count    = 8;
inline constexpr int32 shell_direction_count = face_direction_count + shell_edge_count +
                                               shell_corner_count;

[[nodiscard]] constexpr auto shell_span(vec3i step) -> int32 {
    return static_cast<int32>(step.x != 0) + static_cast<int32>(step.y != 0) +
           static_cast<int32>(step.z != 0);
}

[[nodiscard]] constexpr auto shell_face(vec3i step) -> face_direction {
    const int32 axis = step.x != 0 ? 0 : (step.y != 0 ? 1 : 2);
    return static_cast<face_direction>((axis * 2) + (step[axis] > 0 ? 0 : 1));
}

[[nodiscard]] constexpr auto shell_free_axis(vec3i step) -> int32 {
    return step.x == 0 ? 0 : (step.y == 0 ? 1 : 2);
}

[[nodiscard]] constexpr auto shell_edge_index(vec3i step) -> int32 {
    const int32 free = shell_free_axis(step);
    const int32 a    = (free + 1) % 3;
    const int32 b    = (free + 2) % 3;
    return (free * 4) + (step[a] > 0 ? 2 : 0) + (step[b] > 0 ? 1 : 0);
}

[[nodiscard]] constexpr auto shell_corner_index(vec3i step) -> int32 {
    return (step.x > 0 ? 4 : 0) + (step.y > 0 ? 2 : 0) + (step.z > 0 ? 1 : 0);
}

[[nodiscard]] constexpr auto all_shell_steps() -> std::array<vec3i, shell_direction_count> {
    std::array<vec3i, shell_direction_count> steps{};

    std::size_t at = 0;
    for (int32 x = -1; x <= 1; ++x) {
        for (int32 y = -1; y <= 1; ++y) {
            for (int32 z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }
                steps[at++] = vec3i{x, y, z};
            }
        }
    }

    return steps;
}

}  // namespace vw
