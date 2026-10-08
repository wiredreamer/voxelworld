module;

#include <imgui.h>

module vw.testbed;

import std;
import vw.core;
import vw.world;
import vw.gfx;

namespace vw::testbed {

namespace {

constexpr std::string_view asset_root = VW_TESTBED_ASSET_ROOT;

auto settings_file(const arg_reader& args) -> std::filesystem::path {
    if (const auto given = args.text("--settings")) {
        return std::filesystem::path{*given};
    }
    return std::filesystem::path{asset_root} / "data" / "world_gen.json";
}

auto lab_base() -> ecs::perlin_terrain_generator::params {
    return ecs::perlin_terrain_generator::params{};
}

}  // namespace

terrain_lab_scene::terrain_lab_scene(
    testbed_app& stand, const arg_reader& args
)
    : scene{stand}
    , file_{settings_file(args)} {
    reload_();
}

auto terrain_lab_scene::reload_() -> void {
    auto loaded = ecs::load_terrain_settings(file_, default_voxel_registry(), lab_base());
    if (!loaded) {
        status_ = loaded.error();
        params_ = lab_base();
    } else {
        status_ = std::format("loaded {}", file_.generic_string());
        params_ = std::move(*loaded);
    }
    rebuild_();
}

auto terrain_lab_scene::save_() -> void {
    const auto saved = ecs::save_terrain_settings(file_, params_, default_voxel_registry());
    status_ = saved ? std::format("saved {}", file_.generic_string()) : saved.error();
}

auto terrain_lab_scene::tick(
    float32 delta_time
) -> void {
    if (since_edit_ < 0.0F) {
        return;
    }
    since_edit_ += delta_time;
    if (live_ && since_edit_ >= rebuild_after_seconds_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        rebuild_();
    }
}

auto terrain_lab_scene::voxel_combo_(
    const char* label, voxel& value
) -> bool {
    const auto& voxels = default_voxel_registry();
    bool changed       = false;
    if (ImGui::BeginCombo(label, std::string{voxels.get(value).name}.c_str())) {
        for (const auto& type : voxels.all()) {
            if (type.id == voxels::air) {
                continue;
            }
            const std::string name{type.name};
            if (ImGui::Selectable(name.c_str(), type.id == value)) {
                value   = type.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

auto terrain_lab_scene::world_ui_() -> bool {
    bool changed = false;

    int32 seed = static_cast<int32>(params_.seed);
    if (ImGui::InputInt("seed", &seed)) {
        params_.seed = static_cast<uint32>(seed);
        changed      = true;
    }

    for (const auto& flag : ecs::terrain_flag_fields()) {
        const std::string label{flag.key};
        changed |= ImGui::Checkbox(label.c_str(), &(params_.*(flag.member)));
    }

    for (const auto& field : ecs::terrain_number_fields()) {
        const std::string label{field.key};
        std::visit(
            [&](auto member) {
                auto& value = params_.*member;
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, int32>) {
                    changed |= ImGui::SliderInt(
                        label.c_str(), &value, static_cast<int32>(field.min),
                        static_cast<int32>(field.max)
                    );
                } else {
                    changed |= ImGui::SliderFloat(label.c_str(), &value, field.min, field.max, "%.4g");
                }
            },
            field.member
        );
    }
    return changed;
}

auto terrain_lab_scene::biome_ui_(
    ecs::terrain_biome& biome
) -> bool {
    bool changed = false;

    ecs::visit_biome_fields(biome, [&](std::string_view key, auto& value, auto... range) {
        const std::string label{key};
        using field_type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<field_type, float32>) {
            const auto [min, max] = std::array{range...};
            changed |= ImGui::SliderFloat(label.c_str(), &value, min, max, "%.4g");
        } else if constexpr (std::is_same_v<field_type, int32>) {
            const auto [min, max] = std::array{range...};
            changed |= ImGui::SliderInt(
                label.c_str(), &value, static_cast<int32>(min), static_cast<int32>(max)
            );
        } else if constexpr (std::is_same_v<field_type, voxel>) {
            changed |= voxel_combo_(label.c_str(), value);
        } else {
            changed |= ramp_ui_(label.c_str(), value);
        }
    });

    if (ImGui::Button("back to the code values")) {
        biome = std::visit(
            [](const auto& tuned) -> ecs::terrain_biome { return std::remove_cvref_t<decltype(tuned)>{}; },
            biome
        );
        changed = true;
    }
    return changed;
}

auto terrain_lab_scene::ramp_ui_(
    const char* label, ecs::tone_ramp& ramp
) -> bool {
    bool changed = false;
    ImGui::PushID(label);
    if (ImGui::TreeNode(label)) {
        const auto row = std::ranges::find(voxels::groups, ramp.row.first, &voxel_group::first);
        const std::string current{row == voxels::groups.end() ? "?" : row->name};
        if (ImGui::BeginCombo("row", current.c_str())) {
            for (const auto& group : voxels::groups) {
                const std::string name{group.name};
                if (ImGui::Selectable(name.c_str(), group.first == ramp.row.first)) {
                    ramp.row = voxel_span{group.first.value, group.count};
                    changed  = true;
                }
            }
            ImGui::EndCombo();
        }

        const auto last = static_cast<float32>(ramp.row.count - 1);
        changed |= ImGui::SliderFloat("from", &ramp.from, 0.0F, last, "%.1f");
        changed |= ImGui::SliderFloat("to", &ramp.to, 0.0F, last, "%.1f");
        changed |= ImGui::SliderFloat("spot_frequency", &ramp.spot_frequency, 0.0005F, 0.05F, "%.4g");
        changed |= ImGui::SliderFloat("spot_contrast", &ramp.spot_contrast, 0.5F, 6.0F, "%.2f");
        ImGui::TreePop();
    }
    ImGui::PopID();
    return changed;
}

auto terrain_lab_scene::focus_ui_() -> bool {
    bool changed = false;

    std::vector<std::string> names{"every biome"};
    for (const auto& biome : params_.biomes) {
        names.push_back(std::format("only {}", ecs::biome_name(biome)));
    }
    const int32 shown = std::clamp(biome_ + 1, 0, static_cast<int32>(names.size()) - 1);
    if (ImGui::BeginCombo("show", names[static_cast<std::size_t>(shown)].c_str())) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (ImGui::Selectable(names[index].c_str(), static_cast<int32>(index) == shown)) {
                biome_  = static_cast<int32>(index) - 1;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

auto terrain_lab_scene::shown_() const -> ecs::perlin_terrain_generator::params {
    auto shown = params_;
    if (biome_ >= 0 && biome_ < static_cast<int32>(shown.biomes.size())) {
        shown.biomes = {shown.biomes[static_cast<std::size_t>(biome_)]};
    }
    return shown;
}

auto terrain_lab_scene::rebuild_() -> void {
    stand().rebuild_terrain(shown_());
    since_edit_ = -1.0F;
}

auto terrain_lab_scene::ui() -> void {
    ImGui::TextUnformatted(status_.c_str());

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0F, viewport->WorkPos.y + 10.0F),
        ImGuiCond_FirstUseEver, ImVec2(1.0F, 0.0F)
    );
    ImGui::SetNextWindowSize(ImVec2(440.0F, viewport->WorkSize.y * 0.8F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain generation")) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("rebuild while editing", &live_);
    ImGui::SameLine();
    if (ImGui::Button("rebuild")) {
        rebuild_();
    }
    ImGui::SameLine();
    if (ImGui::Button("save")) {
        save_();
    }
    ImGui::SameLine();
    if (ImGui::Button("reload")) {
        reload_();
    }

    bool changed = focus_ui_();

    if (ImGui::CollapsingHeader("World")) {
        changed |= world_ui_();
    }

    for (std::size_t index = 0; index < params_.biomes.size(); ++index) {
        auto& biome = params_.biomes[index];
        const std::string header = std::format("Biome {}###biome{}", ecs::biome_name(biome), index);
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::PushID(static_cast<int>(index));
            changed |= biome_ui_(biome);
            ImGui::PopID();
        }
    }

    if (changed) {
        since_edit_ = 0.0F;
    }

    ImGui::End();
}

}  // namespace vw::testbed
