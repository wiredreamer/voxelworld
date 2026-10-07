module;

#include <cstddef>

export module vw.gfx:render.grass;

import std;

import vw.core;
import :frames_in_flight;
import vw.asset;
import vw.ecs;
import vw.world;
import :camera;
import :resource;
import :render.vulkan_context;
import :renderer.settings;
import vulkan;

export namespace vw::gfx {

struct grass_instance {
    alignas(16) vec4f place;
    alignas(16) vec4f light;
};

static_assert(offsetof(grass_instance, light) == 16);
static_assert(sizeof(grass_instance) == 32);

struct grass_push_constants {
    alignas(16) vec4f wind;
    alignas(16) vec4f eye;
    alignas(16) vec4f shape;
};

static_assert(offsetof(grass_push_constants, eye) == 16);
static_assert(offsetof(grass_push_constants, shape) == 32);
static_assert(sizeof(grass_push_constants) == 48);

struct grass_stats {
    uint32 instances = 0;
    uint32 meshes    = 0;
    uint32 draws     = 0;
    uint32 chunks    = 0;
};

struct grass_pipeline_layouts {
    vk::DescriptorSetLayout uniform;
    vk::DescriptorSetLayout shadow;
    vk::DescriptorSetLayout lights;
    vk::DescriptorSetLayout palette;
};

struct grass_bound_sets {
    vk::DescriptorSet uniform;
    vk::DescriptorSet shadow;
    vk::DescriptorSet lights;
    vk::DescriptorSet palette;
};

// см. docs/rendering.md#трава
class grass_renderer {
public:
    grass_renderer(
        vulkan_context& context, vk::DescriptorPool descriptor_pool, vk::RenderPass render_pass,
        vk::SampleCountFlagBits samples, const grass_pipeline_layouts& layouts,
        const vk::PipelineShaderStageCreateInfo& fragment_stage
    );
    ~grass_renderer();

    grass_renderer(const grass_renderer&)                    = delete;
    auto operator=(const grass_renderer&) -> grass_renderer& = delete;
    grass_renderer(grass_renderer&&)                         = delete;
    auto operator=(grass_renderer&&) -> grass_renderer&      = delete;

    auto prepare(ecs::world& world, const camera& camera, const grass_settings& settings,
                 const wind_settings& wind, float32 wind_time, uint32 frame)
        -> void;
    auto draw(vk::CommandBuffer cmd, uint32 frame, const grass_bound_sets& sets) -> void;

    [[nodiscard]] auto get_stats() const -> const grass_stats& {
        return stats_;
    }

private:
    struct tuft_mesh {
        uint32 quad_offset = 0;
        uint32 quad_count  = 0;
    };

    struct chunk_grass {
        uint64 revision                     = 0;
        const asset::light_field* sky       = nullptr;
        const asset::light_field* block     = nullptr;
        const asset::light_field* sky_above = nullptr;
        std::vector<std::pair<uint16, grass_instance>> instances;
        uint64 seen                         = 0;
    };

    struct frame_buffers {
        std::unique_ptr<storage_buffer> instances;
        std::unique_ptr<storage_buffer> quads;
        uint32 instance_capacity = 0;
        uint32 quad_capacity     = 0;
        uint64 quads_written     = 0;
        vk::DescriptorSet set    = nullptr;
    };

    struct mesh_draw {
        uint16 mesh           = 0;
        uint32 first_instance = 0;
        uint32 count          = 0;
    };

    auto create_pipeline_(
        vk::RenderPass render_pass, vk::SampleCountFlagBits samples,
        const grass_pipeline_layouts& layouts, const vk::PipelineShaderStageCreateInfo& fragment_stage
    ) -> void;
    auto refresh_chunk_(
        ecs::world& world, const ecs::world_grid& grid, vec3i coord, const ecs::chunk& c,
        chunk_grass& entry
    ) -> void;
    [[nodiscard]] auto mesh_for_(ecs::world& world, uint8 form, voxel look) -> uint16;
    auto ensure_frame_buffers_(frame_buffers& buffers, uint32 instances) -> void;

    vulkan_context* context_;
    vk::DescriptorPool descriptor_pool_ = nullptr;

    std::unique_ptr<shader> vertex_shader_;
    vk::DescriptorSetLayout set_layout_ = nullptr;
    vk::PipelineLayout pipeline_layout_ = nullptr;
    vk::Pipeline pipeline_              = nullptr;

    std::unique_ptr<index_buffer> indices_;

    std::vector<quad> quads_;
    std::vector<tuft_mesh> meshes_;
    std::unordered_map<uint16, uint16> mesh_by_kind_;
    uint64 quads_revision_ = 0;
    mesh_generation_storage mesh_scratch_;

    std::unordered_map<vec3i, chunk_grass> chunks_;
    uint64 frame_number_ = 0;

    std::array<frame_buffers, frames_in_flight> frames_{};
    std::vector<mesh_draw> draws_;
    std::vector<uint32> mesh_counts_;
    grass_push_constants push_{};

    grass_stats stats_;
};

}  // namespace vw::gfx
