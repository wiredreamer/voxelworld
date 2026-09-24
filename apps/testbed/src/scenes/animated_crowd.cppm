export module vw.testbed:scenes.animated_crowd;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import :app;
import :args;
import :camera;
import :scene;

export namespace vw::testbed {

class animated_crowd_scene final : public scene {
public:
    animated_crowd_scene(testbed_app& stand, const arg_reader& args);

    [[nodiscard]] auto name() const -> std::string_view override {
        return "animated-crowd";
    }

    auto tick(float32 delta_time) -> void override;

    [[nodiscard]] auto default_camera() const -> camera_hint override {
        return {.offset = {0.0f, 60.0f, 260.0f}, .pitch = -15.0f, .yaw = 180.0f};
    }
    auto on_world_ready() -> void override;

    [[nodiscard]] auto is_ready() const -> bool override;

    auto collect_report(gfx::report& out) const -> void override;
    auto ui() -> void override;

private:
    static constexpr uint32 settle_target = 400;

    static constexpr vec3f collider_extents{12.0f, 24.0f, 12.0f};
    static constexpr float32 collider_half_width = 6.0f;

    static constexpr float32 drop_height = 16.0f;

    static constexpr float32 wave_seconds = 1.0f;
    static constexpr float32 spacing      = 40.0f;

    static constexpr float32 bone_spacing = 2.0f;
    static constexpr float32 bone_swing   = 3.0f;

    static constexpr float32 blend_seconds = 0.35f;

    struct body_part {
        std::string_view target;
        std::string_view model;
        vec3i size;
        vec3f rest;
        voxel fill;
        float32 lift;
        float32 peak;
    };

    static constexpr std::array<body_part, 4> parts{{
        {.target = "body", .model = "crowd_body", .size = {6, 12, 4},
         .rest = {-3.0f, 0.0f, -2.0f}, .fill = voxels::blue[2], .lift = 1.0f, .peak = 0.30f},
        {.target = "head", .model = "crowd_head", .size = {6, 6, 6},
         .rest = {-3.0f, 13.0f, -3.0f}, .fill = voxels::brown[2], .lift = 2.0f, .peak = 0.45f},
        {.target = "hand_left", .model = "crowd_hand", .size = {3, 8, 3},
         .rest = {-7.0f, 2.0f, -1.5f}, .fill = voxels::green[4], .lift = 6.0f, .peak = 0.15f},
        {.target = "hand_right", .model = "crowd_hand", .size = {3, 8, 3},
         .rest = {4.0f, 2.0f, -1.5f}, .fill = voxels::green[4], .lift = 6.0f, .peak = 0.60f},
    }};

    struct body {
        ecs::entity ent;
        vec2f home;
    };

    [[nodiscard]] auto make_clip_(ecs::world& world, std::string_view name, float32 swing) const
        -> std::shared_ptr<asset::animation_clip>;

    auto spawn_bone_chain_(ecs::entity root) -> void;

    [[nodiscard]] auto ground_at_(float32 x, float32 z) const -> float32;
    [[nodiscard]] auto grounded_() const -> std::size_t;
    [[nodiscard]] auto drift_() const -> float32;

    auto spawn_() -> void;

    uint32 size_  = 50;
    uint32 bones_ = 0;
    uint32 blend_ = 0;

    std::array<std::shared_ptr<asset::animation_clip>, 2> clips_{};
    uint32 blend_frames_ = 0;
    uint32 blend_slot_   = 0;

    std::vector<body> bodies_;
    uint32 settle_frames_ = 0;
    bool spawned_         = false;
};

}  // namespace vw::testbed
