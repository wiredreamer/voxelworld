module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::testbed {

namespace {
constexpr float32 lift            = 2600.0f;
constexpr float32 gap_of_spans    = 1.25f;
constexpr int32 columns_per_chunk = 1;
}  // namespace

lod_probe_scene::lod_probe_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}, coarsest_{std::clamp(args.integer("--coarsest", 4), 1, 8)} {
    span_ = std::max(coarsest_, 1);
}

auto lod_probe_scene::build_step_(
    int32 voxels_per_cell, float32 offset_x
) -> step {
    const auto started = std::chrono::steady_clock::now();

    auto& world         = stand().world();
    auto& transform_sys = world.system<ecs::transform_system>();
    auto& model_sys     = world.system<ecs::model_system>();

    const int32 units_per_cell = stand().world_units_per_voxel() * voxels_per_cell;
    const int32 columns        = std::max(span_ / voxels_per_cell, columns_per_chunk);

    std::unordered_map<vec3i, std::shared_ptr<asset::chunk_volume>> volumes;

    for (int32 cx = 0; cx < columns; ++cx) {
        for (int32 cz = 0; cz < columns; ++cz) {
            ecs::gen_column column{cx, cz};

            ecs::terrain_context ctx{
                .cx              = cx,
                .cz              = cz,
                .voxels_per_cell = voxels_per_cell,
                .create_chunk    = [&column](int32 y) -> ecs::chunk_data& {
                    return column.create_chunk(y, ecs::chunk_data{});
                },
            };

            stand().terrain().generate(ctx);

            for (auto& [cy, data] : column.get_all_chunk_data()) {
                volumes.emplace(vec3i{cx, cy, cz}, data.volume);
            }
        }
    }

    for (auto& [coord, volume] : volumes) {
        for (const auto face : all_face_directions) {
            const auto neighbor = volumes.find(coord + offset_of(face));
            if (neighbor != volumes.end()) {
                volume->set_boundary_slice(face, neighbor->second->voxels());
            }
        }
    }

    step made{.voxels_per_cell = voxels_per_cell, .columns = columns * columns};

    for (auto& [coord, volume] : volumes) {
        const auto ent = world.create()
                             .with<ecs::transform_component>()
                             .with<ecs::spatial_component>()
                             .with<ecs::model_component>()
                             .with<ecs::lod_component>()
                             .get_entity();

        model_sys.modify(ent).set_chunk(volume);

        const auto side  = static_cast<float32>(cells_per_chunk * units_per_cell);
        const auto scale = static_cast<float32>(units_per_cell);

        transform_sys.modify(ent)
            .set_position(
                {(static_cast<float32>(coord.x) * side) + offset_x,
                 (static_cast<float32>(coord.y) * side) + lift,
                 static_cast<float32>(coord.z) * side}
            )
            .set_scale({scale, scale, scale});

        ++made.chunks;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started
    );
    made.build_ms = static_cast<float32>(elapsed.count()) / 1000.0f;

    return made;
}

auto lod_probe_scene::place_() -> void {
    if (placed_) {
        return;
    }

    const auto reach =
        static_cast<float32>(span_ * cells_per_chunk * stand().world_units_per_voxel());

    float32 offset_x = 0.0f;
    for (int32 voxels_per_cell = 1; voxels_per_cell <= coarsest_; voxels_per_cell *= 2) {
        steps_.push_back(build_step_(voxels_per_cell, offset_x));
        offset_x += reach * gap_of_spans;
    }

    placed_ = true;

    for (const auto& one : steps_) {
        log::info(
            "lod-probe: step {} built {} columns, {} chunks in {:.1f} ms", one.voxels_per_cell,
            one.columns, one.chunks, one.build_ms
        );
    }
}

auto lod_probe_scene::tick(float32) -> void {
    place_();
}

auto lod_probe_scene::ui() -> void {
    if (!ImGui::Begin("lod probe")) {
        ImGui::End();
        return;
    }

    ImGui::Text("same %d columns of world, coarser each time", span_);

    for (const auto& one : steps_) {
        ImGui::Text(
            "step %d: %d columns, %d chunks, %.1f ms", one.voxels_per_cell, one.columns,
            one.chunks, one.build_ms
        );
    }

    ImGui::End();
}

auto lod_probe_scene::collect_report(
    gfx::report& out
) const -> void {
    auto section = out.section("lod_probe");
    section.value("span_columns", static_cast<uint64>(span_));

    for (const auto& one : steps_) {
        const auto tag = std::format("step_{}", one.voxels_per_cell);
        section.value(tag + "_chunks", static_cast<uint64>(one.chunks));
        section.value(tag + "_build_ms", static_cast<float64>(one.build_ms), 1);
    }
}

}  // namespace vw::testbed
