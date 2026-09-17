export module vw.gfx:camera;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {

struct player_input_state {
    vec2f move_input{0.0f, 0.0f};
    vec2f look_delta{0.0f, 0.0f};
    float32 zoom_delta    = 0.0f;
    bool jump_requested   = false;
    bool attack_requested = false;
    bool sprint           = false;
};

}  // namespace vw::gfx

export namespace vw::gfx {


class camera {
public:
    explicit camera(
        float fov = 60.0f, float aspect = 16.0f / 9.0f, float near = 0.1f, float far = 50000.0f
    );

    auto set_position(const vec3f& position) -> void;
    auto set_rotation(float pitch, float yaw) -> void;
    auto set_aspect_ratio(float aspect) -> void;

    auto set_far(float32 far) -> void;

    [[nodiscard]] auto get_near() const -> float;
    [[nodiscard]] auto get_far() const -> float;
    [[nodiscard]] auto get_fov() const -> float;
    [[nodiscard]] auto get_aspect_ratio() const -> float;

    [[nodiscard]] auto get_position() const -> vec3f;
    [[nodiscard]] auto get_pitch() const -> float;
    [[nodiscard]] auto get_yaw() const -> float;

    [[nodiscard]] auto get_view_matrix() const -> mat4f;
    [[nodiscard]] auto get_projection_matrix() const -> mat4f;
    [[nodiscard]] auto get_view_projection_matrix() const -> mat4f;

    [[nodiscard]] auto get_frustum() const -> const vw::spatial::frustum&;

    auto move_forward(float distance) -> void;
    auto move_right(float distance) -> void;
    auto move_up(float distance) -> void;
    auto rotate(float delta_pitch, float delta_yaw) -> void;

    auto get_forward() const -> vec3f;
    auto get_right() const -> vec3f;
    auto get_up() const -> vec3f;

    [[nodiscard]] auto screen_to_world_ray(
        const vec2d& mouse_pos,
        const vec2i& window_size
    ) const -> vw::spatial::ray;

private:
    auto update_vectors() const -> void;
    auto update_view_matrix() const -> void;
    auto update_projection_matrix() const -> void;
    auto update_frustum() const -> void;

    vec3f position_;
    float pitch_, yaw_;
    float fov_, aspect_, near_, far_;

    mutable vec3f forward_, right_, up_;
    mutable bool vectors_dirty_;

    mutable mat4f view_matrix_;
    mutable mat4f projection_matrix_;
    mutable bool view_matrix_dirty_;
    mutable bool projection_matrix_dirty_;
    
    mutable vw::spatial::frustum frustum_;
    mutable bool frustum_dirty_;
};
}  // namespace vw::gfx
