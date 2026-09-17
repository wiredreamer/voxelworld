export module vw.gfx:renderer;

export import :renderer.settings;
export import :renderer.uniforms;
export import :renderer.stats;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :camera;
import :resource;
import :debug.primitive;
import :render;
import vulkan;

namespace vw::gfx {
using namespace ::vw::ecs;
using namespace ::vw::plat;
}

export namespace vw::gfx {


class renderer final {
public:
    using world_type                = world;
    using combined_buffer_pool_type = combined_buffer_pool;
    using light_buffer_type         = light_buffer;

    renderer(vulkan_context& context, window& window, const voxel_registry& registry,
             uint32 mesh_workers = 0);
    ~renderer();

    renderer(const renderer&)            = delete;
    renderer& operator=(const renderer&) = delete;

    renderer(renderer&&)            = delete;
    renderer& operator=(renderer&&) = delete;

    auto begin_frame() -> void;
    auto render(world_type& world, camera& camera) -> void;
    auto end_frame() -> void;

    auto set_clear_color(float r, float g, float b, float a = 1.0f) -> void;
    auto set_clear_color(vec4f color) -> void;

    auto wait_idle() const -> void;

    auto handle_resize() -> void;

    auto set_render_mode(render_mode mode) -> void;

    [[nodiscard]] auto get_render_mode() const -> render_mode;

    [[nodiscard]] auto get_stats() const -> const renderer_stats&;

    [[nodiscard]] auto get_present_mode_name() const -> std::string_view;
    [[nodiscard]] static constexpr auto get_frames_in_flight() -> uint32 {
        return static_cast<uint32>(max_frames_in_flight_);
    }

    [[nodiscard]] auto get_descriptor_pool() const -> vk::DescriptorPool {
        return descriptor_pool_;
    }
    [[nodiscard]] auto get_storage_descriptor_set_layout() const -> vk::DescriptorSetLayout {
        return storage_descriptor_set_layout_;
    }

    auto draw_line(const vec3f& a, const vec3f& b, color col = colors::red_4) -> void;

    auto draw_box(const mat4f& matrix, const vec3f& size, color col = colors::red_4) -> void;
    auto draw_box(const transform& transform, const vec3f& size, color col = colors::red_4) -> void;
    auto draw_box(const vec3f& position, const vec3f& size, color col = colors::red_4) -> void;

    auto draw_triangle(const vec3f& a, const vec3f& b, const vec3f& c, color col = colors::red_4)
        -> void;
    auto draw_quad(
        const vec3f& a, const vec3f& b, const vec3f& c, const vec3f& d, color col = colors::red_4
    ) -> void;

    auto draw_grid(
        const mat4f& matrix, float cell_size, int cols, int rows, color clr = colors::red_4
    ) -> void;
    auto draw_grid(
        const transform& transform, float cell_size, int cols, int rows, color clr = colors::red_4
    ) -> void;
    auto draw_grid(
        const vec3f& position, float cell_size, int cols, int rows, color clr = colors::red_4
    ) -> void;

    [[nodiscard]] auto get_directional_light_settings() -> directional_light_settings&;
    [[nodiscard]] auto get_fog_settings() -> fog_settings&;
    [[nodiscard]] auto get_ambient_settings() -> ambient_settings&;
    [[nodiscard]] auto get_tonemap_settings() -> tonemap_settings&;
    [[nodiscard]] auto get_block_light_settings() -> block_light_settings&;

    [[nodiscard]] auto get_blob_strength() -> float32& {
        return blob_strength_;
    }

    [[nodiscard]] auto get_max_visible_lights() -> uint32&;

    [[nodiscard]] auto get_cluster_settings() -> cluster_settings&;

    auto set_cluster_readback(cluster_readback_level level) -> void;
    [[nodiscard]] auto take_cluster_readback(cull_list kind) -> std::optional<cluster_readback>;

    [[nodiscard]] auto get_cluster_grid(const camera& camera) const -> spatial::cluster_grid;
    [[nodiscard]] auto get_shadow_settings() -> shadow_settings&;

    auto set_debug_view(debug_view view) -> void;
    [[nodiscard]] auto get_debug_view() const -> debug_view;

    [[nodiscard]] auto get_cascade_splits() const -> const std::array<float32, shadow_map::cascade_count>&;
    [[nodiscard]] auto get_cascade_texel_sizes() const -> const std::array<float32, shadow_map::cascade_count>&;

    [[nodiscard]] auto get_visible_light_count() const -> uint32;

