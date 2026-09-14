export module vw.core:transform;

import :vector;
import :matrix;

export namespace vw {

struct transform {
private:
    vec3f position_{0.0F, 0.0F, 0.0F};
    quat rotation_;
    vec3f scale_{1.0F, 1.0F, 1.0F};

public:
    [[nodiscard]] auto get_position() const -> const vec3f&;
    [[nodiscard]] auto get_rotation() const -> const quat&;
    [[nodiscard]] auto get_rotation_euler() const -> vec3f;
    [[nodiscard]] auto get_scale() const -> const vec3f&;

    [[nodiscard]] auto calc_matrix() const -> mat4f;

    auto set_position(const vec3f& position) -> void;
    auto set_rotation(const quat& rotation) -> void;
    auto set_rotation_euler(const vec3f& euler) -> void;
    auto set_scale(const vec3f& scale) -> void;

    auto translate(const vec3f& offset) -> void;
    auto rotate(const vec3f& angles) -> void;
    auto scale(const vec3f& factor) -> void;
};

}  // namespace vw
