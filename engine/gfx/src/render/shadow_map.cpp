module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

shadow_map::shadow_map(
    vulkan_context& context, uint32 size
)
    : context_(&context), size_(size) {
    for (auto& matrix : light_space_matrices_) {
        matrix = math::identity_matrix();
    }
    create_shadow_map_image();
    create_sampler();
    create_render_pass();
    create_framebuffers();
}

shadow_map::~shadow_map() {
    cleanup();
}

auto shadow_map::create_shadow_map_image() -> void {
    constexpr std::array candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };

    vk::Format depth_format = vk::Format::eUndefined;
    for (vk::Format fmt : candidates) {
        const vk::FormatProperties props = context_->get_physical_device().getFormatProperties(fmt);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            depth_format = fmt;
            break;
        }
    }
    if (depth_format == vk::Format::eUndefined) {
        throw std::runtime_error("Failed to find supported depth format for shadow map!");
    }

    vk::ImageCreateInfo image_info{};
    image_info.imageType     = vk::ImageType::e2D;
    image_info.extent.width  = size_;
    image_info.extent.height = size_;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = cascade_count;
    image_info.format        = depth_format;
    image_info.tiling        = vk::ImageTiling::eOptimal;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage   = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    image_info.samples = vk::SampleCountFlagBits::e1;
    image_info.sharingMode = vk::SharingMode::eExclusive;

    shadow_image_ = vk_must(context_->get_device().createImage(image_info), "create shadow map image");

    const vk::MemoryRequirements mem_requirements =
        context_->get_device().getImageMemoryRequirements(shadow_image_);

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = mem_requirements.size;

    const vk::PhysicalDeviceMemoryProperties mem_properties =
        context_->get_physical_device().getMemoryProperties();

    uint32 memory_type_index = std::numeric_limits<uint32>::max();
    for (uint32 i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags &
             vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == std::numeric_limits<uint32>::max()) {
        throw std::runtime_error("Failed to find suitable memory type for shadow map!");
    }

    alloc_info.memoryTypeIndex = memory_type_index;

    shadow_image_memory_ =
        vk_must(context_->get_device().allocateMemory(alloc_info), "allocate shadow map memory");

    vk_must(
        context_->get_device().bindImageMemory(shadow_image_, shadow_image_memory_, 0),
        "bind shadow map memory"
    );

    vk::ImageViewCreateInfo array_view_info{};
    array_view_info.image                           = shadow_image_;
    array_view_info.viewType                        = vk::ImageViewType::e2DArray;
    array_view_info.format                          = depth_format;
    array_view_info.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
    array_view_info.subresourceRange.baseMipLevel   = 0;
    array_view_info.subresourceRange.levelCount     = 1;
    array_view_info.subresourceRange.baseArrayLayer = 0;
    array_view_info.subresourceRange.layerCount     = cascade_count;

    shadow_array_image_view_ = vk_must(
        context_->get_device().createImageView(array_view_info), "create shadow map array view"
    );

    for (uint32 i = 0; i < cascade_count; ++i) {
        vk::ImageViewCreateInfo view_info{};
        view_info.image                           = shadow_image_;
        view_info.viewType                        = vk::ImageViewType::e2D;
        view_info.format                          = depth_format;
        view_info.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = i;
        view_info.subresourceRange.layerCount     = 1;

        shadow_cascade_image_views_[i] = vk_must(
            context_->get_device().createImageView(view_info), "create shadow map cascade view"
        );
    }
}

auto shadow_map::create_sampler() -> void {
    vk::SamplerCreateInfo sampler_info{};
    sampler_info.magFilter               = vk::Filter::eLinear;
    sampler_info.minFilter               = vk::Filter::eLinear;
    sampler_info.mipmapMode              = vk::SamplerMipmapMode::eNearest;
    sampler_info.addressModeU            = vk::SamplerAddressMode::eClampToBorder;
    sampler_info.addressModeV            = vk::SamplerAddressMode::eClampToBorder;
    sampler_info.addressModeW            = vk::SamplerAddressMode::eClampToBorder;
    sampler_info.mipLodBias              = 0.0f;
    sampler_info.anisotropyEnable        = vk::False;
    sampler_info.maxAnisotropy           = 1.0f;
    sampler_info.compareEnable           = vk::True;
    sampler_info.compareOp               = vk::CompareOp::eLessOrEqual;
    sampler_info.minLod                  = 0.0f;
    sampler_info.maxLod                  = 1.0f;
    sampler_info.borderColor             = vk::BorderColor::eFloatOpaqueWhite;
    sampler_info.unnormalizedCoordinates = vk::False;

    shadow_sampler_ =
        vk_must(context_->get_device().createSampler(sampler_info), "create shadow map sampler");

    sampler_info.compareEnable = vk::False;
    sampler_info.compareOp     = vk::CompareOp::eNever;

    debug_sampler_ =
        vk_must(context_->get_device().createSampler(sampler_info), "create shadow debug sampler");
}