    [[nodiscard]] auto get_mesh_pool() -> mesh_pool& { return mesh_pool_; }
    [[nodiscard]] auto get_mesh_pool() const -> const mesh_pool& { return mesh_pool_; }

    auto set_chunk_cull_enabled(bool enabled) -> void {
        combined_buffer_pool_->set_chunk_cull_enabled(enabled);
    }

    [[nodiscard]] auto is_chunk_cull_enabled() const -> bool {
        return combined_buffer_pool_->is_chunk_cull_enabled();
    }

    auto draw_colliders(world_type& w, color col = colors::green_4) -> void;

    [[nodiscard]] auto get_shadow_map_texture_id(uint32 cascade_index = 0) const -> void*;

private:
    auto create_swapchain() -> void;
    auto create_image_views() -> void;
    auto create_depth_resources() -> void;
    auto create_render_pass() -> void;
    auto create_descriptor_set_layouts() -> void;
    auto create_graphics_pipeline() -> void;
    auto create_wireframe_pipeline() -> void;
    auto create_shadow_pipeline() -> void;
    auto create_debug_pipeline() -> void;
    auto create_framebuffers() -> void;
    auto create_command_buffers() -> void;
    auto create_sync_objects() -> void;
    auto create_uniform_buffers() -> void;
    auto create_shadow_uniform_buffers() -> void;
    auto create_descriptor_pool() -> void;
    auto create_descriptor_sets() -> void;
    auto create_shadow_descriptor_sets() -> void;
    auto create_shadow_map_descriptor_sets() -> void;

    auto init_imgui() -> void;
    auto setup_imgui_style() -> void;
    auto create_imgui_descriptor_pool() -> void;
    auto cleanup_imgui() -> void;

    auto cleanup_descriptor_pool() -> void;
    auto cleanup_render_pass() -> void;
    auto cleanup_descriptor_set_layouts() -> void;
    auto cleanup_pipelines() -> void;
    auto cleanup_shadow_pipeline() -> void;
    auto cleanup_debug_pipeline() -> void;
    auto cleanup_swapchain() -> void;
    auto cleanup_depth_resources() -> void;
    auto recreate_swapchain() -> void;

    auto create_point_lights_descriptor_set_layout() -> void;
    auto cleanup_point_lights_resources() -> void;

    auto create_palette_descriptor_set_layout() -> void;
    auto cleanup_palette_resources() -> void;

    auto update_shadow_uniform_buffer() const -> void;
    auto render_shadow_pass(world_type& world, const camera& camera) -> void;

    auto update_uniform_buffer(world_type& world, const camera& camera) const -> void;


    auto render_world_pass(world_type& world, const camera& camera) -> void;
    auto render_world(world_type& world, const camera& camera) -> void;

    auto sync_meshes_(world_type& world) -> void;

    auto render_debug_primitives() -> void;
    auto render_debug_solids() -> void;
    auto update_debug_vertex_buffer() -> void;
    auto update_debug_solid_vertex_buffer() -> void;

    auto render_imgui() const -> void;

    [[nodiscard]]
    auto find_depth_format() -> vk::Format;

    [[nodiscard]]
    auto find_supported_format(
        const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features
    ) -> vk::Format;

    auto create_image(
        vk::Image& image,
        vk::DeviceMemory& image_memory,
        vk::Extent2D extent,
        vk::Format format,
        vk::ImageTiling tiling,
        vk::ImageUsageFlags usage,
        vk::MemoryPropertyFlags properties,
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1
    ) -> void;

    [[nodiscard]]
    auto create_image_view(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_flags) -> vk::ImageView;

    [[nodiscard]]
    auto find_memory_type(uint32 typeFilter, vk::MemoryPropertyFlags properties) -> uint32;

    [[nodiscard]]
    static auto choose_swap_surface_format(
        const std::vector<vk::SurfaceFormatKHR>& available_formats
    ) -> vk::SurfaceFormatKHR;

    [[nodiscard]]
    static auto choose_swap_present_mode(
        const std::vector<vk::PresentModeKHR>& available_present_modes
    ) -> vk::PresentModeKHR;

    [[nodiscard]]
    auto choose_swap_extent(const vk::SurfaceCapabilitiesKHR& capabilities) -> vk::Extent2D;

    vulkan_context* context_;
    window* window_;

    vk::SwapchainKHR swapchain_ = nullptr;
    std::vector<vk::Image> swapchain_images_;
    vk::Format swapchain_image_format_ = vk::Format::eUndefined;
    vk::Extent2D swapchain_extent_{};
    vk::PresentModeKHR present_mode_   = vk::PresentModeKHR::eFifo;
    std::vector<vk::ImageView> swapchain_image_views_;

