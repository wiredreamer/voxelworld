export module vw.gfx:renderer;

export import :renderer.settings;
export import :renderer.uniforms;
export import :renderer.stats;

import std;

import vw.core;
import :frames_in_flight;
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


struct frame_capture_request {
    bool with_interface = false;
    bool with_overlays  = false;
};

class renderer final {
public:
    using world_type                = world;
    using combined_buffer_pool_type = combined_buffer_pool;
    using light_buffer_type         = light_buffer;

    renderer(vulkan_context& context, window& window, const voxel_registry& registry,
             vw::job_system& jobs, uint32 wanted_msaa_samples = msaa_sample_count);
    ~renderer();

    renderer(const renderer&)            = delete;
    renderer& operator=(const renderer&) = delete;

    renderer(renderer&&)            = delete;
    renderer& operator=(renderer&&) = delete;

    auto track(world_type& world) -> void;
    [[nodiscard]] auto begin_frame() -> bool;
    auto render(world_type& world, camera& camera) -> void;
    auto end_frame() -> void;

    auto set_clear_color(float r, float g, float b, float a = 1.0f) -> void;
    auto set_clear_color(vec4f color) -> void;

    auto wait_idle() const -> void;

    auto handle_resize() -> void;

    [[nodiscard]] auto has_drawable_surface() const -> bool;

    [[nodiscard]] auto has_pending_meshes() const -> bool;

    [[nodiscard]] auto request_capture(const frame_capture_request& request) -> bool;
    [[nodiscard]] auto take_capture() -> std::optional<image_rgba>;

    auto set_render_mode(render_mode mode) -> void;

    [[nodiscard]] auto get_render_mode() const -> render_mode;

    [[nodiscard]] auto get_stats() const -> const renderer_stats&;

    [[nodiscard]] auto get_present_mode_name() const -> std::string_view;
    [[nodiscard]] auto get_msaa_samples() const -> uint32 {
        return static_cast<uint32>(msaa_samples_);
    }
    [[nodiscard]] static constexpr auto get_frames_in_flight() -> uint32 {
        return static_cast<uint32>(frames_in_flight);
    }

    [[nodiscard]] auto get_descriptor_pool() const -> vk::DescriptorPool {
        return descriptor_pool_;
    }
    [[nodiscard]] auto get_storage_descriptor_set_layout() const -> vk::DescriptorSetLayout {
        return storage_descriptor_set_layout_;
    }

    auto draw_line(const vec3f& a, const vec3f& b, color col = colors::red_8) -> void;

    auto draw_box(const mat4f& matrix, const vec3f& size, color col = colors::red_8) -> void;
    auto draw_box(const transform& transform, const vec3f& size, color col = colors::red_8) -> void;
    auto draw_box(const vec3f& position, const vec3f& size, color col = colors::red_8) -> void;

    auto draw_triangle(const vec3f& a, const vec3f& b, const vec3f& c, color col = colors::red_8)
        -> void;
    auto draw_quad(
        const vec3f& a, const vec3f& b, const vec3f& c, const vec3f& d, color col = colors::red_8
    ) -> void;

    auto draw_grid(
        const mat4f& matrix, float cell_size, int cols, int rows, color clr = colors::red_8
    ) -> void;
    auto draw_grid(
        const transform& transform, float cell_size, int cols, int rows, color clr = colors::red_8
    ) -> void;
    auto draw_grid(
        const vec3f& position, float cell_size, int cols, int rows, color clr = colors::red_8
    ) -> void;

    [[nodiscard]] auto get_directional_light_settings() -> directional_light_settings&;
    [[nodiscard]] auto get_fog_settings() -> fog_settings&;
    [[nodiscard]] auto get_grass_settings() -> grass_settings&;
    [[nodiscard]] auto get_wind_settings() -> wind_settings&;
    [[nodiscard]] auto get_grass_stats() const -> const grass_stats&;
    [[nodiscard]] auto get_ambient_settings() -> ambient_settings&;
    [[nodiscard]] auto get_tonemap_settings() -> tonemap_settings&;
    [[nodiscard]] auto get_bloom_settings() -> bloom_settings&;
    [[nodiscard]] auto get_light_cache_settings() -> light_cache_settings& {
        return light_cache_settings_;
    }