auto shadow_map::create_render_pass() -> void {
    vk::AttachmentDescription depth_attachment{};
    depth_attachment.format         = vk::Format::eD32Sfloat;
    depth_attachment.samples        = vk::SampleCountFlagBits::e1;
    depth_attachment.loadOp         = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp        = vk::AttachmentStoreOp::eStore;
    depth_attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout  = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout    = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

    vk::AttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 0;
    depth_attachment_ref.layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount    = 0;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass    = vk::SubpassExternal;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = vk::PipelineStageFlagBits::eFragmentShader;
    dependency.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    dependency.dstStageMask  = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::RenderPassCreateInfo render_pass_info{};
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments    = &depth_attachment;
    render_pass_info.subpassCount    = 1;
    render_pass_info.pSubpasses      = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies   = &dependency;

    shadow_render_pass_ = vk_must(
        context_->get_device().createRenderPass(render_pass_info), "create shadow map render pass"
    );
}

auto shadow_map::create_framebuffers() -> void {
    for (uint32 i = 0; i < cascade_count; ++i) {
        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.renderPass      = shadow_render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments    = &shadow_cascade_image_views_[i];
        framebuffer_info.width           = size_;
        framebuffer_info.height          = size_;
        framebuffer_info.layers          = 1;

        shadow_framebuffers_[i] = vk_must(
            context_->get_device().createFramebuffer(framebuffer_info),
            "create shadow map framebuffer"
        );
    }
}

auto shadow_map::invalidate(
    const vw::spatial::aabb& bounds
) -> void {
    for (uint32 i = 0; i < cascade_count; ++i) {
        if (cascade_frustums_[i].intersects(bounds)) {
            dirty_mask_ |= 1U << i;
        }
    }
}

auto shadow_map::invalidate_all() -> void {
    dirty_mask_ = (1U << cascade_count) - 1;
}

auto shadow_map::is_cascade_pending(
    uint32 cascade_index
) const -> bool {
    return (pending_mask_ & (1U << cascade_index)) != 0;
}

auto shadow_map::get_pending_count() const -> uint32 {
    return static_cast<uint32>(std::popcount(pending_mask_));
}

auto shadow_map::clear_pending() -> void {
    pending_mask_ = 0;
}

