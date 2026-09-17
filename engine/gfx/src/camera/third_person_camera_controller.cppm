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
    float32 pitch_min      = -89.0f;
    float32 pitch_max      = 89.0f;
    float32 zoom_speed     = 2.0f;
    float32 collision_skin = 0.3f;
};

class third_person_camera_controller {
public:
    using world_type = world;
    explicit third_person_camera_controller(
        camera& camera,
        world_type& world,
        third_person_camera_params params = {}
    );

    auto update(const player_input_state& input, entity target) -> void;

    [[nodiscard]] auto get_params() -> third_person_camera_params&;
    [[nodiscard]] auto get_pitch() const -> float32;
    [[nodiscard]] auto get_yaw() const -> float32;
    [[nodiscard]] auto get_actual_arm_length() const -> float32;

private:
    camera* camera_;
    world_type* world_;
    third_person_camera_params params_;

    float32 pitch_ = 20.0f;
    float32 yaw_   = 0.0f;
    float32 actual_arm_length_ = 0.0f;
};

}  // namespace vw::gfx
