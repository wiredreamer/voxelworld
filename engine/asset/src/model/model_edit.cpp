module vw.asset;

import std;
import vw.core;

namespace vw::asset {

auto voxel_batch::apply_to(model& target) const -> void {
    model_writer writer{target};
    writer.apply(*this);
}

auto occupied_bounds(
    const model& source
) -> std::optional<voxel_bounds> {
    const auto size = source.size();

    voxel_bounds bounds{.min = size, .max = vec3i{-1, -1, -1}};

    for (int32 x = 0; x < size.x; ++x) {
        for (int32 y = 0; y < size.y; ++y) {
            for (int32 z = 0; z < size.z; ++z) {
                if (source.is_empty(x, y, z)) {
                    continue;
                }

                bounds.min.x = std::min(bounds.min.x, x);
                bounds.min.y = std::min(bounds.min.y, y);
                bounds.min.z = std::min(bounds.min.z, z);
                bounds.max.x = std::max(bounds.max.x, x);
                bounds.max.y = std::max(bounds.max.y, y);
                bounds.max.z = std::max(bounds.max.z, z);
            }
        }
    }

    if (bounds.max.x < bounds.min.x) {
        return std::nullopt;
    }

    return bounds;
}

auto trimmed(
    const model& source, model_registry& registry
) -> std::shared_ptr<model> {
    const auto bounds = occupied_bounds(source);
    if (!bounds) {
        return nullptr;
    }

    const auto new_size = bounds->size();
    if (new_size == source.size()) {
        return nullptr;
    }

    auto result = registry.create_unnamed(new_size);

    {
        model_writer writer{*result};

        for (int32 x = 0; x < new_size.x; ++x) {
            for (int32 y = 0; y < new_size.y; ++y) {
                for (int32 z = 0; z < new_size.z; ++z) {
                    const auto from =
                        vec3i{x + bounds->min.x, y + bounds->min.y, z + bounds->min.z};
                    writer.set(x, y, z, source.get_matter(from));
                }
            }
        }
    }

    result->set_pivot(
        source.pivot() -
        vec3f{
            static_cast<float32>(bounds->min.x),
            static_cast<float32>(bounds->min.y),
            static_cast<float32>(bounds->min.z),
        }
    );

    return result;
}

namespace {

constexpr int32 quarter_turns_in_full = 4;

constexpr auto index_of(voxel_axis axis) -> std::size_t {
    return std::to_underlying(axis);
}

constexpr auto next_axis(voxel_axis axis) -> voxel_axis {
    return static_cast<voxel_axis>((std::to_underlying(axis) + 1) % 3);
}

auto composed(const voxel_orientation& first, const voxel_orientation& then)
    -> voxel_orientation {
    voxel_orientation result;

    for (std::size_t i = 0; i < 3; ++i) {
        const auto via        = index_of(then.source_axis[i]);
        result.source_axis[i] = first.source_axis[via];
        result.flipped[i]     = then.flipped[i] != first.flipped[via];
    }

    return result;
}

auto quarter_turn(voxel_axis axis) -> voxel_orientation {
    const auto from = next_axis(axis);
    const auto to   = next_axis(from);

    voxel_orientation result;
    result.source_axis[index_of(to)]   = from;
    result.source_axis[index_of(from)] = to;
    result.flipped[index_of(from)]     = true;

    return result;
}

auto source_position(vec3i at, vec3i source_size, const voxel_orientation& how) -> vec3i {
    vec3i from;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto axis = index_of(how.source_axis[i]);
        from[axis]      = how.flipped[i] ? source_size[axis] - 1 - at[i] : at[i];
    }
    return from;
}

auto to_float(vec3i value) -> vec3f {
    return vec3f{
        static_cast<float32>(value.x),
        static_cast<float32>(value.y),
        static_cast<float32>(value.z),
    };
}

auto contains(const voxel_bounds& box, vec3i pos) -> bool {
    return pos.x >= box.min.x && pos.x <= box.max.x && pos.y >= box.min.y &&
           pos.y <= box.max.y && pos.z >= box.min.z && pos.z <= box.max.z;
}

}  // namespace

auto mirrored_orientation(
    voxel_axis axis
) -> voxel_orientation {
    voxel_orientation result;
    result.flipped[index_of(axis)] = true;
    return result;
}

auto rotated_orientation(
    voxel_axis axis, int32 quarter_turns
) -> voxel_orientation {
    const auto turns =
        ((quarter_turns % quarter_turns_in_full) + quarter_turns_in_full) % quarter_turns_in_full;
    const auto step = quarter_turn(axis);

    voxel_orientation result;
    for (int32 i = 0; i < turns; ++i) {
        result = composed(result, step);
    }

    return result;
}

