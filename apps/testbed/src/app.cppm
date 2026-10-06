export module vw.testbed:app;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

import :args;
import :camera;
import :probes.clusters;
import :scene;

export namespace vw::testbed {

inline constexpr float32 round_reach = 0.8f;

enum class edit_tool : int32 {
    none = 0,
    place,
    remove,
};

struct voxel_choice {
    const char* name;
    voxel id;
};

constexpr std::array<voxel_choice, 8> voxel_menu{{
    {"glowstone (emits 14)", voxels::lamp_amber},
    {"lava (emits 15)", voxels::fire_red},
    {"stone", voxels::gray[10]},
    {"dark stone", voxels::gray[4]},
    {"grass", voxels::green[10]},
    {"dirt", voxels::brown[4]},
    {"sand", voxels::brown[8]},
    {"white", voxels::white},
}};

struct voxel_pick {
    vec3i solid_voxel_pos;
    vec3i empty_voxel_pos;
};

class testbed_app final : public gfx::app {
public:
    testbed_app(
        gfx::engine& eng, const arg_reader& args, const scene_factory& make_scene,
        const camera_factory& make_camera
    );
    ~testbed_app() override;

    testbed_app(const testbed_app&)                    = delete;
    auto operator=(const testbed_app&) -> testbed_app& = delete;
    testbed_app(testbed_app&&)                         = delete;
    auto operator=(testbed_app&&) -> testbed_app&      = delete;

    [[nodiscard]] auto is_bench_ready() const -> bool override;
    auto render(float32 delta_time) -> void override;

    auto collect_report(gfx::report& out) const -> void override;

    [[nodiscard]] auto engine() const -> gfx::engine& {
        return get_engine();
    }

    [[nodiscard]] auto world() const -> ecs::world&;
    [[nodiscard]] auto renderer() const -> gfx::renderer&;
    [[nodiscard]] auto camera() const -> gfx::camera&;

    [[nodiscard]] auto camera_controller() const -> gfx::free_camera_controller& {
        return *camera_controller_;
    }

    [[nodiscard]] auto grid() const -> ecs::world_grid& {
        return *world_grid_;
    }

    [[nodiscard]] auto terrain() const -> ecs::perlin_terrain_generator& {
        return *generator_;
    }

    [[nodiscard]] auto world_units_per_voxel() const -> int32 {
        return generator_params_.world_units_per_voxel;
    }

    [[nodiscard]] auto altitude() const -> float32 {
        return bench_altitude_;
    }

    [[nodiscard]] auto camera_placed() const -> bool {
        return camera_placed_;
    }

    [[nodiscard]] auto streaming_settled() const -> bool;

    [[nodiscard]] auto benching() const -> bool {
        return benching_;
    }

    auto drop_emitter(voxel id, int32 radius) -> void;

private:
    auto setup_world_grid() -> void;
    auto try_place_camera() -> void;
    [[nodiscard]] auto scene_camera_() -> std::unique_ptr<camera_rig>;

    auto tick_day_night_(float32 delta_time) -> void;
    auto apply_time_of_day_() -> void;
    auto step_time_of_day_(float32 delta) -> void;

    auto set_torch_(bool on) -> void;
    auto tick_torch_(const vec3f& at) -> void;

    [[nodiscard]] auto pick_voxel_() const -> std::optional<voxel_pick>;
    auto update_hovered_() -> void;
    auto draw_hover_() -> void;
    auto apply_tool_() -> void;

    auto render_ui() -> void;
    auto handle_key_press(plat::keyboard::keys key) -> void;



    std::unique_ptr<gfx::free_camera_controller> camera_controller_;
    ecs::world_grid* world_grid_ = nullptr;
    ecs::entity viewer_          = ecs::invalid_entity;
    ecs::perlin_terrain_generator* generator_ = nullptr;
    ecs::perlin_terrain_generator::params generator_params_;
    bool camera_placed_ = false;

    float32 time_of_day_        = 0.5f;
    float32 day_length_seconds_ = 120.0f;
    float32 night_intensity_    = 0.06f;
    bool day_night_running_     = true;
    bool sun_in_bench_          = false;

    std::string drop_status_;
    ecs::entity torch_ = ecs::invalid_entity;

    edit_tool tool_     = edit_tool::none;
    int32 place_choice_ = 0;
    int32 reach_voxels_ = 12;
    int32 edit_clicks_  = 0;
    std::optional<voxel_pick> hovered_;

    uint32 view_distance_ = ecs::default_view_distance;
    float32 lod_distance_ = static_cast<float32>(ecs::default_lod_base_chunks);
    int32 lod_level_       = -1;

    float32 bench_altitude_   = 0.0f;
    mutable bool bench_ready_ = false;
    bool world_ready_         = false;
    bool benching_            = false;

    cluster_probe clusters_;

    std::unique_ptr<scene> scene_;

    std::unique_ptr<camera_rig> rig_;
};

}  // namespace vw::testbed
