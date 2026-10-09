export module vw.gfx:camera.third_person_controller;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :camera;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {


struct third_person_camera_params {
    float32 arm_length     = 10.0f;
    float32 arm_length_min = 2.0f;
    float32 arm_length_max = 50.0f;
    vec3f target_offset    = {0.0f, 8.0f, 0.0f};
    float32 zoom_speed     = 2.0f;
    float32 collision_skin = 0.3f;

    // см. docs/ENGINE.md#камера-от-третьего-лица
    float32 pivot_rise            = 0.0f;
    float32 look_up_arm_share     = 0.25f;
    float32 look_up_full_degrees  = 30.0f;
    float32 look_down_arm_share   = 1.5f;
    float32 look_down_full_degrees = 80.0f;
    float32 probe_radius          = 0.5f;
    float32 arm_return_seconds    = 0.2f;
    float32 height_follow_seconds = 0.0f;
    float32 height_snap_distance  = 64.0f;

    // см. docs/ENGINE.md#камера-у-плеча
    float32 shoulder_arm_length     = 45.0f;
    float32 shoulder_offset         = 14.0f;
    float32 shoulder_rise           = -6.0f;
    float32 shoulder_follow_seconds = 0.12f;
};

class third_person_camera_controller {
public:
    using world_type = world;
    explicit third_person_camera_controller(
        camera& camera,
        world_type& world,
        third_person_camera_params params = {}
    );

    // см. docs/ENGINE.md#ввод
    auto update(
        entity target, float32 look_yaw_degrees, float32 look_pitch_degrees, float32 zoom_delta,
        float32 focus_lift = 0.0F, bool over_shoulder = false, float32 delta_time = 0.0F
    ) -> void;

    [[nodiscard]] auto get_params() -> third_person_camera_params&;
    [[nodiscard]] auto get_actual_arm_length() const -> float32;
    [[nodiscard]] auto get_shoulder_share() const -> float32;

private:
    [[nodiscard]] auto free_length_(const vec3f& from, const vec3f& to) -> float32;
    [[nodiscard]] auto belongs_to_target_(entity ent) const -> bool;

    camera* camera_;
    world_type* world_;
    third_person_camera_params params_;

    float32 actual_arm_length_ = 0.0f;
    float32 shoulder_share_    = 0.0f;
    float32 held_arm_share_    = 1.0f;
    float32 followed_height_   = 0.0f;
    bool height_known_         = false;
    entity target_;

    std::vector<entity> collision_candidates_;
};

}  // namespace vw::gfx