auto reoriented(
    const model& source, const voxel_orientation& how, model_registry& registry
) -> std::shared_ptr<model> {
    const auto source_size  = source.size();
    const auto source_pivot = source.pivot();

    vec3i new_size;
    vec3f new_pivot;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto from = index_of(how.source_axis[i]);
        new_size[i]     = source_size[from];
        new_pivot[i]    = how.flipped[i]
                              ? static_cast<float32>(source_size[from]) - source_pivot[from]
                              : source_pivot[from];
    }

    auto result = registry.create_unnamed(new_size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < new_size.x; ++at.x) {
            for (at.y = 0; at.y < new_size.y; ++at.y) {
                for (at.z = 0; at.z < new_size.z; ++at.z) {
                    const auto value = source.get_matter(source_position(at, source_size, how));
                    if (!value.is_empty()) {
                        writer.set(at, value);
                    }
                }
            }
        }
    }

    result->set_pivot(new_pivot);

    return result;
}

auto clamped(
    const voxel_bounds& region, vec3i size
) -> std::optional<voxel_bounds> {
    voxel_bounds result;
    for (std::size_t i = 0; i < 3; ++i) {
        result.min[i] = std::max(region.min[i], 0);
        result.max[i] = std::min(region.max[i], size[i] - 1);
        if (result.max[i] < result.min[i]) {
            return std::nullopt;
        }
    }
    return result;
}

auto copied(
    const model& source, const voxel_bounds& region
) -> voxel_clip {
    const auto inside = clamped(region, source.size());
    if (!inside) {
        return voxel_clip{};
    }

    voxel_clip clip;
    clip.size              = inside->size();
    clip.corner_from_pivot = to_float(inside->min) - source.pivot();
    clip.voxels.resize(static_cast<std::size_t>(clip.size.x * clip.size.y * clip.size.z));

    vec3i at;
    for (at.x = 0; at.x < clip.size.x; ++at.x) {
        for (at.y = 0; at.y < clip.size.y; ++at.y) {
            for (at.z = 0; at.z < clip.size.z; ++at.z) {
                clip.voxels[clip.index_of(at)] = source.get_matter(
                    at.x + inside->min.x, at.y + inside->min.y, at.z + inside->min.z
                );
            }
        }
    }

    return clip;
}

auto erased(
    const model& source, const voxel_bounds& region, model_registry& registry
) -> std::shared_ptr<model> {
    const auto size = source.size();
    auto result     = registry.create_unnamed(size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < size.x; ++at.x) {
            for (at.y = 0; at.y < size.y; ++at.y) {
                for (at.z = 0; at.z < size.z; ++at.z) {
                    if (contains(region, at)) {
                        continue;
                    }

                    const auto value = source.get_matter(at);
                    if (!value.is_empty()) {
                        writer.set(at, value);
                    }
                }
            }
        }
    }

    result->set_pivot(source.pivot());

    return result;
}

auto filled(
    const model& source, const voxel_bounds& region, matter value, fill_scope scope,
    model_registry& registry
) -> std::shared_ptr<model> {
    const auto size = source.size();
    auto result     = registry.create_unnamed(size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < size.x; ++at.x) {
            for (at.y = 0; at.y < size.y; ++at.y) {
                for (at.z = 0; at.z < size.z; ++at.z) {
                    auto cell = source.get_matter(at);

                    const bool takes = contains(region, at) &&
                                       (scope == fill_scope::every_cell || !cell.is_empty());
                    if (takes) {
                        cell = scope == fill_scope::solid_only ? matter{value.color, cell.made_of} : value;
                    }

                    if (!cell.is_empty()) {
                        writer.set(at, cell);
                    }
                }
            }
        }
    }

    result->set_pivot(source.pivot());

    return result;
}

auto occupied_bounds(
    const voxel_clip& clip
) -> std::optional<voxel_bounds> {
    voxel_bounds bounds{.min = clip.size, .max = vec3i{-1, -1, -1}};

    vec3i at;
    for (at.x = 0; at.x < clip.size.x; ++at.x) {
        for (at.y = 0; at.y < clip.size.y; ++at.y) {
            for (at.z = 0; at.z < clip.size.z; ++at.z) {
                if (clip.at(at).is_empty()) {
                    continue;
                }

                for (std::size_t i = 0; i < 3; ++i) {
                    bounds.min[i] = std::min(bounds.min[i], at[i]);
                    bounds.max[i] = std::max(bounds.max[i], at[i]);
                }
            }
        }
    }

    if (bounds.max.x < bounds.min.x) {
        return std::nullopt;
    }

    return bounds;
}

auto paste_origin(
    const model& target, const voxel_clip& clip
) -> vec3i {
    const auto corner = target.pivot() + clip.corner_from_pivot;
    return vec3i{
        static_cast<int32>(std::lround(corner.x)),
        static_cast<int32>(std::lround(corner.y)),
        static_cast<int32>(std::lround(corner.z)),
    };
}

