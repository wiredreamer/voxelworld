module;

#include <imgui.h>

module vw.gfx;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {

namespace {

constexpr float32 item_width = 170.0f;

constexpr uint32 default_visible_cap = 256;

auto reset_button(const char* id) -> bool {
    ImGui::Spacing();
    return ImGui::Button(id);
}

auto drag_uint(const char* label, uint32& value, uint32 low, uint32 high) -> void {
    auto shown = static_cast<int32>(value);
    if (ImGui::SliderInt(label, &shown, static_cast<int32>(low), static_cast<int32>(high))) {
        value = static_cast<uint32>(shown);
    }
}

}  // namespace

auto debug_window::render_view_panel() -> void {
    auto& renderer = engine_->get_renderer();

    ImGui::TextUnformatted("mode");
    for (std::size_t i = 0; i < render_mode_names.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const bool active = static_cast<std::size_t>(renderer.get_render_mode()) == i;
        if (ImGui::RadioButton(render_mode_names[i].data(), active)) {
            renderer.set_render_mode(static_cast<render_mode>(i));
        }
    }

    ImGui::PushItemWidth(item_width);

    const auto current = static_cast<std::size_t>(renderer.get_debug_view());
    if (ImGui::BeginCombo("debug view", debug_view_names[current].data())) {
        for (std::size_t i = 0; i < debug_view_names.size(); ++i) {
            const bool selected = i == current;
            if (ImGui::Selectable(debug_view_names[i].data(), selected)) {
                renderer.set_debug_view(static_cast<debug_view>(i));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();

    ImGui::Checkbox("colliders", &show_colliders_);

    // см. docs/rendering.md#отсев-по-заслонам
    static constexpr std::array<const char*, 3> occlusion_names{"off", "on", "check"};
    auto occlusion = static_cast<int32>(renderer.get_occlusion_settings().mode);
    if (ImGui::Combo(
            "occlusion", &occlusion, occlusion_names.data(),
            static_cast<int32>(occlusion_names.size())
        )) {
        renderer.get_occlusion_settings().mode = static_cast<occlusion_mode>(occlusion);
    }

    bool chunk_cull = renderer.is_chunk_cull_enabled();
    if (ImGui::Checkbox("chunk cull", &chunk_cull)) {
        renderer.set_chunk_cull_enabled(chunk_cull);
    }
}

auto debug_window::render_lighting_panel() -> void {
    auto& renderer = engine_->get_renderer();
    ImGui::PushItemWidth(item_width);

    if (ImGui::TreeNode("Sun")) {
        auto& sun = renderer.get_directional_light_settings();

        ImGui::TextDisabled("the app may drive these every frame");

        if (ImGui::DragFloat3("direction", &sun.direction.x, 0.01f, -1.0f, 1.0f, "%.2f")) {
            if (math::length(sun.direction) > 0.0f) {
                sun.direction = math::normalize(sun.direction);
            }
        }
        ImGui::ColorEdit3("colour", &sun.color.x);
        ImGui::SliderFloat("intensity", &sun.intensity, 0.0f, 4.0f, "%.2f");

        ImGui::SliderFloat("wrap", &sun.wrap, 0.0f, 1.0f, "%.2f");

        if (reset_button("reset##sun")) {
            sun = directional_light_settings{};
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Ambient")) {
        auto& ambient = renderer.get_ambient_settings();

        ImGui::SliderFloat("strength", &ambient.strength, 0.0f, 2.0f, "%.2f");
        ImGui::ColorEdit3("sky", &ambient.sky.x);
        ImGui::ColorEdit3("ground", &ambient.ground.x);
        ImGui::ColorEdit3("cave", &ambient.cave.x);

        ImGui::SliderFloat("ao strength", &ambient.ao_strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("ao curve", &ambient.ao_curve, 0.25f, 4.0f, "%.2f");

        ImGui::SliderFloat("convex strength", &ambient.convex_strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("convex curve", &ambient.convex_curve, 0.25f, 4.0f, "%.2f");

        ImGui::SliderFloat("sky curve", &ambient.sky_curve, 0.25f, 4.0f, "%.2f");

        ImGui::SliderFloat("sun curve", &ambient.sun_curve, 0.25f, 8.0f, "%.2f");

        if (reset_button("reset##ambient")) {
            ambient = ambient_settings{};
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Block light")) {
        auto& lamp = renderer.get_block_light_settings();

        ImGui::ColorEdit3("colour", &lamp.color.x);
        ImGui::SliderFloat("intensity", &lamp.intensity, 0.0f, 4.0f, "%.2f");

        ImGui::SliderFloat("curve", &lamp.curve, 0.25f, 4.0f, "%.2f");

        ImGui::SliderFloat("glow", &lamp.glow, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("lamp colour saturation", &lamp.tint_saturation, 0.0f, 1.0f, "%.2f");

        if (reset_button("reset##lamp")) {
            lamp = block_light_settings{};
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Blob shadows")) {
        ImGui::SliderFloat("strength", &renderer.get_blob_strength(), 0.0f, 1.0f, "%.2f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Tonemap")) {
        auto& tonemap = renderer.get_tonemap_settings();

        ImGui::SliderFloat("exposure", &tonemap.exposure, 0.1f, 4.0f, "%.2f");
        ImGui::SliderFloat("white point", &tonemap.white_point, 0.25f, 4.0f, "%.2f");

        if (reset_button("reset##tonemap")) {
            tonemap = tonemap_settings{};
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Light cache")) {
        auto& cache = renderer.get_light_cache_settings();

        int bricks = static_cast<int>(cache.bricks_per_frame);

        if (ImGui::SliderInt("bricks per frame", &bricks, 1, 2048)) {
            cache.bricks_per_frame = static_cast<uint32>(bricks);
        }

        if (reset_button("reset##light_cache")) {
            cache = light_cache_settings{};
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bloom")) {
        auto& bloom = renderer.get_bloom_settings();

        ImGui::Checkbox("enabled##bloom", &bloom.enabled);
        ImGui::SliderFloat("intensity##bloom", &bloom.intensity, 0.0f, 3.0f, "%.2f");

        if (reset_button("reset##bloom")) {
            bloom = bloom_settings{};
        }
        ImGui::TreePop();
    }

    ImGui::PopItemWidth();
}

auto debug_window::render_shadows_panel() -> void {
    auto& shadows = engine_->get_renderer().get_shadow_settings();
    ImGui::PushItemWidth(item_width);

    ImGui::Checkbox("enabled", &shadows.enabled);

    ImGui::TextDisabled("twin switch: SHADOW_ENABLED in voxel.frag");

    ImGui::SliderFloat("first split", &shadows.first_split, 4.0f, 256.0f, "%.0f");
    ImGui::SliderFloat("distance", &shadows.distance, 100.0f, 4000.0f, "%.0f");
    ImGui::SliderFloat("turn texels", &shadows.turn_texels, 0.0f, 2.0f, "%.3f");
    drag_uint("updates/frame", shadows.updates_per_frame, 0, shadow_map::cascade_count);
    ImGui::SliderFloat("filter texels", &shadows.filter_texels, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("normal bias", &shadows.normal_bias, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("slope bias", &shadows.slope_bias, 0.0f, 4.0f, "%.2f");

    ImGui::SeparatorText("cascades");
    const auto& splits = engine_->get_renderer().get_cascade_splits();
    const auto& texels = engine_->get_renderer().get_cascade_texel_sizes();
    for (uint32 i = 0; i < shadow_map::cascade_count; ++i) {
        ImGui::Text("%u: to %8.1f, texel %6.3f", i, splits[i], texels[i]);
    }

    if (reset_button("reset##shadows")) {
        shadows = shadow_settings{};
    }

    ImGui::PopItemWidth();
}

auto debug_window::render_lights_panel() -> void {
    auto& renderer = engine_->get_renderer();
    auto& clusters = renderer.get_cluster_settings();
    ImGui::PushItemWidth(item_width);

    ImGui::Checkbox("clustered", &clusters.enabled);

    drag_uint("tile size", clusters.tile_size, 8, 128);
    drag_uint("slices", clusters.slices, 1, 64);
    drag_uint("light cap", clusters.cap, 1, 256);
    drag_uint("blob cap", clusters.blob_cap, 1, 64);

    const auto grid = renderer.get_cluster_grid(engine_->get_camera());
    ImGui::Text(
        "%u x %u x %u, %u clusters", grid.tiles_x(), grid.tiles_y(), clusters.slices,
        grid.cluster_count()
    );

    ImGui::Text("visible lights %u", renderer.get_visible_light_count());

    auto& max_visible = renderer.get_max_visible_lights();
    bool capped       = max_visible != light_buffer::no_cap;
    if (ImGui::Checkbox("cap visible", &capped)) {
        max_visible = capped ? default_visible_cap : light_buffer::no_cap;
    }
    if (capped) {
        drag_uint("max visible", max_visible, 1, 4096);
    }

    ImGui::PopItemWidth();
}

auto debug_window::render_fog_panel() -> void {
    auto& fog = engine_->get_renderer().get_fog_settings();
    ImGui::PushItemWidth(item_width);

    ImGui::Checkbox("enabled", &fog.enabled);
    ImGui::ColorEdit3("colour", &fog.color.x);
    ImGui::SliderFloat("near", &fog.near_distance, 0.0f, 4096.0f, "%.0f");
    ImGui::SliderFloat("far", &fog.far_distance, 0.0f, 8192.0f, "%.0f");

    if (reset_button("reset##fog")) {
        fog = fog_settings{};
    }

    ImGui::PopItemWidth();
}

// см. docs/lod-plan.md#подобранные-умолчания
auto debug_window::render_lod_panel() -> void {
    auto& world = engine_->get_world();
    auto& lod   = world.system<ecs::lod_system>();

    const auto* grid   = world.system<ecs::world_grid_system>().grid();
    const auto in_chunk = static_cast<float32>(
        ecs::chunk::size * (grid != nullptr ? grid->world_units_per_voxel() : 1)
    );

    ImGui::PushItemWidth(item_width);

    float32 base = lod.get_default_base_distance() / in_chunk;
    if (ImGui::SliderFloat("base (chunks)", &base, 0.0f, 32.0f, "%.1f")) {
        lod.set_default_base_distance(base * in_chunk);
    }
    ImGui::TextUnformatted("the slider rebuilds the whole ladder as base x 2^L");

    ImGui::Spacing();

    const auto& ladder = lod.get_level_distances();
    for (uint32 level = 1; level < ladder.size(); ++level) {
        const auto label =
            std::format("step {} from (chunks)", asset::lod_step_of(static_cast<int32>(level)));

        float32 at = ladder[level] / in_chunk;
        if (ImGui::SliderFloat(label.c_str(), &at, 0.0f, 96.0f, "%.1f")) {
            lod.set_level_distance(level, at * in_chunk);
        }
    }
    ImGui::TextUnformatted("zero parks a step and every step above it");

    ImGui::Spacing();

    static constexpr std::array<const char*, asset::lod_level_count + 1> forced_names{
        "by distance", "1", "2", "4", "8"
    };

    int32 forced = lod.get_forced_level() + 1;
    if (ImGui::Combo(
            "force step", &forced, forced_names.data(), static_cast<int32>(forced_names.size())
        )) {
        lod.set_forced_level(forced - 1);
    }

    ImGui::PopItemWidth();
    ImGui::Separator();

    const auto& stats = lod.get_stats();

    ImGui::Text("levelled %u entities, pick %.3f ms", stats.entities, stats.pick_ms);
    ImGui::Text("last frame: %u raised, %u lowered", stats.raised, stats.lowered);

    ImGui::Spacing();

    for (uint32 level = 0; level < stats.at_level.size(); ++level) {
        const auto count = stats.at_level[level];
        const auto share = stats.entities > 0
            ? 100.0f * static_cast<float32>(count) / static_cast<float32>(stats.entities)
            : 0.0f;

        ImGui::Text(
            "step %d: %6u  %5.1f%%", asset::lod_step_of(static_cast<int32>(level)), count, share
        );
    }

    if (reset_button("reset##lod")) {
        lod.set_default_base_distance(
            ecs::lod_base_chunks_behind_fog(ecs::default_view_distance) * in_chunk
        );
        lod.set_forced_level(-1);
    }
}

}  // namespace vw::gfx
