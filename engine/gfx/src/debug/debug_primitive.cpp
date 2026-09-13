module;

#include <cstddef>

module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

auto debug_vertex::get_binding_descriptions()
    -> std::vector<vk::VertexInputBindingDescription> {
    std::vector<vk::VertexInputBindingDescription> bindings(1);

    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(debug_vertex);
    bindings[0].inputRate = vk::VertexInputRate::eVertex;

    return bindings;
}

auto debug_vertex::get_attribute_descriptions()
    -> std::vector<vk::VertexInputAttributeDescription> {
    std::vector<vk::VertexInputAttributeDescription> attributes(2);

    attributes[0].binding  = 0;
    attributes[0].location = 0;
    attributes[0].format   = vk::Format::eR32G32B32Sfloat;
    attributes[0].offset   = offsetof(debug_vertex, pos);

    attributes[1].binding  = 0;
    attributes[1].location = 1;
    attributes[1].format   = vk::Format::eR32Uint;
    attributes[1].offset   = offsetof(debug_vertex, col);

    return attributes;
}

auto debug_primitives::clear() -> void {
    vertices_.clear();
    solid_vertices_.clear();
}

auto debug_primitives::add_line(
    const vec3f& begin, const vec3f& end, color clr
) -> void {
    vertices_.emplace_back(begin, clr);
    vertices_.emplace_back(end, clr);
}
auto debug_primitives::add_box(
    const mat4f& matrix, const vec3f& size, color clr
) -> void {
    const vec3f p0 = matrix * vec3f{0.0f, 0.0f, 0.0f};
    const vec3f p1 = matrix * vec3f{size.x, 0.0f, 0.0f};
    const vec3f p2 = matrix * vec3f{size.x, 0.0f, size.z};
    const vec3f p3 = matrix * vec3f{0.0f, 0.0f, size.z};
    const vec3f p4 = matrix * vec3f{0.0f, size.y, 0.0f};
    const vec3f p5 = matrix * vec3f{size.x, size.y, 0.0f};
    const vec3f p6 = matrix * vec3f{size.x, size.y, size.z};
    const vec3f p7 = matrix * vec3f{0.0f, size.y, size.z};

    add_line(p0, p1, clr);
    add_line(p1, p2, clr);
    add_line(p2, p3, clr);
    add_line(p3, p0, clr);

    add_line(p4, p5, clr);
    add_line(p5, p6, clr);
    add_line(p6, p7, clr);
    add_line(p7, p4, clr);

    add_line(p0, p4, clr);
    add_line(p1, p5, clr);
    add_line(p2, p6, clr);
    add_line(p3, p7, clr);
}

auto debug_primitives::get_vertices() const -> const std::vector<debug_vertex>& {
    return vertices_;
}

auto debug_primitives::add_box(
    const transform& transform, const vec3f& size, color clr
) -> void {
    add_box(transform.calc_matrix(), size, clr);
}

auto debug_primitives::add_box(
    const vec3f& pos, const vec3f& size, color clr
) -> void {
    add_box(math::translation_matrix(pos), size, clr);
}

auto debug_primitives::add_grid(
    const mat4f& matrix, float cell_size, int cols, int rows, color clr
) -> void {
    const float cols_size = cell_size * static_cast<float>(cols);
    for (int i = 0; i <= cols; ++i) {
        const vec3f start = matrix * vec3f{static_cast<float>(i) * cell_size, 0.0f, 0.0f};
        const vec3f end   = matrix * vec3f{static_cast<float>(i) * cell_size, 0.0f, cols_size};
        add_line(start, end, clr);
    }

    const float rows_size = cell_size * static_cast<float>(rows);
    for (int j = 0; j <= rows; ++j) {
        const vec3f start = matrix * vec3f{0.0f, 0.0f, static_cast<float>(j) * cell_size};
        const vec3f end   = matrix * vec3f{rows_size, 0.0f, static_cast<float>(j) * cell_size};
        add_line(start, end, clr);
    }
}

auto debug_primitives::add_grid(
    const transform& transform, float cell_size, int cols, int rows, color clr
) -> void {
    add_grid(transform.calc_matrix(), cell_size, cols, rows, clr);
}

auto debug_primitives::add_grid(
    const vec3f& pos, float cell_size, int cols, int rows, color clr
) -> void {
    add_grid(math::translation_matrix(pos), cell_size, cols, rows, clr);
}

auto debug_primitives::is_empty() const -> bool {
    return vertices_.empty();
}

auto debug_primitives::add_triangle(
    const vec3f& a, const vec3f& b, const vec3f& c, color clr
) -> void {
    solid_vertices_.emplace_back(a, clr);
    solid_vertices_.emplace_back(b, clr);
    solid_vertices_.emplace_back(c, clr);
}

auto debug_primitives::add_quad(
    const vec3f& a, const vec3f& b, const vec3f& c, const vec3f& d, color clr
) -> void {
    add_triangle(a, b, c, clr);
    add_triangle(a, c, d, clr);
}

auto debug_primitives::get_solid_vertices() const -> const std::vector<debug_vertex>& {
    return solid_vertices_;
}

auto debug_primitives::is_solid_empty() const -> bool {
    return solid_vertices_.empty();
}

}  // namespace vw::gfx
