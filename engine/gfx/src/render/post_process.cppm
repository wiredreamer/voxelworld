export module vw.gfx:render.post_process;

import std;

import vw.core;
import :resource;
import :render.vulkan_context;
import :renderer.settings;
import vulkan;

export namespace vw::gfx {

struct post_push_constants {
    alignas(16) vec4f params;
};

static_assert(sizeof(post_push_constants) == 16);

// см. docs/rendering.md#кадр-в-hdr
class post_process {
public:
    static constexpr uint32 bloom_level_count = 5;
    static constexpr uint32 sampled_image_count = bloom_level_count + 1;

    static constexpr vk::Format scene_format = vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format bloom_format = vk::Format::eB10G11R11UfloatPack32;

    post_process(
        vulkan_context& context, vk::DescriptorPool descriptor_pool, vk::RenderPass composite_pass
    );
    ~post_process();

    post_process(const post_process&)                    = delete;
    auto operator=(const post_process&) -> post_process& = delete;
    post_process(post_process&&)                         = delete;
    auto operator=(post_process&&) -> post_process&      = delete;

    auto resize(vk::Extent2D extent, vk::ImageView scene_view) -> void;

    auto record_bloom(vk::CommandBuffer cmd) const -> void;

    auto draw_composite(
        vk::CommandBuffer cmd, const tonemap_settings& tonemap, const bloom_settings& bloom
    ) const -> void;

private:
    struct bloom_level {
        vk::Image image              = nullptr;
        vk::DeviceMemory memory      = nullptr;
        vk::ImageView view           = nullptr;
        vk::Framebuffer framebuffer  = nullptr;
        vk::DescriptorSet sampled_as = nullptr;
        vk::Extent2D extent{};
    };

    auto create_sampler_() -> void;
    auto create_layouts_() -> void;
    auto create_bloom_passes_() -> void;
    auto create_pipelines_(vk::RenderPass composite_pass) -> void;
    auto allocate_sets_() -> void;

    auto create_level_(bloom_level& level, vk::Extent2D extent) -> void;
    auto destroy_level_(bloom_level& level) const -> void;
    auto point_set_at_(vk::DescriptorSet set, vk::ImageView view) const -> void;

    auto blur_into_(
        vk::CommandBuffer cmd, const bloom_level& target, vk::DescriptorSet source,
        vk::Extent2D source_extent, bool upward, bool takes_glow_share
    ) const -> void;

    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_;

    shader fullscreen_vertex_;
    shader bloom_down_fragment_;
    shader bloom_up_fragment_;
    shader composite_fragment_;

    vk::Sampler sampler_                  = nullptr;
    vk::DescriptorSetLayout sampled_layout_ = nullptr;
    vk::PipelineLayout blur_layout_       = nullptr;
    vk::PipelineLayout composite_layout_  = nullptr;

    vk::RenderPass bloom_down_pass_ = nullptr;
    vk::RenderPass bloom_up_pass_   = nullptr;

    vk::Pipeline bloom_down_pipeline_ = nullptr;
    vk::Pipeline bloom_up_pipeline_   = nullptr;
    vk::Pipeline composite_pipeline_  = nullptr;

    vk::DescriptorSet scene_sampled_as_ = nullptr;
    vk::Extent2D scene_extent_{};
    std::array<bloom_level, bloom_level_count> levels_{};
};

}  // namespace vw::gfx