    vk::Image depth_image_               = nullptr;
    vk::DeviceMemory depth_image_memory_ = nullptr;
    vk::ImageView depth_image_view_      = nullptr;

    vk::RenderPass render_pass_                                 = nullptr;
    vk::DescriptorSetLayout uniform_descriptor_set_layout_      = nullptr;
    vk::DescriptorSetLayout storage_descriptor_set_layout_      = nullptr;
    vk::DescriptorSetLayout shadow_descriptor_set_layout_       = nullptr;
    vk::DescriptorSetLayout point_lights_descriptor_set_layout_ = nullptr;
    vk::DescriptorSetLayout palette_descriptor_set_layout_      = nullptr;
    vk::PipelineLayout pipeline_layout_                         = nullptr;
    vk::Pipeline graphics_pipeline_                             = nullptr;
    vk::Pipeline wireframe_pipeline_                            = nullptr;
    vk::Pipeline shadow_pipeline_                               = nullptr;
    vk::PipelineLayout shadow_pipeline_layout_                  = nullptr;

    std::vector<vk::Framebuffer> framebuffers_;
    std::vector<vk::CommandBuffer> command_buffers_;

    std::vector<vk::Semaphore> image_available_semaphores_;
    std::vector<vk::Semaphore> render_finished_semaphores_;
    std::vector<vk::Fence> in_flight_fences_;

    std::vector<std::unique_ptr<uniform_buffer>> uniform_buffers_;
    std::vector<std::unique_ptr<uniform_buffer>> shadow_uniform_buffers_;
    vk::DescriptorPool descriptor_pool_ = nullptr;
    std::vector<vk::DescriptorSet> descriptor_sets_;
    std::vector<vk::DescriptorSet> shadow_descriptor_sets_;
    std::vector<vk::DescriptorSet> shadow_map_descriptor_sets_;

    std::unique_ptr<shader> vertex_shader_;
    std::unique_ptr<shader> fragment_shader_;
    std::unique_ptr<shader> shadow_vertex_shader_;
    std::unique_ptr<shader> shadow_fragment_shader_;

    uint32 current_frame_       = 0;
    uint64 frame_counter_       = 0;
    uint32 current_image_index_ = 0;
    bool framebuffer_resized_     = false;
    vec4f clear_color_            = {0.1f, 0.1f, 0.1f, 1.0f};
    std::vector<vk::Fence> images_in_flight_;
    render_mode current_render_mode_ = render_mode::lit;

    vk::DescriptorPool imgui_descriptor_pool_ = nullptr;

    vk::PipelineLayout debug_pipeline_layout_ = nullptr;
    vk::Pipeline debug_pipeline_              = nullptr;
    vk::Pipeline debug_solid_pipeline_        = nullptr;
    std::unique_ptr<vertex_buffer> debug_vertex_buffer_;
    std::unique_ptr<vertex_buffer> debug_solid_vertex_buffer_;
    debug_primitives debug_primitives_;

    std::unique_ptr<shader> debug_vertex_shader_;
    std::unique_ptr<shader> debug_fragment_shader_;

    mesh_pool mesh_pool_;
    std::unordered_set<entity> pending_mesh_entities_;

    deletion_queue deletion_queue_;

    std::unique_ptr<combined_buffer_pool_type> combined_buffer_pool_;

    std::unique_ptr<light_buffer_type> light_buffer_;
    std::unique_ptr<light_grid> light_grid_;
    std::unique_ptr<blob_buffer> blob_buffer_;

    const voxel_registry* voxel_registry_;
    std::unique_ptr<palette_buffer> palette_buffer_;

    std::unique_ptr<cull_pipeline> cull_pipeline_;

    std::unique_ptr<shadow_map> shadow_map_;

    std::unique_ptr<gpu_timer> gpu_timer_;

    directional_light_settings directional_light_settings_;

    fog_settings fog_settings_;
    ambient_settings ambient_settings_;
    tonemap_settings tonemap_settings_;
    block_light_settings block_light_settings_;
    cluster_settings cluster_settings_;
    float32 blob_strength_ = 1.0f;
    debug_view debug_view_ = debug_view::off;

    mutable renderer_stats stats_;
    uint32 draw_call_count_ = 0;

    static constexpr uint32 max_frames_in_flight_ = 2;
};
}  // namespace vw::gfx