auto pasted(
    const model& target, const voxel_clip& clip, vec3i origin, paste_mode mode,
    model_registry& registry
) -> std::shared_ptr<model> {
    const auto target_size = target.size();
    const auto target_box  = voxel_bounds{
        .min = vec3i{0, 0, 0},
        .max = vec3i{target_size.x - 1, target_size.y - 1, target_size.z - 1},
    };
    const auto clip_box = voxel_bounds{
        .min = origin,
        .max = vec3i{
            origin.x + clip.size.x - 1, origin.y + clip.size.y - 1, origin.z + clip.size.z - 1
        },
    };

    auto grown = target_box;
    if (const auto solid = occupied_bounds(clip)) {
        for (std::size_t i = 0; i < 3; ++i) {
            grown.min[i] = std::min(grown.min[i], origin[i] + solid->min[i]);
            grown.max[i] = std::max(grown.max[i], origin[i] + solid->max[i]);
        }
    }

    const auto new_size = grown.size();
    auto result         = registry.create_unnamed(new_size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < new_size.x; ++at.x) {
            for (at.y = 0; at.y < new_size.y; ++at.y) {
                for (at.z = 0; at.z < new_size.z; ++at.z) {
                    const auto in_target = vec3i{
                        at.x + grown.min.x, at.y + grown.min.y, at.z + grown.min.z
                    };

                    matter value;
                    bool taken = false;

                    if (!clip.empty() && contains(clip_box, in_target)) {
                        value = clip.at(vec3i{
                            in_target.x - origin.x, in_target.y - origin.y,
                            in_target.z - origin.z
                        });
                        taken = mode == paste_mode::replace || !value.is_empty();
                    }

                    if (!taken && contains(target_box, in_target)) {
                        value = target.get_matter(in_target);
                    }

                    if (!value.is_empty()) {
                        writer.set(at, value);
                    }
                }
            }
        }
    }

    result->set_pivot(target.pivot() - to_float(grown.min));

    return result;
}

auto reoriented(
    const voxel_clip& source, const voxel_orientation& how
) -> voxel_clip {
    voxel_clip result;
    for (std::size_t i = 0; i < 3; ++i) {
        result.size[i] = source.size[index_of(how.source_axis[i])];
    }

    const auto shrink        = to_float(source.size) - to_float(result.size);
    result.corner_from_pivot = source.corner_from_pivot + shrink * 0.5F;
    result.voxels.resize(source.voxels.size());

    vec3i at;
    for (at.x = 0; at.x < result.size.x; ++at.x) {
        for (at.y = 0; at.y < result.size.y; ++at.y) {
            for (at.z = 0; at.z < result.size.z; ++at.z) {
                result.voxels[result.index_of(at)] =
                    source.at(source_position(at, source.size, how));
            }
        }
    }

    return result;
}

auto contains(
    vec3i size, vec3i position
) -> bool {
    return position.x >= 0 && position.x < size.x && position.y >= 0 && position.y < size.y &&
           position.z >= 0 && position.z < size.z;
}

auto edited(
    const model& source, std::span<const voxel_edit> edits, model_registry& registry
) -> std::shared_ptr<model> {
    const auto size = source.size();
    auto result     = registry.create_unnamed(size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < size.x; ++at.x) {
            for (at.y = 0; at.y < size.y; ++at.y) {
                for (at.z = 0; at.z < size.z; ++at.z) {
                    const auto cell = source.get_matter(at);
                    if (!cell.is_empty()) {
                        writer.set(at, cell);
                    }
                }
            }
        }

        for (const voxel_edit& edit : edits) {
            if (contains(size, edit.position)) {
                writer.set(edit.position, edit.value);
            }
        }
    }

    result->set_pivot(source.pivot());

    return result;
}

auto resized(
    const model& source, vec3i grown_at_min, vec3i grown_at_max, model_registry& registry
) -> std::shared_ptr<model> {
    const auto old_size = source.size();
    const vec3i size{
        old_size.x + grown_at_min.x + grown_at_max.x,
        old_size.y + grown_at_min.y + grown_at_max.y,
        old_size.z + grown_at_min.z + grown_at_max.z,
    };
    if (size.x < 1 || size.y < 1 || size.z < 1) {
        return nullptr;
    }

    auto result = registry.create_unnamed(size);

    {
        model_writer writer{*result};

        vec3i at;
        for (at.x = 0; at.x < size.x; ++at.x) {
            for (at.y = 0; at.y < size.y; ++at.y) {
                for (at.z = 0; at.z < size.z; ++at.z) {
                    const vec3i from{
                        at.x - grown_at_min.x, at.y - grown_at_min.y, at.z - grown_at_min.z
                    };
                    if (!contains(old_size, from)) {
                        continue;
                    }

                    const auto cell = source.get_matter(from);
                    if (!cell.is_empty()) {
                        writer.set(at, cell);
                    }
                }
            }
        }
    }

    result->set_pivot(source.pivot() + to_float(grown_at_min));

    return result;
}

}  // namespace vw::asset