    // см. docs/rendering.md#приборы-кадра
    [[nodiscard]] auto get_render_extent() const -> vec2<uint32> {
        return {swapchain_extent_.width, swapchain_extent_.height};
    }

    [[nodiscard]] auto get_shading_parts() -> shading_parts& {
        return shading_parts_;
    }

    // см. docs/rendering.md#отсев-по-заслонам
    [[nodiscard]] auto get_occlusion_settings() -> occlusion_settings& {
        return occlusion_settings_;
    }

    auto set_frame_probe_enabled(bool enabled) -> void {
        frame_probe_->set_enabled(enabled);
    }

    [[nodiscard]] auto get_frame_probe_stats() const -> const frame_probe_stats& {
        return frame_probe_->get_stats();
    }
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

    auto draw_colliders(world_type& w, color col = colors::green_8) -> void;

    [[nodiscard]] auto get_shadow_map_texture_id(uint32 cascade_index = 0) const -> void*;

private:
    auto create_swapchain() -> void;
    auto create_image_views() -> void;
    auto choose_msaa_samples() -> void;
    auto create_color_resources() -> void;
    auto create_depth_resources() -> void;
    auto create_render_pass() -> void;
    auto create_composite_pass() -> void;
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
    auto cleanup_color_resources() -> void;
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
    auto render_post_() -> void;
    auto cover_swapchain_() -> void;
    [[nodiscard]] auto tonemap_push_() const -> vec4f;
    [[nodiscard]] auto grid_push_() const -> vec4f;
    auto render_world(world_type& world, const camera& camera) -> void;

    auto sync_meshes_(world_type& world) -> void;

    auto render_debug_primitives() -> void;
    auto render_debug_solids() -> void;
    auto update_debug_vertex_buffer() -> void;
    auto update_debug_solid_vertex_buffer() -> void;

    auto render_imgui() const -> void;

    auto record_capture_() -> void;
    auto resolve_capture_() -> void;
    [[nodiscard]] auto draws_interface_() const -> bool;
    [[nodiscard]] auto draws_overlays_() const -> bool;

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

    uint32 wanted_msaa_samples_           = msaa_sample_count;
    vk::SampleCountFlagBits msaa_samples_ = vk::SampleCountFlagBits::e1;

    vk::Image color_image_               = nullptr;
    vk::DeviceMemory color_image_memory_ = nullptr;
    vk::ImageView color_image_view_      = nullptr;

    vk::Image scene_image_               = nullptr;
    vk::DeviceMemory scene_image_memory_ = nullptr;
    vk::ImageView scene_image_view_      = nullptr;

    vk::Image depth_image_               = nullptr;
    vk::DeviceMemory depth_image_memory_ = nullptr;
    vk::ImageView depth_image_view_      = nullptr;

    vk::RenderPass render_pass_                                 = nullptr;
    vk::RenderPass composite_pass_                              = nullptr;
    vk::DescriptorSetLayout uniform_descriptor_set_layout_      = nullptr;
    vk::DescriptorSetLayout storage_descriptor_set_layout_      = nullptr;
    vk::DescriptorSetLayout shadow_descriptor_set_layout_       = nullptr;
    vk::DescriptorSetLayout point_lights_descriptor_set_layout_ = nullptr;
    vk::DescriptorSetLayout palette_descriptor_set_layout_      = nullptr;
    vk::PipelineLayout pipeline_layout_                         = nullptr;
    vk::Pipeline graphics_pipeline_                             = nullptr;
    vk::Pipeline wireframe_pipeline_                            = nullptr;
    vk::Pipeline overdraw_pipeline_                             = nullptr;
    vk::Pipeline marked_pipeline_                               = nullptr;
    vk::Pipeline shadow_pipeline_                               = nullptr;
    vk::PipelineLayout shadow_pipeline_layout_                  = nullptr;

