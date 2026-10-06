export module vw.testbed:scenes.terrain_lab;

import std;

import vw.core;
import vw.world;
import :app;
import :args;
import :scene;

export namespace vw::testbed {

class terrain_lab_scene final : public scene {
public:
    terrain_lab_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "terrain-lab";
    }

    auto tick(float32 delta_time) -> void override;
    auto ui() -> void override;

private:
    auto reload_() -> void;
    auto save_() -> void;
    auto world_ui_() -> bool;
    auto biome_ui_(ecs::terrain_biome& biome) -> bool;
    auto voxel_combo_(const char* label, voxel& value) -> bool;
    auto ramp_ui_(const char* label, ecs::tone_ramp& ramp) -> bool;
    auto focus_ui_() -> bool;
    auto rebuild_() -> void;
    [[nodiscard]] auto shown_() const -> ecs::perlin_terrain_generator::params;

    std::filesystem::path file_;
    ecs::perlin_terrain_generator::params params_;
    std::string status_;
    float32 since_edit_ = -1.0F;
    float32 rebuild_after_seconds_ = 0.4F;
    bool live_ = true;
    int32 biome_ = -1;
};

}  // namespace vw::testbed
