export module vw.gfx:debug.primitive;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import :camera;
import :gpu_buffers;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
}

export namespace vw::gfx {

class vulkan_context;

struct debug_vertex {
    vec3f pos;
    color col;

    explicit debug_vertex(
        vec3f pos, color col = colors::red_4
    )
        : pos(pos), col(col) {}

    [[nodiscard]]
    static auto get_binding_descriptions() -> std::vector<vk::VertexInputBindingDescription>;

    [[nodiscard]]
    static auto get_attribute_descriptions() -> std::vector<vk::VertexInputAttributeDescription>;
};

class debug_primitives {
public:
    auto add_line(const vec3f& begin, const vec3f& end, color clr = colors::red_4) -> void;

    auto add_box(const mat4f& matrix, const vec3f& size, color clr = colors::red_4) -> void;
    auto add_box(const transform& transform, const vec3f& size, color clr = colors::red_4) -> void;
    auto add_box(const vec3f& pos, const vec3f& size, color clr = colors::red_4) -> void;

    auto add_grid(
        const mat4f& matrix, float cell_size, int cols, int rows, color clr = colors::red_4
    ) -> void;
    auto add_grid(
        const transform& transform, float cell_size, int cols, int rows, color clr = colors::red_4
    ) -> void;
    auto add_grid(const vec3f& pos, float cell_size, int cols, int rows, color clr = colors::red_4) -> void;

    [[nodiscard]]
    auto get_vertices() const -> const std::vector<debug_vertex>&;

    [[nodiscard]]
    auto is_empty() const -> bool;

    auto clear() -> void;

private:
    std::vector<debug_vertex> vertices_;
};

}  // namespace vw::gfx