    vk::Framebuffer scene_framebuffer_ = nullptr;
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
    std::unique_ptr<shader> mark_fragment_shader_;
    std::unique_ptr<shader> shadow_vertex_shader_;
    std::unique_ptr<shader> shadow_fragment_shader_;

    uint32 current_frame_       = 0;
    uint64 frame_counter_       = 0;
    uint32 current_image_index_ = 0;
    bool framebuffer_resized_     = false;
    bool swapchain_stale_         = false;

    bool capture_supported_ = false;
    bool capture_recorded_  = false;
    vk::Extent2D capture_extent_{};
    std::optional<frame_capture_request> capture_request_;
    std::unique_ptr<buffer> capture_buffer_;
    std::optional<image_rgba> captured_;
    vec4f clear_color_            = {0.1f, 0.1f, 0.1f, 1.0f};
    render_mode current_render_mode_ = render_mode::lit;

    vk::DescriptorPool imgui_descriptor_pool_ = nullptr;

    vk::PipelineLayout debug_pipeline_layout_ = nullptr;
    vk::Pipeline debug_pipeline_              = nullptr;
    vk::Pipeline debug_solid_pipeline_        = nullptr;
    std::array<std::unique_ptr<vertex_buffer>, frames_in_flight> debug_vertex_buffers_;
    std::array<std::unique_ptr<vertex_buffer>, frames_in_flight> debug_solid_vertex_buffers_;
    debug_primitives debug_primitives_;

    std::unique_ptr<shader> debug_vertex_shader_;
    std::unique_ptr<shader> debug_fragment_shader_;

    mesh_pool mesh_pool_;
    std::unordered_set<entity> pending_mesh_entities_;
    std::vector<entity> changed_model_entities_;

    deletion_queue deletion_queue_;

    std::unique_ptr<combined_buffer_pool_type> combined_buffer_pool_;

    std::unique_ptr<light_buffer_type> light_buffer_;
    std::unique_ptr<light_grid> light_grid_;
    std::unique_ptr<blob_buffer> blob_buffer_;

    const voxel_registry* voxel_registry_;
    std::unique_ptr<palette_buffer> palette_buffer_;
    std::unique_ptr<grass_renderer> grass_;
    std::unique_ptr<post_process> post_process_;
    std::unique_ptr<model_occupancy_buffer> model_volumes_;
    std::unique_ptr<occupancy_clipmap> occupancy_;
    std::unique_ptr<occupancy_view> occupancy_view_;
    std::unique_ptr<light_cache> light_cache_;
    light_cache_settings light_cache_settings_;

    std::unique_ptr<occluder_pass> occluders_;
    std::unique_ptr<cull_pipeline> cull_pipeline_;

    std::unique_ptr<shadow_map> shadow_map_;

    std::unique_ptr<gpu_timer> gpu_timer_;

    directional_light_settings directional_light_settings_;

    fog_settings fog_settings_;
    grass_settings grass_settings_;
    wind_settings wind_settings_;
    std::chrono::steady_clock::time_point wind_start_ = std::chrono::steady_clock::now();
    float32 wind_time_ = 0.0F;

    [[nodiscard]] auto wind_push_() const -> vec4f;
    ambient_settings ambient_settings_;
    tonemap_settings tonemap_settings_;
    bloom_settings bloom_settings_;
    block_light_settings block_light_settings_;
    cluster_settings cluster_settings_;
    float32 blob_strength_ = 1.0f;
    debug_view debug_view_ = debug_view::off;
    shading_parts shading_parts_{};
    occlusion_settings occlusion_settings_{};
    bool checks_occlusion_ = false;
    std::unique_ptr<frame_probe> frame_probe_;

    mutable renderer_stats stats_;
    uint32 draw_call_count_ = 0;

};
}  // namespace vw::gfx
