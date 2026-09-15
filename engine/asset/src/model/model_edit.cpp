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

    auto result = registry.create_unnamed(source.category(), new_size);

    // Один писатель на всё копирование: поэлементный set_voxel брал бы мьютекс
    // пула идентичностей на каждый воксель, а их тут весь объём модели.
    {
        model_writer writer{*result};

        for (int32 x = 0; x < new_size.x; ++x) {
            for (int32 y = 0; y < new_size.y; ++y) {
                for (int32 z = 0; z < new_size.z; ++z) {
                    const auto from =
                        vec3i{x + bounds->min.x, y + bounds->min.y, z + bounds->min.z};
                    writer.set(x, y, z, source.get_voxel(from));
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

}  // namespace vw::asset
