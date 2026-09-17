export module vw.gfx:render.shadow_map;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :camera;
import :resource;
import :render.vulkan_context;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {

class vulkan_context;
class camera;

struct shadow_settings {
    bool enabled = false;

    float32 first_split = 32.0f;

    float32 distance = 1000.0f;

    float32 turn_texels = 0.05f;

    uint32 updates_per_frame = 1;

    float32 filter_texels = 0.5f;

    float32 normal_bias = 0.0f;
    float32 slope_bias  = 0.5f;
};

class shadow_map {
public:
    static constexpr uint32 cascade_count = 5;

    explicit shadow_map(vulkan_context& context, uint32 size = 2048);

    [[nodiscard]] auto get_size() const -> uint32 { return size_; }
    ~shadow_map();

    shadow_map(const shadow_map&)                    = delete;
    auto operator=(const shadow_map&) -> shadow_map& = delete;
    shadow_map(shadow_map&&)                         = delete;
    auto operator=(shadow_map&&) -> shadow_map&      = delete;

    auto update(const camera& camera, const vec3f& light_direction) -> void;

    [[nodiscard]] auto get_settings() -> shadow_settings& { return settings_; }
    [[nodiscard]] auto get_settings() const -> const shadow_settings& { return settings_; }

    auto invalidate(const vw::spatial::aabb& bounds) -> void;
    auto invalidate_all() -> void;
    [[nodiscard]] auto is_cascade_pending(uint32 cascade_index) const -> bool;
    [[nodiscard]] auto get_pending_count() const -> uint32;
    auto clear_pending() -> void;

    [[nodiscard]] auto get_light_space_matrix(uint32 cascade_index) const -> mat4f;
    [[nodiscard]] auto get_light_space_matrices() const -> const std::array<mat4f, cascade_count>&;
    [[nodiscard]] auto get_cascade_splits() const -> const std::array<float32, cascade_count>&;

    [[nodiscard]] auto get_cascade_texel_sizes() const
        -> const std::array<float32, cascade_count>&;
    [[nodiscard]] auto get_cascade_frustums() const
        -> const std::array<vw::spatial::frustum, cascade_count>&;
    [[nodiscard]] auto get_image() const -> vk::Image;
    [[nodiscard]] auto get_image_view(uint32 cascade_index) const -> vk::ImageView;
    [[nodiscard]] auto get_array_image_view() const -> vk::ImageView;
    [[nodiscard]] auto get_sampler() const -> vk::Sampler;
    [[nodiscard]] auto get_debug_sampler() const -> vk::Sampler;
    [[nodiscard]] auto get_framebuffer(uint32 cascade_index) const -> vk::Framebuffer;
    [[nodiscard]] auto get_render_pass() const -> vk::RenderPass;

private:
    auto create_shadow_map_image() -> void;
    auto create_sampler() -> void;
    auto create_framebuffers() -> void;
    auto create_render_pass() -> void;
    auto cleanup() -> void;

    [[nodiscard]] auto select_cascades_(
        const std::array<vec3f, cascade_count>& centers,
        const std::array<float32, cascade_count>& radii
    ) -> uint32;

    auto build_cascade_matrix_(
        uint32 cascade_index,
        const std::array<vec3f, 8>& corners,
        const vec3f& center,
        float32 radius,
        const vec3f& light_dir,
        float32 shadow_dist
    ) -> void;

    vulkan_context* context_;

    vk::Image shadow_image_                                              = nullptr;
    vk::DeviceMemory shadow_image_memory_                                = nullptr;
    vk::ImageView shadow_array_image_view_                               = nullptr;
    std::array<vk::ImageView, cascade_count> shadow_cascade_image_views_ = {};
    vk::Sampler shadow_sampler_                                          = nullptr;
    vk::Sampler debug_sampler_                                           = nullptr;
    std::array<vk::Framebuffer, cascade_count> shadow_framebuffers_      = {};
    vk::RenderPass shadow_render_pass_                                   = nullptr;

    std::array<mat4f, cascade_count> light_space_matrices_            = {};
    std::array<vw::spatial::frustum, cascade_count> cascade_frustums_ = {};
    std::array<float32, cascade_count> cascade_splits_                = {};

    std::array<float32, cascade_count> cascade_texel_sizes_ = {};
    std::array<vec3f, cascade_count> drawn_centers_         = {};
    std::array<float32, cascade_count> drawn_radii_         = {};

    std::array<vec3f, cascade_count> drawn_light_dirs_ = {};

    std::array<uint32, cascade_count> frames_waited_ = {};
    uint32 dirty_mask_   = 0;
    uint32 pending_mask_ = 0;

    uint32 size_;

    shadow_settings settings_{};

    float32 built_first_split_ = 0.0f;
    float32 built_distance_    = 0.0f;

    float32 cascade_padding_ratio_ = 0.18f;

    float32 cascade_trigger_ratio_ = 0.15f;
};

}  // namespace vw::gfx