auto shadow_map::update(
    const camera& camera, const vec3f& light_direction
) -> void {
    const vec3f light_dir = math::normalize(light_direction);

    const float cam_near   = camera.get_near();
    const float cam_far    = camera.get_far();
    const float shadow_far = std::min(settings_.distance, cam_far);

    if (settings_.first_split != built_first_split_ ||
        settings_.distance != built_distance_) {
        built_first_split_ = settings_.first_split;
        built_distance_    = settings_.distance;
        invalidate_all();
    }

    const float first =
        std::clamp(settings_.first_split, cam_near * 2.0f, shadow_far * 0.5f);
    const float ratio = std::pow(
        shadow_far / first, 1.0f / static_cast<float>(cascade_count - 1)
    );

    float split = first;
    for (uint32 i = 0; i < cascade_count; ++i) {
        cascade_splits_[i] = split - cam_near;
        split *= ratio;
    }
    cascade_splits_[cascade_count - 1] = shadow_far - cam_near;

    const float shadow_dist = shadow_far - cam_near;

    const std::array<vec3f, 8> shadowed_corners = camera.frustum_corners(cam_near, shadow_far);

    std::array<std::array<vec3f, 8>, cascade_count> cascade_corners{};
    std::array<vec3f, cascade_count> centers{};
    std::array<float32, cascade_count> radii{};

    float last_cascade_split = 0.0f;
    for (uint32 cascade_index = 0; cascade_index < cascade_count; ++cascade_index) {
        const float cascade_split = cascade_splits_[cascade_index];

        std::array<vec3f, 8> frustum_corners = shadowed_corners;

        for (int i = 0; i < 4; ++i) {
            vec3 dist              = frustum_corners[i + 4] - frustum_corners[i];
            frustum_corners[i + 4] = frustum_corners[i] + dist * (cascade_split / shadow_dist);
            frustum_corners[i]     = frustum_corners[i] + dist * (last_cascade_split / shadow_dist);
        }

        const vec3 frustum_center = camera.get_position();

        float radius = 0.0f;
        for (const auto& corner : frustum_corners) {
            float distance = math::length(corner - frustum_center);
            radius         = std::max(radius, distance);
        }

        cascade_corners[cascade_index] = frustum_corners;
        centers[cascade_index]         = frustum_center;
        radii[cascade_index]           = radius;

        last_cascade_split = cascade_split;
    }

    for (uint32 i = 0; i < cascade_count; ++i) {
        const float32 turn = math::length(light_dir - drawn_light_dirs_[i]);
        const float32 threshold =
            settings_.turn_texels * cascade_texel_sizes_[i] / std::max(radii[i], 1.0f);

        if (turn > threshold) {
            dirty_mask_ |= 1U << i;
        }
    }

    pending_mask_ |= select_cascades_(centers, radii);

    for (uint32 cascade_index = 0; cascade_index < cascade_count; ++cascade_index) {
        if ((pending_mask_ & (1U << cascade_index)) == 0) {
            continue;
        }

        build_cascade_matrix_(
            cascade_index,
            cascade_corners[cascade_index],
            centers[cascade_index],
            radii[cascade_index],
            light_dir,
            shadow_dist
        );
    }

    dirty_mask_ &= ~pending_mask_;
}

auto shadow_map::select_cascades_(
    const std::array<vec3f, cascade_count>& centers,
    const std::array<float32, cascade_count>& radii
) -> uint32 {
    uint32 selected = 0;
    std::array<float32, cascade_count> priority{};

    for (uint32 i = 0; i < cascade_count; ++i) {
        const uint32 bit    = 1U << i;
        const float32 drift = math::length(centers[i] - drawn_centers_[i]);
        const float32 reach = radii[i] > 0.0f ? drift / radii[i] : 0.0f;

        const bool stale = reach > cascade_padding_ratio_ || radii[i] > drawn_radii_[i];

        if (stale) {
            priority[i] = 100.0f + reach;
        } else if ((dirty_mask_ & bit) != 0) {
            priority[i] = 1.0f + reach +
                          (static_cast<float32>(std::min(frames_waited_[i], 1000U)) * 0.001f);
        } else if (reach > cascade_trigger_ratio_) {
            priority[i] = reach;
        } else {
            priority[i] = -1.0f;
        }
    }

    uint32 budget = settings_.updates_per_frame;

    while (budget > 0) {
        uint32 best        = cascade_count;
        float32 best_score = 0.0f;

        for (uint32 i = 0; i < cascade_count; ++i) {
            if (priority[i] > best_score) {
                best_score = priority[i];
                best       = i;
            }
        }

        if (best == cascade_count) {
            break;
        }

        selected |= 1U << best;
        priority[best] = -1.0f;
        --budget;
    }

    for (uint32 i = 0; i < cascade_count; ++i) {
        const uint32 bit = 1U << i;

        if ((selected & bit) != 0) {
            frames_waited_[i] = 0;
        } else if ((dirty_mask_ & bit) != 0) {
            ++frames_waited_[i];
        }
    }

    return selected;
}

auto shadow_map::build_cascade_matrix_(
    uint32 cascade_index,
    const std::array<vec3f, 8>& corners,
    const vec3f& center,
    float32 radius,
    const vec3f& light_dir,
    float32 shadow_dist
) -> void {
    drawn_centers_[cascade_index]    = center;
    drawn_radii_[cascade_index]      = radius;
    drawn_light_dirs_[cascade_index] = light_dir;

    radius += radius * cascade_padding_ratio_;

    cascade_texel_sizes_[cascade_index] = 2.0f * radius / static_cast<float32>(size_);

    const vec3f max_extents = vec3f{radius, radius, radius};
    const vec3f min_extents = -max_extents;

    const vec3f target = center;
    const vec3f eye    = target - light_dir * shadow_dist;

    auto up = vec3f{0.0f, 1.0f, 0.0f};
    if (std::abs(math::dot(up, light_dir)) > 0.99f) {
        up = vec3f{1.0f, 0.0f, 0.0f};
    }

    const mat4f light_view = math::look_at_matrix(eye, target, up);

    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (const auto& corner : corners) {
        auto lv = light_view * vec4f{corner.x, corner.y, corner.z, 1.0f};
        min_z   = std::min(min_z, lv.z);
        max_z   = std::max(max_z, lv.z);
    }

    const float ortho_near = std::max(-max_z - shadow_dist, 0.001f);
    const float ortho_far  = -min_z + (radius * cascade_padding_ratio_);

    auto light_proj = math::orthographic_matrix(
        min_extents.x, max_extents.x, min_extents.y, max_extents.y, ortho_near, ortho_far
    );

    auto lsm = light_proj * light_view;

    const float half_size = static_cast<float>(size_) * 0.5f;
    vec4f shadow_origin   = lsm * vec4f{0.0f, 0.0f, 0.0f, 1.0f};
    const float rounded_x = std::round(shadow_origin.x * half_size);
    const float rounded_y = std::round(shadow_origin.y * half_size);
    lsm[0, 3] += (rounded_x - shadow_origin.x * half_size) / half_size;
    lsm[1, 3] += (rounded_y - shadow_origin.y * half_size) / half_size;

    light_space_matrices_[cascade_index] = lsm;
    cascade_frustums_[cascade_index] = vw::spatial::frustum::from_view_projection_matrix(lsm);
}

auto shadow_map::get_cascade_texel_sizes() const
    -> const std::array<float32, cascade_count>& {
    return cascade_texel_sizes_;
}

auto shadow_map::get_light_space_matrix(
    uint32 cascade_index
) const -> mat4f {
    return light_space_matrices_[cascade_index];
}

auto shadow_map::
    get_light_space_matrices() const -> const std::array<mat4f, cascade_count>& {
    return light_space_matrices_;
}

auto shadow_map::get_cascade_splits() const -> const std::array<float, cascade_count>& {
    return cascade_splits_;
}

auto shadow_map::get_cascade_frustums() const -> const std::array<vw::spatial::frustum, cascade_count>& {
    return cascade_frustums_;
}

auto shadow_map::get_image() const -> vk::Image {
    return shadow_image_;
}

auto shadow_map::get_image_view(
    uint32 cascade_index
) const -> vk::ImageView {
    return shadow_cascade_image_views_[cascade_index];
}

auto shadow_map::get_array_image_view() const -> vk::ImageView {
    return shadow_array_image_view_;
}

auto shadow_map::get_sampler() const -> vk::Sampler {
    return shadow_sampler_;
}

auto shadow_map::get_debug_sampler() const -> vk::Sampler {
    return debug_sampler_;
}

auto shadow_map::get_framebuffer(
    uint32 cascade_index
) const -> vk::Framebuffer {
    return shadow_framebuffers_[cascade_index];
}

auto shadow_map::get_render_pass() const -> vk::RenderPass {
    return shadow_render_pass_;
}

auto shadow_map::cleanup() -> void {
    const vk::Device device = context_->get_device();

    for (uint32 i = 0; i < cascade_count; ++i) {
        device.destroyFramebuffer(shadow_framebuffers_[i]);
        shadow_framebuffers_[i] = nullptr;
        device.destroyImageView(shadow_cascade_image_views_[i]);
        shadow_cascade_image_views_[i] = nullptr;
    }
    device.destroyImageView(shadow_array_image_view_);
    shadow_array_image_view_ = nullptr;
    device.destroyRenderPass(shadow_render_pass_);
    shadow_render_pass_ = nullptr;
    device.destroySampler(shadow_sampler_);
    shadow_sampler_ = nullptr;
    device.destroySampler(debug_sampler_);
    debug_sampler_ = nullptr;
    device.destroyImage(shadow_image_);
    shadow_image_ = nullptr;
    device.freeMemory(shadow_image_memory_);
    shadow_image_memory_ = nullptr;
}

}  // namespace vw::gfx
