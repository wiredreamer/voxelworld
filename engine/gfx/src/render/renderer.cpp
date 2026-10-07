module;

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

renderer::renderer(
    vulkan_context& context, window& window, const voxel_registry& registry, vw::job_system& jobs,
    uint32 wanted_msaa_samples
)
    : context_(&context)
    , window_(&window)
    , mesh_pool_(registry, jobs)
    , voxel_registry_(&registry)
    , wanted_msaa_samples_(wanted_msaa_samples) {
    vertex_shader_ =
        std::make_unique<shader>(*context_, "shaders/voxel.vert.spv", shader_type::VERTEX);
    fragment_shader_ =
        std::make_unique<shader>(*context_, "shaders/voxel.frag.spv", shader_type::FRAGMENT);

    debug_vertex_shader_ =
        std::make_unique<shader>(*context_, "shaders/debug.vert.spv", shader_type::VERTEX);
    debug_fragment_shader_ =
        std::make_unique<shader>(*context_, "shaders/debug.frag.spv", shader_type::FRAGMENT);

    shadow_vertex_shader_ =
        std::make_unique<shader>(*context_, "shaders/shadow.vert.spv", shader_type::VERTEX);
    shadow_fragment_shader_ =
        std::make_unique<shader>(*context_, "shaders/shadow.frag.spv", shader_type::FRAGMENT);

    constexpr vk::DeviceSize initial_size = 512 * 2 * sizeof(debug_vertex);
    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        debug_vertex_buffers_[frame]       = std::make_unique<vertex_buffer>(*context_, initial_size);
        debug_solid_vertex_buffers_[frame] = std::make_unique<vertex_buffer>(*context_, initial_size);
    }

    shadow_map_ = std::make_unique<shadow_map>(*context_);

    create_swapchain();
    create_image_views();
    choose_msaa_samples();
    create_color_resources();
    create_depth_resources();
    create_render_pass();
    create_composite_pass();
    create_descriptor_set_layouts();
    create_point_lights_descriptor_set_layout();
    create_palette_descriptor_set_layout();
    create_graphics_pipeline();
    create_wireframe_pipeline();
    create_shadow_pipeline();
    create_debug_pipeline();
    create_framebuffers();
    create_command_buffers();
    create_sync_objects();
    create_uniform_buffers();
    create_shadow_uniform_buffers();
    create_descriptor_pool();
    create_descriptor_sets();
    create_shadow_descriptor_sets();
    create_shadow_map_descriptor_sets();

    create_imgui_descriptor_pool();
    init_imgui();

    gpu_timer_ = std::make_unique<gpu_timer>(*context_, get_frames_in_flight());

    cull_pipeline_ = std::make_unique<cull_pipeline>(*context_, descriptor_pool_);

    combined_buffer_pool_ = std::make_unique<combined_buffer_pool_type>(
        *context_,
        deletion_queue_,
        descriptor_pool_,
        storage_descriptor_set_layout_,
        cull_pipeline_->get_buffer_descriptor_set_layout()
    );

    light_buffer_ = std::make_unique<light_buffer_type>(
        *context_, deletion_queue_, descriptor_pool_, point_lights_descriptor_set_layout_
    );

    std::array<vk::DescriptorSet, frames_in_flight> light_sets{};
    for (uint32 frame = 0; frame < frames_in_flight; ++frame) {
        light_sets[frame] = light_buffer_->get_descriptor_set(frame);
    }

    blob_buffer_ = std::make_unique<blob_buffer>(*context_, deletion_queue_, light_sets);

    light_grid_ = std::make_unique<light_grid>(
        *context_, deletion_queue_, descriptor_pool_, point_lights_descriptor_set_layout_,
        light_sets
    );

    palette_buffer_ = std::make_unique<palette_buffer>(
        *context_, descriptor_pool_, palette_descriptor_set_layout_, *voxel_registry_
    );

    grass_ = std::make_unique<grass_renderer>(
        *context_, descriptor_pool_, render_pass_, msaa_samples_,
        grass_pipeline_layouts{
            .uniform = uniform_descriptor_set_layout_,
            .shadow  = shadow_descriptor_set_layout_,
            .lights  = point_lights_descriptor_set_layout_,
            .palette = palette_descriptor_set_layout_,
        },
        fragment_shader_->get_stage_info()
    );

    post_process_ = std::make_unique<post_process>(*context_, descriptor_pool_, composite_pass_);
    post_process_->resize(swapchain_extent_, scene_image_view_);
}

renderer::~renderer() {
    mesh_pool_.stop_gen_threads();
    wait_idle();

    post_process_.reset();
    grass_.reset();
    combined_buffer_pool_.reset();
    cull_pipeline_.reset();
    gpu_timer_.reset();
    palette_buffer_.reset();
    light_grid_.reset();
    blob_buffer_.reset();
    light_buffer_.reset();
    shadow_map_.reset();

    cleanup_imgui();

    cleanup_swapchain();
    cleanup_color_resources();
    cleanup_depth_resources();
    cleanup_pipelines();
    cleanup_shadow_pipeline();
    cleanup_debug_pipeline();
    cleanup_point_lights_resources();
    cleanup_palette_resources();
    cleanup_descriptor_set_layouts();
    cleanup_render_pass();
    cleanup_descriptor_pool();
}

auto renderer::begin_frame() -> bool {
    if (swapchain_stale_ || !has_drawable_surface()) {
        recreate_swapchain();
        if (swapchain_stale_) {
            return false;
        }
    }

    const vk::Device device = context_->get_device();
    vk_must(
        device.waitForFences(in_flight_fences_[current_frame_], vk::True, std::numeric_limits<uint64>::max()),
        "wait for frame fence"
    );

    vk_must(device.resetFences(in_flight_fences_[current_frame_]), "reset frame fence");

    if (frame_counter_ >= frames_in_flight) {
        deletion_queue_.collect(frame_counter_ - frames_in_flight);
    }
    deletion_queue_.set_frame(frame_counter_);

    gpu_timer_->resolve(current_frame_);
    stats_.timing.gpu = gpu_timer_->get_stats();

    uint32 image_index = 0;
    const vk::Result result = device.acquireNextImageKHR(
        swapchain_,
        std::numeric_limits<uint64>::max(),
        image_available_semaphores_[current_frame_],
        nullptr,
        &image_index
    );

    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreate_swapchain();
        return false;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    current_image_index_ = image_index;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    return true;
}

auto renderer::end_frame() -> void {
    vk::SubmitInfo submit_info{};

    vk::Semaphore wait_semaphores[]      = {image_available_semaphores_[current_frame_]};
    vk::PipelineStageFlags wait_stages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    submit_info.waitSemaphoreCount     = 1;
    submit_info.pWaitSemaphores        = wait_semaphores;
    submit_info.pWaitDstStageMask      = wait_stages;
    submit_info.commandBufferCount     = 1;
    submit_info.pCommandBuffers        = &command_buffers_[current_frame_];

    vk::Semaphore signal_semaphores[]  = {render_finished_semaphores_[current_image_index_]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = signal_semaphores;

    vk_must(
        context_->get_graphics_queue().submit(submit_info, in_flight_fences_[current_frame_]),
        "submit draw command buffers"
    );

    vk::PresentInfoKHR present_info{};
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = signal_semaphores;

    vk::SwapchainKHR swapchains[] = {swapchain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains    = swapchains;
    present_info.pImageIndices  = &current_image_index_;

    const vk::Result result = context_->get_present_queue().presentKHR(&present_info);

    resolve_capture_();

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || framebuffer_resized_) {
        framebuffer_resized_ = false;
        recreate_swapchain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present swap chain image!");
    }

    debug_primitives_.clear();

    stats_.draw_call_count = draw_call_count_;
    draw_call_count_       = 0;

    current_frame_ = (current_frame_ + 1) % frames_in_flight;
    ++frame_counter_;
}

auto renderer::get_present_mode_name() const -> std::string_view {
    switch (present_mode_) {
        case vk::PresentModeKHR::eImmediate: return "immediate";
        case vk::PresentModeKHR::eMailbox: return "mailbox";
        case vk::PresentModeKHR::eFifo: return "fifo";
        case vk::PresentModeKHR::eFifoRelaxed: return "fifo_relaxed";
        default: return "other";
    }
}

auto renderer::get_stats() const -> const renderer_stats& {
    stats_.combined_buffers = combined_buffer_pool_->get_stats();
    return stats_;
}

auto renderer::get_directional_light_settings() -> directional_light_settings& {
    return directional_light_settings_;
}

auto renderer::get_grass_settings() -> grass_settings& {
    return grass_settings_;
}

auto renderer::get_wind_settings() -> wind_settings& {
    return wind_settings_;
}

// см. docs/rendering.md#ветер
auto renderer::wind_push_() const -> vec4f {
    const vec2f dir   = wind_settings_.direction;
    const float32 len = std::max(std::sqrt((dir.x * dir.x) + (dir.y * dir.y)), 0.0001F);
    return vec4f{dir.x / len, dir.y / len, wind_settings_.leaf_sway_voxels, wind_time_ * wind_settings_.speed};
}

auto renderer::get_grass_stats() const -> const grass_stats& {
    return grass_->get_stats();
}

auto renderer::get_fog_settings() -> fog_settings& {
    return fog_settings_;
}

auto renderer::get_tonemap_settings() -> tonemap_settings& {
    return tonemap_settings_;
}

auto renderer::get_bloom_settings() -> bloom_settings& {
    return bloom_settings_;
}

auto renderer::tonemap_push_() const -> vec4f {
    return {tonemap_settings_.exposure, std::max(tonemap_settings_.white_point, 0.01f), 0.0f, 0.0f};
}

auto renderer::get_block_light_settings() -> block_light_settings& {
    return block_light_settings_;
}

auto renderer::get_max_visible_lights() -> uint32& {
    return light_buffer_->get_max_visible();
}

auto renderer::get_cluster_settings() -> cluster_settings& {
    return cluster_settings_;
}

auto renderer::set_cluster_readback(cluster_readback_level level) -> void {
    light_grid_->set_readback(level);
}

auto renderer::take_cluster_readback(cull_list kind) -> std::optional<cluster_readback> {
    return light_grid_->take_readback(kind);
}

auto renderer::get_cluster_grid(
    const camera& camera
) const -> spatial::cluster_grid {
    const mat4f projection = camera.get_projection_matrix();

    const float32 far_depth = fog_settings_.enabled
                                  ? fog_settings_.far_distance
                                  : camera.get_far();

    return spatial::cluster_grid{
        .screen_width  = swapchain_extent_.width,
        .screen_height = swapchain_extent_.height,
        .tile_size     = cluster_settings_.tile_size,
        .slices        = cluster_settings_.slices,
        .near_depth    = camera.get_near(),
        .far_depth     = std::max(far_depth, camera.get_near() * 2.0f),
        .proj_x        = projection[0, 0],
        .proj_y        = projection[1, 1],
        .orthographic  = camera.is_orthographic(),
    };
}

auto renderer::get_visible_light_count() const -> uint32 {
    return light_buffer_ ? light_buffer_->get_lights_count() : 0;
}

auto renderer::get_ambient_settings() -> ambient_settings& {
    return ambient_settings_;
}

auto renderer::get_shadow_settings() -> shadow_settings& {
    return shadow_map_->get_settings();
}

auto renderer::set_debug_view(
    debug_view view
) -> void {
    debug_view_ = view;
}

auto renderer::get_debug_view() const -> debug_view {
    return debug_view_;
}

auto renderer::get_cascade_splits() const
    -> const std::array<float32, shadow_map::cascade_count>& {
    return shadow_map_->get_cascade_splits();
}

auto renderer::get_cascade_texel_sizes() const
    -> const std::array<float32, shadow_map::cascade_count>& {
    return shadow_map_->get_cascade_texel_sizes();
}

auto renderer::draw_colliders(
    world& w, color col
) -> void {
    for (auto [ent, box, tc] :
         w.registry().view<box_collider_component, transform_component>()) {
        auto pos  = tc.get_position() + box.get_offset();
        auto half = box.get_extents() * 0.5f;
        draw_box(pos - half, box.get_extents(), col);
    }
}

auto renderer::set_clear_color(
    float r, float g, float b, float a
) -> void {
    clear_color_ = {r, g, b, a};
}

auto renderer::set_clear_color(
    vec4f color
) -> void {
    clear_color_ = color;
}

auto renderer::wait_idle() const -> void {
    vk_must(context_->get_device().waitIdle(), "wait for device idle");
}

auto renderer::handle_resize() -> void {
    framebuffer_resized_ = true;
}

auto renderer::has_drawable_surface() const -> bool {
    const vec2i size = window_->framebuffer_size();
    return size.x > 0 && size.y > 0;
}

auto renderer::set_render_mode(
    render_mode mode
) -> void {
    current_render_mode_ = mode;
}

auto renderer::get_render_mode() const -> render_mode {
    return current_render_mode_;
}

auto renderer::track(world_type& world) -> void {
    const auto& changed = world.changed<model_component>();
    changed_model_entities_.insert(changed_model_entities_.end(), changed.begin(), changed.end());
    combined_buffer_pool_->track(world);
}

auto renderer::sync_meshes_(world_type& world) -> void {
    mesh_pool_.process_completed(combined_buffer_pool_->mesh_write_budget());

    auto& registry = world.registry();

    for (auto it = pending_mesh_entities_.begin(); it != pending_mesh_entities_.end();) {
        auto ent = *it;
        if (!registry.has<model_component>(ent)) {
            it = pending_mesh_entities_.erase(it);
            continue;
        }
        auto& comp = registry.get<model_component>(ent);
        if (!comp.has_model()) {
            it = pending_mesh_entities_.erase(it);
            continue;
        }
        if (mesh_pool_.has(comp.get_identity(), entity_lod_step(comp))) {
            it = pending_mesh_entities_.erase(it);
            combined_buffer_pool_->mark_mesh_ready(ent);
        } else {
            ++it;
        }
    }

    std::sort(changed_model_entities_.begin(), changed_model_entities_.end());
    changed_model_entities_.erase(
        std::unique(changed_model_entities_.begin(), changed_model_entities_.end()),
        changed_model_entities_.end()
    );

    for (auto ent : changed_model_entities_) {
        if (!registry.has<model_component>(ent)) continue;
        auto& comp = registry.get<model_component>(ent);
        if (!comp.has_model()) continue;
        auto identity    = comp.get_identity();
        const int32 step = entity_lod_step(comp);
        if (!mesh_pool_.has(identity, step) && !mesh_pool_.is_pending(identity, step)) {
            mesh_pool_.request_mesh(
                comp.get_model(),
                comp.get_chunk(),
                mesh_options{
                    .build_links = combined_buffer_pool_->is_chunk_cull_enabled(),
                    .lod_step    = step,
                }
            );
            pending_mesh_entities_.insert(ent);
        }
    }
    changed_model_entities_.clear();
}

auto renderer::render(
    world_type& world, camera& camera
) -> void {
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    vk_must(command_buffers_[current_frame_].begin(begin_info), "begin recording command buffer");

    auto cmd = command_buffers_[current_frame_];
    gpu_timer_->reset(cmd, current_frame_);
    gpu_timer_->begin(cmd, gpu_stage::frame);

    stats_.timing.mesh_sync_ms = measure_ms([&] { sync_meshes_(world); });

    stats_.timing.buffer_pool_update_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::buffer_upload);
        combined_buffer_pool_->update(
            world, camera, command_buffers_[current_frame_], mesh_pool_);
        gpu_timer_->end(cmd, gpu_stage::buffer_upload);
    });

    const bool shadows_on = shadow_map_->get_settings().enabled;

    stats_.timing.shadow_map_update_ms = measure_ms([&] {
        if (!shadows_on) {
            return;
        }
        for (const auto& bounds : combined_buffer_pool_->get_touched_bounds()) {
            shadow_map_->invalidate(bounds);
        }
        shadow_map_->update(camera, directional_light_settings_.direction);
    });

    const auto& cascade_frustums = shadow_map_->get_cascade_frustums();

    stats_.timing.light_gather_ms = measure_ms([&] {
        light_buffer_->update(
            world, camera.get_frustum(), camera.get_position(), current_frame_
        );
        blob_buffer_->update(world, current_frame_);
    });

    stats_.timing.grass_prepare_ms = measure_ms([&] {
        wind_time_ = std::chrono::duration<float32>(std::chrono::steady_clock::now() - wind_start_).count();
        grass_->prepare(world, camera, grass_settings_, wind_settings_, wind_time_, current_frame_);
    });

    stats_.timing.light_cull_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::light_cull);

        light_grid_->set_grid(
            get_cluster_grid(camera), cluster_settings_.cap, cluster_settings_.blob_cap
        );
        light_grid_->dispatch(
            cmd, light_buffer_->get_descriptor_set(current_frame_), camera.get_view_matrix(),
            light_buffer_->get_lights(), blob_buffer_->get_blobs(), current_frame_
        );

        gpu_timer_->end(cmd, gpu_stage::light_cull);
    });

    stats_.timing.compute_cull_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::compute_cull);
        {
            vk::MemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask =                    //
                vk::AccessFlagBits::eVertexAttributeRead |  //
                vk::AccessFlagBits::eIndexRead |             //
                vk::AccessFlagBits::eIndirectCommandRead |  //
                vk::AccessFlagBits::eShaderRead;
            constexpr auto stage_mask =                //
                vk::PipelineStageFlagBits::eVertexInput |   //
                vk::PipelineStageFlagBits::eDrawIndirect |  //
                vk::PipelineStageFlagBits::eVertexShader |  //
                vk::PipelineStageFlagBits::eComputeShader;
            command_buffers_[current_frame_].pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                stage_mask,
                {},
                barrier,
                nullptr,
                nullptr
            );
        }

        const vw::spatial::frustum& view_frustum = camera.get_frustum();
        const std::span<const vw::spatial::frustum> cull_cascades =
            shadows_on ? std::span<const vw::spatial::frustum>{cascade_frustums}
                       : std::span<const vw::spatial::frustum>{};

        cull_pipeline_->update_frustums(
            current_frame_, view_frustum, cull_cascades, camera.culling_eye());

        cull_pipeline_->dispatch(
            command_buffers_[current_frame_],
            combined_buffer_pool_->get_buffers(),
            current_frame_
        );

        {
            vk::MemoryBarrier compute_barrier{};
            compute_barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            compute_barrier.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
            command_buffers_[current_frame_].pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eDrawIndirect,
                {},
                compute_barrier,
                nullptr,
                nullptr
            );
        }

        gpu_timer_->end(cmd, gpu_stage::compute_cull);
    });

    stats_.timing.shadow_pass_ms = measure_ms([&] {
        if (!shadows_on) {
            return;
        }
        gpu_timer_->begin(cmd, gpu_stage::shadow_pass);
        render_shadow_pass(world, camera);
        gpu_timer_->end(cmd, gpu_stage::shadow_pass);
    });

    stats_.timing.world_pass_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::world_pass);
        render_world_pass(world, camera);
        gpu_timer_->end(cmd, gpu_stage::world_pass);
    });

    render_post_();

    gpu_timer_->end(cmd, gpu_stage::frame);

    record_capture_();

    vk_must(command_buffers_[current_frame_].end(), "record command buffer");
}

auto renderer::has_pending_meshes() const -> bool {
    return !pending_mesh_entities_.empty() || mesh_pool_.get_pending_count() > 0;
}

auto renderer::request_capture(
    const frame_capture_request& request
) -> bool {
    if (!capture_supported_ || !has_drawable_surface()) {
        return false;
    }

    capture_request_ = request;
    captured_.reset();
    return true;
}

auto renderer::take_capture() -> std::optional<image_rgba> {
    return std::exchange(captured_, std::nullopt);
}

auto renderer::draws_interface_() const -> bool {
    return !capture_request_ || capture_request_->with_interface;
}

auto renderer::draws_overlays_() const -> bool {
    return !capture_request_ || capture_request_->with_overlays;
}

auto renderer::record_capture_() -> void {
    if (!capture_request_) {
        return;
    }

    const auto cmd         = command_buffers_[current_frame_];
    const vk::Image shown  = swapchain_images_[current_image_index_];
    const vk::DeviceSize bytes =
        static_cast<vk::DeviceSize>(swapchain_extent_.width) * swapchain_extent_.height *
        image_rgba::channels;

    if (!capture_buffer_ || capture_buffer_->get_size() < bytes) {
        capture_buffer_ = std::make_unique<buffer>(
            *context_, bytes, vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
    }

    vk::ImageSubresourceRange whole_image{};
    whole_image.aspectMask = vk::ImageAspectFlagBits::eColor;
    whole_image.levelCount = 1;
    whole_image.layerCount = 1;

    vk::ImageMemoryBarrier to_source{};
    to_source.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    to_source.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
    to_source.oldLayout           = vk::ImageLayout::ePresentSrcKHR;
    to_source.newLayout           = vk::ImageLayout::eTransferSrcOptimal;
    to_source.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_source.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_source.image               = shown;
    to_source.subresourceRange    = whole_image;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
        {}, nullptr, nullptr, to_source
    );

    vk::BufferImageCopy region{};
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = vk::Extent3D{swapchain_extent_.width, swapchain_extent_.height, 1};

    cmd.copyImageToBuffer(
        shown, vk::ImageLayout::eTransferSrcOptimal, capture_buffer_->get_buffer(), region
    );

    vk::ImageMemoryBarrier to_present{};
    to_present.srcAccessMask       = vk::AccessFlagBits::eTransferRead;
    to_present.dstAccessMask       = {};
    to_present.oldLayout           = vk::ImageLayout::eTransferSrcOptimal;
    to_present.newLayout           = vk::ImageLayout::ePresentSrcKHR;
    to_present.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_present.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    to_present.image               = shown;
    to_present.subresourceRange    = whole_image;

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {},
        nullptr, nullptr, to_present
    );

    capture_extent_   = swapchain_extent_;
    capture_recorded_ = true;
}

auto renderer::resolve_capture_() -> void {
    if (!capture_recorded_) {
        return;
    }
    capture_recorded_ = false;
    capture_request_.reset();

    vk_must(
        context_->get_device().waitForFences(
            in_flight_fences_[current_frame_], vk::True, std::numeric_limits<uint64>::max()
        ),
        "wait for the captured frame"
    );

    image_rgba frame{
        .width  = capture_extent_.width,
        .height = capture_extent_.height,
        .pixels = {},
    };
    frame.pixels.resize(
        static_cast<std::size_t>(frame.width) * frame.height * image_rgba::channels
    );
    capture_buffer_->copy_to(frame.pixels.data(), frame.pixels.size());
    capture_buffer_->unmap();

    const bool blue_first = swapchain_image_format_ == vk::Format::eB8G8R8A8Srgb ||
        swapchain_image_format_ == vk::Format::eB8G8R8A8Unorm;

    for (std::size_t at = 0; at < frame.pixels.size(); at += image_rgba::channels) {
        if (blue_first) {
            std::swap(frame.pixels[at], frame.pixels[at + 2]);
        }
        frame.pixels[at + 3] = 255;
    }

    captured_ = std::move(frame);
}

auto renderer::draw_line(
    const vec3f& a, const vec3f& b, color col
) -> void {
    debug_primitives_.add_line(a, b, col);
}

auto renderer::draw_triangle(
    const vec3f& a, const vec3f& b, const vec3f& c, color col
) -> void {
    debug_primitives_.add_triangle(a, b, c, col);
}

auto renderer::draw_quad(
    const vec3f& a, const vec3f& b, const vec3f& c, const vec3f& d, color col
) -> void {
    debug_primitives_.add_quad(a, b, c, d, col);
}

auto renderer::draw_box(
    const mat4f& matrix, const vec3f& size, const color col
) -> void {
    debug_primitives_.add_box(matrix, size, col);
}

auto renderer::draw_box(
    const transform& transform, const vec3f& size, color col
) -> void {
    debug_primitives_.add_box(transform, size, col);
}

auto renderer::draw_box(
    const vec3f& position, const vec3f& size, color col
) -> void {
    debug_primitives_.add_box(position, size, col);
}

auto renderer::draw_grid(
    const mat4f& matrix, float cell_size, int cols, int rows, color clr
) -> void {
    debug_primitives_.add_grid(matrix, cell_size, cols, rows, clr);
}

auto renderer::draw_grid(
    const transform& transform, float cell_size, int cols, int rows, color clr
) -> void {
    debug_primitives_.add_grid(transform, cell_size, cols, rows, clr);
}

auto renderer::draw_grid(
    const vec3f& position, float cell_size, int cols, int rows, color clr
) -> void {
    debug_primitives_.add_grid(position, cell_size, cols, rows, clr);
}

auto renderer::create_swapchain() -> void {
    auto swapchain_support = context_->query_swapchain_support_();

    vk::SurfaceFormatKHR surface_format = choose_swap_surface_format(swapchain_support.formats);
    vk::Extent2D extent                 = choose_swap_extent(swapchain_support.capabilities);

    present_mode_ = choose_swap_present_mode(swapchain_support.present_modes);

    uint32 image_count = swapchain_support.capabilities.minImageCount + 1;
    if (swapchain_support.capabilities.maxImageCount > 0 &&
        image_count > swapchain_support.capabilities.maxImageCount) {
        image_count = swapchain_support.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR create_info{};
    create_info.surface          = context_->get_surface();
    create_info.minImageCount    = image_count;
    create_info.imageFormat      = surface_format.format;
    create_info.imageColorSpace  = surface_format.colorSpace;
    create_info.imageExtent      = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage       = vk::ImageUsageFlagBits::eColorAttachment;

    const bool can_copy_out = static_cast<bool>(
        swapchain_support.capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc
    );
    const bool known_layout = surface_format.format == vk::Format::eB8G8R8A8Srgb ||
        surface_format.format == vk::Format::eB8G8R8A8Unorm ||
        surface_format.format == vk::Format::eR8G8B8A8Srgb ||
        surface_format.format == vk::Format::eR8G8B8A8Unorm;

    capture_supported_ = can_copy_out && known_layout;
    if (capture_supported_) {
        create_info.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    create_info.imageSharingMode = vk::SharingMode::eExclusive;
    create_info.preTransform     = swapchain_support.capabilities.currentTransform;
    create_info.compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    create_info.presentMode      = present_mode_;
    create_info.clipped          = vk::True;
    create_info.oldSwapchain     = nullptr;

    auto queue_families = context_->get_queue_families();
    if (queue_families.graphics_family != queue_families.present_family) {
        uint32 queue_family_indices[] = {
            queue_families.graphics_family.value(), queue_families.present_family.value()
        };
        create_info.imageSharingMode      = vk::SharingMode::eConcurrent;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = queue_family_indices;
    }

    swapchain_ = vk_must(context_->get_device().createSwapchainKHR(create_info), "failed to create swap chain");

    swapchain_images_ =
        vk_must(context_->get_device().getSwapchainImagesKHR(swapchain_), "get swapchain images");

    swapchain_image_format_ = surface_format.format;
    swapchain_extent_       = extent;
}

auto renderer::create_image_views() -> void {
    swapchain_image_views_.resize(swapchain_images_.size());

    for (std::size_t i = 0; i < swapchain_images_.size(); i++) {
        vk::ImageViewCreateInfo view_info{};
        view_info.image                           = swapchain_images_[i];
        view_info.viewType                        = vk::ImageViewType::e2D;
        view_info.format                          = swapchain_image_format_;
        view_info.components.r                    = vk::ComponentSwizzle::eIdentity;
        view_info.components.g                    = vk::ComponentSwizzle::eIdentity;
        view_info.components.b                    = vk::ComponentSwizzle::eIdentity;
        view_info.components.a                    = vk::ComponentSwizzle::eIdentity;
        view_info.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount     = 1;

        swapchain_image_views_[i] = vk_must(context_->get_device().createImageView(view_info), "failed to create image views");
    }
}

auto renderer::choose_msaa_samples() -> void {
    const vk::PhysicalDeviceProperties properties = context_->get_physical_device().getProperties();

    const vk::SampleCountFlags supported = properties.limits.framebufferColorSampleCounts &
                                           properties.limits.framebufferDepthSampleCounts;

    constexpr std::array<vk::SampleCountFlagBits, 4> ladder{
        vk::SampleCountFlagBits::e8,
        vk::SampleCountFlagBits::e4,
        vk::SampleCountFlagBits::e2,
        vk::SampleCountFlagBits::e1,
    };

    msaa_samples_ = vk::SampleCountFlagBits::e1;
    for (vk::SampleCountFlagBits step : ladder) {
        if (static_cast<uint32>(step) <= wanted_msaa_samples_ && (supported & step)) {
            msaa_samples_ = step;
            break;
        }
    }
}

auto renderer::create_color_resources() -> void {
    create_image(
        scene_image_,
        scene_image_memory_,
        swapchain_extent_,
        post_process::scene_format,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    scene_image_view_ =
        create_image_view(scene_image_, post_process::scene_format, vk::ImageAspectFlagBits::eColor);

    if (msaa_samples_ == vk::SampleCountFlagBits::e1) {
        return;
    }

    create_image(
        color_image_,
        color_image_memory_,
        swapchain_extent_,
        post_process::scene_format,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        msaa_samples_
    );
    color_image_view_ =
        create_image_view(color_image_, post_process::scene_format, vk::ImageAspectFlagBits::eColor);
}

auto renderer::create_depth_resources() -> void {
    vk::Format depth_format = find_depth_format();

    create_image(
        depth_image_,
        depth_image_memory_,
        swapchain_extent_,
        depth_format,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        msaa_samples_
    );
    depth_image_view_ = create_image_view(depth_image_, depth_format, vk::ImageAspectFlagBits::eDepth);
}

auto renderer::create_render_pass() -> void {
    const bool multisampled = msaa_samples_ != vk::SampleCountFlagBits::e1;

    vk::AttachmentDescription color_attachment{};
    color_attachment.format         = post_process::scene_format;
    color_attachment.samples        = msaa_samples_;
    color_attachment.loadOp         = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp =
        multisampled ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout  = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout    = multisampled ? vk::ImageLayout::eColorAttachmentOptimal
                                                   : vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription resolve_attachment{};
    resolve_attachment.format         = post_process::scene_format;
    resolve_attachment.samples        = vk::SampleCountFlagBits::e1;
    resolve_attachment.loadOp         = vk::AttachmentLoadOp::eDontCare;
    resolve_attachment.storeOp        = vk::AttachmentStoreOp::eStore;
    resolve_attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    resolve_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    resolve_attachment.initialLayout  = vk::ImageLayout::eUndefined;
    resolve_attachment.finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference resolve_attachment_ref{};
    resolve_attachment_ref.attachment = 2;
    resolve_attachment_ref.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentDescription depth_attachment{};
    depth_attachment.format         = find_depth_format();
    depth_attachment.samples        = msaa_samples_;
    depth_attachment.loadOp         = vk::AttachmentLoadOp::eClear;
    depth_attachment.storeOp        = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depth_attachment.initialLayout  = vk::ImageLayout::eUndefined;
    depth_attachment.finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass_3d    = {};
    subpass_3d.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
    subpass_3d.colorAttachmentCount    = 1;
    subpass_3d.pColorAttachments       = &color_attachment_ref;
    subpass_3d.pDepthStencilAttachment = &depth_attachment_ref;
    subpass_3d.pResolveAttachments     = nullptr;

    vk::SubpassDescription subpass_debug    = {};
    subpass_debug.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
    subpass_debug.colorAttachmentCount    = 1;
    subpass_debug.pColorAttachments       = &color_attachment_ref;
    subpass_debug.pDepthStencilAttachment = &depth_attachment_ref;
    subpass_debug.pResolveAttachments = multisampled ? &resolve_attachment_ref : nullptr;

    vk::SubpassDescription subpasses[] = {subpass_3d, subpass_debug};

    vk::SubpassDependency dependency_3d = {};
    dependency_3d.srcSubpass          = vk::SubpassExternal;
    dependency_3d.dstSubpass          = 0;
    dependency_3d.srcStageMask        = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                        vk::PipelineStageFlagBits::eFragmentShader;
    dependency_3d.srcAccessMask       = vk::AccessFlagBits::eShaderRead;
    dependency_3d.dstStageMask        = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency_3d.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::SubpassDependency dependency_debug = {};
    dependency_debug.srcSubpass          = 0;
    dependency_debug.dstSubpass          = 1;
    dependency_debug.srcStageMask        = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency_debug.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency_debug.dstStageMask        = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency_debug.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::SubpassDependency dependency_sampled = {};
    dependency_sampled.srcSubpass          = 1;
    dependency_sampled.dstSubpass          = vk::SubpassExternal;
    dependency_sampled.srcStageMask        = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency_sampled.srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;
    dependency_sampled.dstStageMask        = vk::PipelineStageFlagBits::eFragmentShader;
    dependency_sampled.dstAccessMask       = vk::AccessFlagBits::eShaderRead;

    vk::SubpassDependency dependencies[] = {dependency_3d, dependency_debug, dependency_sampled};

    vk::AttachmentDescription attachments[] = {
        color_attachment, depth_attachment, resolve_attachment
    };

    vk::RenderPassCreateInfo render_pass_info{};
    render_pass_info.attachmentCount = multisampled ? 3u : 2u;
    render_pass_info.pAttachments    = attachments;
    render_pass_info.subpassCount    = 2;
    render_pass_info.pSubpasses      = subpasses;
    render_pass_info.dependencyCount = 3;
    render_pass_info.pDependencies   = dependencies;

    render_pass_ = vk_must(context_->get_device().createRenderPass(render_pass_info), "failed to create render pass");
}

auto renderer::create_composite_pass() -> void {
    vk::AttachmentDescription shown{};
    shown.format         = swapchain_image_format_;
    shown.samples        = vk::SampleCountFlagBits::e1;
    shown.loadOp         = vk::AttachmentLoadOp::eDontCare;
    shown.storeOp        = vk::AttachmentStoreOp::eStore;
    shown.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
    shown.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    shown.initialLayout  = vk::ImageLayout::eUndefined;
    shown.finalLayout    = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference shown_ref{};
    shown_ref.attachment = 0;
    shown_ref.layout     = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &shown_ref;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass    = vk::SubpassExternal;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = {};
    dependency.dstStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask =
        vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo pass_info{};
    pass_info.attachmentCount = 1;
    pass_info.pAttachments    = &shown;
    pass_info.subpassCount    = 1;
    pass_info.pSubpasses      = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies   = &dependency;

    composite_pass_ = vk_must(
        context_->get_device().createRenderPass(pass_info), "create composite render pass"
    );
}

auto renderer::create_descriptor_set_layouts() -> void {
    vk::DescriptorSetLayoutBinding ubo_layout_binding{};
    ubo_layout_binding.binding         = 0;
    ubo_layout_binding.descriptorType  = vk::DescriptorType::eUniformBuffer;
    ubo_layout_binding.descriptorCount = 1;
    ubo_layout_binding.stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    ubo_layout_binding.pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutCreateInfo ubo_layout_info{};
    ubo_layout_info.bindingCount = 1;
    ubo_layout_info.pBindings    = &ubo_layout_binding;

    uniform_descriptor_set_layout_ = vk_must(context_->get_device().createDescriptorSetLayout(ubo_layout_info), "failed to create uniform descriptor set layout");

    std::array<vk::DescriptorSetLayoutBinding, 3> storage_layout_bindings{};
    storage_layout_bindings[0].binding            = 0;
    storage_layout_bindings[0].descriptorType     = vk::DescriptorType::eStorageBuffer;
    storage_layout_bindings[0].descriptorCount    = 1;
    storage_layout_bindings[0].stageFlags         = vk::ShaderStageFlagBits::eVertex;
    storage_layout_bindings[0].pImmutableSamplers = nullptr;

    storage_layout_bindings[1].binding            = 1;
    storage_layout_bindings[1].descriptorType     = vk::DescriptorType::eStorageBuffer;
    storage_layout_bindings[1].descriptorCount    = 1;
    storage_layout_bindings[1].stageFlags         = vk::ShaderStageFlagBits::eVertex;
    storage_layout_bindings[1].pImmutableSamplers = nullptr;

    storage_layout_bindings[2].binding            = 2;
    storage_layout_bindings[2].descriptorType     = vk::DescriptorType::eStorageBuffer;
    storage_layout_bindings[2].descriptorCount    = 1;
    storage_layout_bindings[2].stageFlags         = vk::ShaderStageFlagBits::eVertex;
    storage_layout_bindings[2].pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutCreateInfo storage_layout_info{};
    storage_layout_info.bindingCount = static_cast<uint32>(storage_layout_bindings.size());
    storage_layout_info.pBindings    = storage_layout_bindings.data();

    storage_descriptor_set_layout_ = vk_must(context_->get_device().createDescriptorSetLayout(storage_layout_info), "failed to create storage descriptor set layout");

    vk::DescriptorSetLayoutBinding shadow_layout_binding{};
    shadow_layout_binding.binding            = 0;
    shadow_layout_binding.descriptorType     = vk::DescriptorType::eCombinedImageSampler;
    shadow_layout_binding.descriptorCount    = 1;
    shadow_layout_binding.stageFlags         = vk::ShaderStageFlagBits::eFragment;
    shadow_layout_binding.pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutCreateInfo shadow_layout_info{};
    shadow_layout_info.bindingCount = 1;
    shadow_layout_info.pBindings    = &shadow_layout_binding;

    shadow_descriptor_set_layout_ = vk_must(context_->get_device().createDescriptorSetLayout(shadow_layout_info), "failed to create shadow descriptor set layout");
}

auto renderer::create_graphics_pipeline() -> void {
    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        vertex_shader_->get_stage_info(), fragment_shader_->get_stage_info()
    };

    auto binding_description    = quad::get_binding_descriptions();
    auto attribute_descriptions = quad::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.vertexBindingDescriptionCount =
        static_cast<uint32>(binding_description.size());
    vertex_input_info.pVertexBindingDescriptions = binding_description.data();
    vertex_input_info.vertexAttributeDescriptionCount =
        static_cast<uint32>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = vk::False;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr;

    vk::DynamicState dynamic_states[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable        = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode             = vk::PolygonMode::eFill;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable         = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable  = vk::False;
    multisampling.rasterizationSamples = msaa_samples_;

    vk::PipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    color_blend_attachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.logicOpEnable   = vk::False;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable  = vk::True;
    depth_stencil.depthWriteEnable = vk::True;
    depth_stencil.depthCompareOp   = vk::CompareOp::eGreater;
    depth_stencil.depthBoundsTestEnable = vk::False;
    depth_stencil.stencilTestEnable     = vk::False;

    std::array<vk::DescriptorSetLayout, 5> descriptor_set_layouts = {
        uniform_descriptor_set_layout_,
        storage_descriptor_set_layout_,
        shadow_descriptor_set_layout_,
        point_lights_descriptor_set_layout_,
        palette_descriptor_set_layout_
    };

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    vk::PushConstantRange world_push_range{};
    world_push_range.offset     = 0;
    world_push_range.size       = sizeof(world_push_constant_data);
    world_push_range.stageFlags = vk::ShaderStageFlagBits::eVertex;

    pipeline_layout_info.setLayoutCount = static_cast<uint32>(descriptor_set_layouts.size());
    pipeline_layout_info.pSetLayouts    = descriptor_set_layouts.data();
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges    = &world_push_range;

    pipeline_layout_ = vk_must(context_->get_device().createPipelineLayout(pipeline_layout_info), "failed to create pipeline layout");

    vk::GraphicsPipelineCreateInfo pipeline_info{};

    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline_layout_;
    pipeline_info.renderPass          = render_pass_;
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = nullptr;

    graphics_pipeline_ =
        vk_must(context_->get_device().createGraphicsPipeline(nullptr, pipeline_info), "create graphics pipeline");
}
auto renderer::create_wireframe_pipeline() -> void {
    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        vertex_shader_->get_stage_info(), fragment_shader_->get_stage_info()
    };

    auto binding_description    = quad::get_binding_descriptions();
    auto attribute_descriptions = quad::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.vertexBindingDescriptionCount =
        static_cast<uint32>(binding_description.size());
    vertex_input_info.pVertexBindingDescriptions = binding_description.data();
    vertex_input_info.vertexAttributeDescriptionCount =
        static_cast<uint32>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = vk::False;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr;

    vk::DynamicState dynamic_states[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable        = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode             = vk::PolygonMode::eLine;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable         = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable  = vk::False;
    multisampling.rasterizationSamples = msaa_samples_;

    vk::PipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    color_blend_attachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.logicOpEnable   = vk::False;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable  = vk::True;
    depth_stencil.depthWriteEnable = vk::True;
    depth_stencil.depthCompareOp   = vk::CompareOp::eGreater;
    depth_stencil.depthBoundsTestEnable = vk::False;
    depth_stencil.stencilTestEnable     = vk::False;

    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline_layout_;
    pipeline_info.renderPass          = render_pass_;
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = nullptr;

    wireframe_pipeline_ =
        vk_must(context_->get_device().createGraphicsPipeline(nullptr, pipeline_info), "create wireframe pipeline");
}

auto renderer::create_debug_pipeline() -> void {
    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        debug_vertex_shader_->get_stage_info(), debug_fragment_shader_->get_stage_info()
    };

    auto binding_description    = debug_vertex::get_binding_descriptions();
    auto attribute_descriptions = debug_vertex::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.vertexBindingDescriptionCount =
        static_cast<uint32>(binding_description.size());
    vertex_input_info.pVertexBindingDescriptions = binding_description.data();
    vertex_input_info.vertexAttributeDescriptionCount =
        static_cast<uint32>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eLineList;
    input_assembly.primitiveRestartEnable = vk::False;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr;

    vk::DynamicState dynamic_states[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable        = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode             = vk::PolygonMode::eFill;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable         = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable  = vk::False;
    multisampling.rasterizationSamples = msaa_samples_;

    vk::PipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
    color_blend_attachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.logicOpEnable   = vk::False;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable  = vk::True;
    depth_stencil.depthWriteEnable = vk::False;
    depth_stencil.depthCompareOp   = vk::CompareOp::eGreater;
    depth_stencil.depthBoundsTestEnable = vk::False;
    depth_stencil.stencilTestEnable     = vk::False;


    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.setLayoutCount         = 1;
    vk::PushConstantRange tonemap_range{};
    tonemap_range.offset     = 0;
    tonemap_range.size       = sizeof(vec4f);
    tonemap_range.stageFlags = vk::ShaderStageFlagBits::eFragment;

    pipeline_layout_info.pSetLayouts            = &uniform_descriptor_set_layout_;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges    = &tonemap_range;

    debug_pipeline_layout_ = vk_must(context_->get_device().createPipelineLayout(pipeline_layout_info), "failed to create debug pipeline layout");

    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = debug_pipeline_layout_;
    pipeline_info.renderPass          = render_pass_;
    pipeline_info.subpass             = 1;
    pipeline_info.basePipelineHandle  = nullptr;

    debug_pipeline_ =
        vk_must(context_->get_device().createGraphicsPipeline(nullptr, pipeline_info), "create debug pipeline");

    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    rasterizer.cullMode     = vk::CullModeFlagBits::eNone;

    depth_stencil.depthTestEnable = vk::False;

    color_blend_attachment.blendEnable         = vk::True;
    color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
    color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;

    debug_solid_pipeline_ = vk_must(
        context_->get_device().createGraphicsPipeline(nullptr, pipeline_info),
        "create debug solid pipeline"
    );
}

auto renderer::create_framebuffers() -> void {
    framebuffers_.resize(swapchain_image_views_.size());

    const bool multisampled = msaa_samples_ != vk::SampleCountFlagBits::e1;

    vk::ImageView scene_attachments[] = {
        multisampled ? color_image_view_ : scene_image_view_,
        depth_image_view_,
        scene_image_view_,
    };

    vk::FramebufferCreateInfo scene_info{};
    scene_info.renderPass      = render_pass_;
    scene_info.attachmentCount = multisampled ? 3u : 2u;
    scene_info.pAttachments    = scene_attachments;
    scene_info.width           = swapchain_extent_.width;
    scene_info.height          = swapchain_extent_.height;
    scene_info.layers          = 1;

    scene_framebuffer_ =
        vk_must(context_->get_device().createFramebuffer(scene_info), "create scene framebuffer");

    for (std::size_t i = 0; i < swapchain_image_views_.size(); i++) {
        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.renderPass      = composite_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments    = &swapchain_image_views_[i];
        framebuffer_info.width           = swapchain_extent_.width;
        framebuffer_info.height          = swapchain_extent_.height;
        framebuffer_info.layers          = 1;

        framebuffers_[i] = vk_must(context_->get_device().createFramebuffer(framebuffer_info), "failed to create framebuffer");
    }
}

auto renderer::create_command_buffers() -> void {
    command_buffers_.resize(frames_in_flight);

    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.commandPool        = context_->get_command_pool();
    alloc_info.level              = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = static_cast<uint32>(command_buffers_.size());

    command_buffers_ =
        vk_must(context_->get_device().allocateCommandBuffers(alloc_info), "allocate command buffers");
}

auto renderer::create_sync_objects() -> void {
    image_available_semaphores_.resize(frames_in_flight);
    render_finished_semaphores_.resize(swapchain_images_.size());
    in_flight_fences_.resize(frames_in_flight);

    vk::SemaphoreCreateInfo semaphore_info{};

    vk::FenceCreateInfo fence_info{};
    fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        image_available_semaphores_[i] = vk_must(context_->get_device().createSemaphore(semaphore_info), "failed to create synchronization objects for a frame");
    }

    for (std::size_t i = 0; i < swapchain_images_.size(); i++) {
        render_finished_semaphores_[i] = vk_must(context_->get_device().createSemaphore(semaphore_info), "failed to create synchronization objects for a frame");
    }

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        in_flight_fences_[i] = vk_must(context_->get_device().createFence(fence_info), "failed to create synchronization objects for a frame");
    }
}

auto renderer::create_uniform_buffers() -> void {
    vk::DeviceSize buffer_size = sizeof(uniform_buffer_object);
    uniform_buffers_.resize(frames_in_flight);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        uniform_buffers_[i] = std::make_unique<uniform_buffer>(*context_, buffer_size);
    }
}

auto renderer::create_descriptor_pool() -> void {
    constexpr uint32 MAX_DESCRIPTOR_SETS  = 512;
    constexpr uint32 STORAGE_BUFFER_COUNT = 1024;

    std::array pool_sizes = {
        vk::DescriptorPoolSize{
            vk::DescriptorType::eUniformBuffer,
            static_cast<uint32>(frames_in_flight * 8)
        },
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, STORAGE_BUFFER_COUNT},
        vk::DescriptorPoolSize{
            vk::DescriptorType::eCombinedImageSampler,
            static_cast<uint32>(frames_in_flight) + post_process::sampled_image_count
        }
    };

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.poolSizeCount = static_cast<uint32>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = MAX_DESCRIPTOR_SETS;

    descriptor_pool_ = vk_must(context_->get_device().createDescriptorPool(pool_info), "failed to create descriptor pool");
}

auto renderer::create_descriptor_sets() -> void {
    std::vector layouts(frames_in_flight, uniform_descriptor_set_layout_);
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = static_cast<uint32>(frames_in_flight);
    alloc_info.pSetLayouts        = layouts.data();

    descriptor_sets_ =
        vk_must(context_->get_device().allocateDescriptorSets(alloc_info), "allocate descriptor sets");

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        vk::DescriptorBufferInfo ubo_buffer_info{};
        ubo_buffer_info.buffer = uniform_buffers_[i]->get_buffer();
        ubo_buffer_info.offset = 0;
        ubo_buffer_info.range  = sizeof(uniform_buffer_object);

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet          = descriptor_sets_[i];
        descriptor_write.dstBinding      = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType  = vk::DescriptorType::eUniformBuffer;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo     = &ubo_buffer_info;

        context_->get_device().updateDescriptorSets(descriptor_write, nullptr);
    }
}

auto renderer::init_imgui() -> void {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    setup_imgui_style();

    constexpr bool install_callbacks = true;
    ImGui_ImplGlfw_InitForVulkan(
        static_cast<GLFWwindow*>(window_->native_handle()), install_callbacks);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion                = VK_API_VERSION_1_2;
    init_info.Instance                  = context_->get_instance();
    init_info.PhysicalDevice            = context_->get_physical_device();
    init_info.Device                    = context_->get_device();
    init_info.QueueFamily               = context_->get_queue_families().graphics_family.value();
    init_info.Queue                     = context_->get_graphics_queue();
    init_info.DescriptorPool            = imgui_descriptor_pool_;
    init_info.MinImageCount             = 2;
    init_info.ImageCount                = static_cast<uint32>(swapchain_images_.size());
    init_info.Allocator                 = nullptr;
    init_info.CheckVkResultFn           = nullptr;

    init_info.PipelineInfoMain.RenderPass  = composite_pass_;
    init_info.PipelineInfoMain.Subpass     = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        throw std::runtime_error("failed to initialize imgui");
    }
}

auto renderer::setup_imgui_style() -> void {
}

auto renderer::create_imgui_descriptor_pool() -> void {
    std::array pool_sizes = {
        vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
        vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000}
    };

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.poolSizeCount = static_cast<uint32>(pool_sizes.size());
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets       = 1000;

    imgui_descriptor_pool_ = vk_must(context_->get_device().createDescriptorPool(pool_info), "failed to create imgui descriptor pool");
}

auto renderer::cleanup_imgui() -> void {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imgui_descriptor_pool_ != nullptr) {
        context_->get_device().destroyDescriptorPool(imgui_descriptor_pool_);
        imgui_descriptor_pool_ = nullptr;
    }
}

auto renderer::cleanup_descriptor_pool() -> void {
    if (descriptor_pool_ != nullptr) {
        context_->get_device().destroyDescriptorPool(descriptor_pool_);
        descriptor_pool_ = nullptr;
    }
}

auto renderer::cleanup_render_pass() -> void {
    if (render_pass_ != nullptr) {
        context_->get_device().destroyRenderPass(render_pass_);
        render_pass_ = nullptr;
    }
    if (composite_pass_ != nullptr) {
        context_->get_device().destroyRenderPass(composite_pass_);
        composite_pass_ = nullptr;
    }
}

auto renderer::cleanup_descriptor_set_layouts() -> void {
    if (storage_descriptor_set_layout_ != nullptr) {
        context_->get_device().destroyDescriptorSetLayout(storage_descriptor_set_layout_);
        storage_descriptor_set_layout_ = nullptr;
    }
    if (shadow_descriptor_set_layout_ != nullptr) {
        context_->get_device().destroyDescriptorSetLayout(shadow_descriptor_set_layout_);
        shadow_descriptor_set_layout_ = nullptr;
    }
    if (uniform_descriptor_set_layout_ != nullptr) {
        context_->get_device().destroyDescriptorSetLayout(uniform_descriptor_set_layout_);
        uniform_descriptor_set_layout_ = nullptr;
    }
}

auto renderer::cleanup_pipelines() -> void {
    if (graphics_pipeline_ != nullptr) {
        context_->get_device().destroyPipeline(graphics_pipeline_);
        graphics_pipeline_ = nullptr;
    }
    if (wireframe_pipeline_ != nullptr) {
        context_->get_device().destroyPipeline(wireframe_pipeline_);
        wireframe_pipeline_ = nullptr;
    }
    if (pipeline_layout_ != nullptr) {
        context_->get_device().destroyPipelineLayout(pipeline_layout_);
        pipeline_layout_ = nullptr;
    }
}

auto renderer::cleanup_shadow_pipeline() -> void {
    if (shadow_pipeline_ != nullptr) {
        context_->get_device().destroyPipeline(shadow_pipeline_);
        shadow_pipeline_ = nullptr;
    }
    if (shadow_pipeline_layout_ != nullptr) {
        context_->get_device().destroyPipelineLayout(shadow_pipeline_layout_);
        shadow_pipeline_layout_ = nullptr;
    }
}

auto renderer::cleanup_debug_pipeline() -> void {
    if (debug_solid_pipeline_ != nullptr) {
        context_->get_device().destroyPipeline(debug_solid_pipeline_);
        debug_solid_pipeline_ = nullptr;
    }
    if (debug_pipeline_ != nullptr) {
        context_->get_device().destroyPipeline(debug_pipeline_);
        debug_pipeline_ = nullptr;
    }
    if (debug_pipeline_layout_ != nullptr) {
        context_->get_device().destroyPipelineLayout(debug_pipeline_layout_);
        debug_pipeline_layout_ = nullptr;
    }
}

auto renderer::cleanup_swapchain() -> void {
    for (auto framebuffer : framebuffers_) {
        context_->get_device().destroyFramebuffer(framebuffer);
    }
    context_->get_device().destroyFramebuffer(scene_framebuffer_);
    scene_framebuffer_ = nullptr;

    for (auto image_view : swapchain_image_views_) {
        context_->get_device().destroyImageView(image_view);
    }

    context_->get_device().destroySwapchainKHR(swapchain_);

    for (auto semaphore : image_available_semaphores_) {
        context_->get_device().destroySemaphore(semaphore);
    }
    for (auto semaphore : render_finished_semaphores_) {
        context_->get_device().destroySemaphore(semaphore);
    }

    for (auto fence : in_flight_fences_) {
        context_->get_device().destroyFence(fence);
    }
}

auto renderer::cleanup_color_resources() -> void {
    if (scene_image_view_ != nullptr) {
        context_->get_device().destroyImageView(scene_image_view_);
        scene_image_view_ = nullptr;
    }
    if (scene_image_ != nullptr) {
        context_->get_device().destroyImage(scene_image_);
        scene_image_ = nullptr;
    }
    if (scene_image_memory_ != nullptr) {
        context_->get_device().freeMemory(scene_image_memory_);
        scene_image_memory_ = nullptr;
    }
    if (color_image_view_ != nullptr) {
        context_->get_device().destroyImageView(color_image_view_);
        color_image_view_ = nullptr;
    }
    if (color_image_ != nullptr) {
        context_->get_device().destroyImage(color_image_);
        color_image_ = nullptr;
    }
    if (color_image_memory_ != nullptr) {
        context_->get_device().freeMemory(color_image_memory_);
        color_image_memory_ = nullptr;
    }
}

auto renderer::cleanup_depth_resources() -> void {
    if (depth_image_view_ != nullptr) {
        context_->get_device().destroyImageView(depth_image_view_);
        depth_image_view_ = nullptr;
    }
    if (depth_image_ != nullptr) {
        context_->get_device().destroyImage(depth_image_);
        depth_image_ = nullptr;
    }
    if (depth_image_memory_ != nullptr) {
        context_->get_device().freeMemory(depth_image_memory_);
        depth_image_memory_ = nullptr;
    }
}

auto renderer::recreate_swapchain() -> void {
    swapchain_stale_ = !has_drawable_surface();
    if (swapchain_stale_) {
        return;
    }

    wait_idle();

    cleanup_swapchain();
    cleanup_color_resources();
    cleanup_depth_resources();

    create_swapchain();
    create_image_views();
    create_color_resources();
    create_depth_resources();
    create_framebuffers();
    create_sync_objects();

    post_process_->resize(swapchain_extent_, scene_image_view_);

    current_frame_       = 0;
    current_image_index_ = 0;
}

auto renderer::render_world_pass(
    world_type& world, const camera& camera
) -> void {
    stats_.timing.world_pass_uniform_ms =
        measure_ms([&] { update_uniform_buffer(world, camera); });

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.renderPass        = render_pass_;
    render_pass_info.framebuffer       = scene_framebuffer_;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = swapchain_extent_;

    const vec3f sky = scene_from_display(
        vec3f{clear_color_.x, clear_color_.y, clear_color_.z}, tonemap_settings_
    );
    const vec4f scene_clear{sky.x, sky.y, sky.z, 0.0f};

    vk::ClearValue clear_values[2]{};
    memcpy(&clear_values[0].color, &scene_clear, sizeof(vec4f));
    clear_values[1].depthStencil = {0.0f, 0};

    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues    = clear_values;

    command_buffers_[current_frame_].beginRenderPass(render_pass_info, vk::SubpassContents::eInline);

    cover_swapchain_();

    auto cmd = command_buffers_[current_frame_];

    stats_.timing.world_pass_geometry_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::world_geometry);
        render_world(world, camera);
        gpu_timer_->end(cmd, gpu_stage::world_geometry);
    });

    command_buffers_[current_frame_].nextSubpass(vk::SubpassContents::eInline);

    stats_.timing.world_pass_debug_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::world_debug);
        if (draws_overlays_()) {
            render_debug_primitives();
            render_debug_solids();
        }
        gpu_timer_->end(cmd, gpu_stage::world_debug);
    });

    command_buffers_[current_frame_].endRenderPass();
}

auto renderer::cover_swapchain_() -> void {
    vk::Viewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(swapchain_extent_.width);
    viewport.height   = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    command_buffers_[current_frame_].setViewport(0, viewport);

    vk::Rect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent_;

    command_buffers_[current_frame_].setScissor(0, scissor);
}

auto renderer::render_post_() -> void {
    const auto cmd = command_buffers_[current_frame_];

    gpu_timer_->begin(cmd, gpu_stage::bloom);
    if (bloom_settings_.enabled) {
        post_process_->record_bloom(cmd);
    }
    gpu_timer_->end(cmd, gpu_stage::bloom);

    vk::RenderPassBeginInfo pass_info{};
    pass_info.renderPass        = composite_pass_;
    pass_info.framebuffer       = framebuffers_[current_image_index_];
    pass_info.renderArea.offset = {0, 0};
    pass_info.renderArea.extent = swapchain_extent_;

    cmd.beginRenderPass(pass_info, vk::SubpassContents::eInline);
    cover_swapchain_();

    gpu_timer_->begin(cmd, gpu_stage::composite);
    post_process_->draw_composite(cmd, tonemap_settings_, bloom_settings_);
    gpu_timer_->end(cmd, gpu_stage::composite);

    stats_.timing.world_pass_imgui_ms = measure_ms([&] {
        gpu_timer_->begin(cmd, gpu_stage::world_imgui);
        render_imgui();
        gpu_timer_->end(cmd, gpu_stage::world_imgui);
    });

    cmd.endRenderPass();
}

auto renderer::render_world(
    [[maybe_unused]] world_type& world, [[maybe_unused]] const camera& camera
) -> void {
    vk::Pipeline current_pipeline =
        (current_render_mode_ == render_mode::lit) ? graphics_pipeline_ : wireframe_pipeline_;
    command_buffers_[current_frame_].bindPipeline(vk::PipelineBindPoint::eGraphics, current_pipeline);

    const world_push_constant_data world_push{.wind = wind_push_()};
    command_buffers_[current_frame_].pushConstants<world_push_constant_data>(
        pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, world_push
    );

    command_buffers_[current_frame_].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipeline_layout_,
        0,
        descriptor_sets_[current_frame_],
        nullptr
    );

    command_buffers_[current_frame_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipeline_layout_,
        2,
        1,
        &shadow_map_descriptor_sets_[current_frame_],
        0,
        nullptr);

    vk::DescriptorSet point_lights_descriptor_set =
        light_buffer_->get_descriptor_set(current_frame_);
    command_buffers_[current_frame_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipeline_layout_,
        3,
        1,
        &point_lights_descriptor_set,
        0,
        nullptr);

    vk::DescriptorSet palette_ds = palette_buffer_->get_descriptor_set();
    command_buffers_[current_frame_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipeline_layout_,
        4,
        1,
        &palette_ds,
        0,
        nullptr);

    const auto& buffers = combined_buffer_pool_->get_buffers();
    for (const auto& buffer : buffers) {
        if (buffer->is_empty()) {
            continue;
        }

        vk::Buffer instance_index_buffer = buffer->get_instance_index_buffer();

        vk::DescriptorSet buffer_descriptor_set = buffer->get_descriptor_set(current_frame_);
        command_buffers_[current_frame_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
            pipeline_layout_,
            1,
            1,
            &buffer_descriptor_set,
            0,
            nullptr);

        constexpr vk::DeviceSize instance_offset = 0;
        command_buffers_[current_frame_].bindVertexBuffers(
            0, instance_index_buffer, instance_offset);
        command_buffers_[current_frame_].bindIndexBuffer(
            combined_buffer_pool_->get_index_buffer(), 0, vk::IndexType::eUint32);

        const uint32 max_draws = buffer->get_draw_command_count();
        if (max_draws > 0) {
            command_buffers_[current_frame_].drawIndexedIndirectCount(buffer->get_culled_indirect_buffer(),
                0,
                buffer->get_count_buffer(),
                0,
                max_draws,
                sizeof(draw_command));
            draw_call_count_++;
        }
    }

    if (current_render_mode_ == render_mode::lit) {
        grass_->draw(
            command_buffers_[current_frame_], current_frame_,
            grass_bound_sets{
                .uniform = descriptor_sets_[current_frame_],
                .shadow  = shadow_map_descriptor_sets_[current_frame_],
                .lights  = point_lights_descriptor_set,
                .palette = palette_ds,
            }
        );
        draw_call_count_ += grass_->get_stats().draws;
    }
}

auto renderer::update_uniform_buffer(
    [[maybe_unused]] world_type& world, const camera& camera
) const -> void {
    uniform_buffer_object ubo{};

    const mat4f& view_matrix = camera.get_view_matrix();
    memcpy(ubo.view, view_matrix.cptr(), sizeof(mat4f));

    const mat4f& projection_matrix = camera.get_projection_matrix();
    memcpy(ubo.projection, projection_matrix.cptr(), sizeof(mat4f));

    ubo.view_pos = camera.get_position();

    const auto& light_space_matrices = shadow_map_->get_light_space_matrices();
    const auto& cascade_splits       = shadow_map_->get_cascade_splits();
    const auto& texels   = shadow_map_->get_cascade_texel_sizes();
    const auto& shadows  = shadow_map_->get_settings();

    ubo.directional_light.shadow_filter = vec4f{
        shadows.filter_texels, shadows.normal_bias, shadows.slope_bias, 0.0f
    };

    for (uint32 i = 0; i < shadow_map::cascade_count; ++i) {
        ubo.directional_light.light_space_matrices[i] = light_space_matrices[i];
        ubo.directional_light.cascades[i] = vec4f{cascade_splits[i], texels[i], 0.0f, 0.0f};
    }
    ubo.directional_light.direction = directional_light_settings_.direction;
    ubo.directional_light.color     = directional_light_settings_.color;
    ubo.directional_light.intensity = directional_light_settings_.intensity;
    ubo.directional_light.wrap      = directional_light_settings_.wrap;

    const float32 ambient_strength = ambient_settings_.strength;
    ubo.ambient_sky = vec4f{
        ambient_settings_.sky.x * ambient_strength,
        ambient_settings_.sky.y * ambient_strength,
        ambient_settings_.sky.z * ambient_strength,
        0.0f
    };
    ubo.ambient_ground = vec4f{
        ambient_settings_.ground.x * ambient_strength,
        ambient_settings_.ground.y * ambient_strength,
        ambient_settings_.ground.z * ambient_strength,
        0.0f
    };

    ubo.corner_shading = corner_shading_data{
        .ao_strength     = ambient_settings_.ao_strength,
        .ao_curve        = ambient_settings_.ao_curve,
        .convex_strength = ambient_settings_.convex_strength,
        .convex_curve    = ambient_settings_.convex_curve,
    };

    ubo.cave_ambient = vec4f{
        ambient_settings_.cave.x, ambient_settings_.cave.y, ambient_settings_.cave.z, 0.0f
    };

    ubo.sky_params =
        vec4f{ambient_settings_.sky_curve, ambient_settings_.sun_curve, 0.0f, 0.0f};

    ubo.lamp_params = vec4f{
        block_light_settings_.color.x * block_light_settings_.intensity,
        block_light_settings_.color.y * block_light_settings_.intensity,
        block_light_settings_.color.z * block_light_settings_.intensity,
        block_light_settings_.curve,
    };

    ubo.glow_params = vec4f{block_light_settings_.glow, 0.0f, 0.0f, 0.0f};

    ubo.tonemap_params = tonemap_push_();

    ubo.debug_view = static_cast<uint32>(debug_view_);


    ubo.point_lights_count = light_buffer_->get_lights_count();
    ubo.blob_strength      = blob_strength_;
    ubo.blob_dims          = vec4<uint32>{
        cluster_settings_.blob_cap, blob_buffer_->get_count(), 0, 0
    };

    const spatial::cluster_grid grid = get_cluster_grid(camera);

    ubo.clusters = cluster_data{
        .z_scale   = grid.z_scale(),
        .z_bias    = grid.z_bias(),
        .tile_size = static_cast<float32>(grid.tile_size),
        .slices    = static_cast<float32>(grid.slices),
        .tiles_x   = grid.tiles_x(),
        .tiles_y   = grid.tiles_y(),
        .cap       = cluster_settings_.cap,
        .enabled   = cluster_settings_.enabled ? 1u : 0u,
    };

    ubo.fog.color         = scene_from_display(fog_settings_.color, tonemap_settings_);
    ubo.fog.near_distance = fog_settings_.near_distance;
    ubo.fog.far_distance  = fog_settings_.far_distance;
    ubo.fog.enabled       = fog_settings_.enabled ? 1u : 0u;

    uniform_buffers_[current_frame_]->copy_from_struct(ubo);
}

auto renderer::render_debug_primitives() -> void {
    if (debug_primitives_.is_empty()) {
        return;
    }

    update_debug_vertex_buffer();

    command_buffers_[current_frame_].bindPipeline(vk::PipelineBindPoint::eGraphics, debug_pipeline_);
    command_buffers_[current_frame_].pushConstants<vec4f>(
        debug_pipeline_layout_, vk::ShaderStageFlagBits::eFragment, 0, tonemap_push_()
    );

    command_buffers_[current_frame_].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        debug_pipeline_layout_,
        0,
        descriptor_sets_[current_frame_],
        nullptr
    );

    vk::Buffer vertex_buffer        = debug_vertex_buffers_[current_frame_]->get_buffer();
    constexpr vk::DeviceSize offset = 0;
    command_buffers_[current_frame_].bindVertexBuffers(0, vertex_buffer, offset);

    command_buffers_[current_frame_].draw(static_cast<uint32>(debug_primitives_.get_vertices().size()),
        1,
        0,
        0);
}

auto renderer::update_debug_vertex_buffer() -> void {
    const auto& debug_vertices = debug_primitives_.get_vertices();

    auto& target = debug_vertex_buffers_[current_frame_];

    const vk::DeviceSize required_size = sizeof(debug_vertex) * debug_vertices.size();
    if (required_size > target->get_size()) {
        target = std::make_unique<vertex_buffer>(*context_, required_size);
    }

    target->copy_from_vector(debug_vertices);
}

auto renderer::render_debug_solids() -> void {
    if (debug_primitives_.is_solid_empty()) {
        return;
    }

    update_debug_solid_vertex_buffer();

    auto cmd = command_buffers_[current_frame_];

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, debug_solid_pipeline_);
    cmd.pushConstants<vec4f>(
        debug_pipeline_layout_, vk::ShaderStageFlagBits::eFragment, 0, tonemap_push_()
    );
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, debug_pipeline_layout_, 0, descriptor_sets_[current_frame_],
        nullptr
    );

    vk::Buffer vertex_buffer        = debug_solid_vertex_buffers_[current_frame_]->get_buffer();
    constexpr vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, vertex_buffer, offset);

    cmd.draw(static_cast<uint32>(debug_primitives_.get_solid_vertices().size()), 1, 0, 0);
}

auto renderer::update_debug_solid_vertex_buffer() -> void {
    const auto& solid_vertices = debug_primitives_.get_solid_vertices();

    auto& target = debug_solid_vertex_buffers_[current_frame_];

    const vk::DeviceSize required_size = sizeof(debug_vertex) * solid_vertices.size();
    if (required_size > target->get_size()) {
        target = std::make_unique<vertex_buffer>(*context_, required_size);
    }

    target->copy_from_vector(solid_vertices);
}

auto renderer::render_imgui() const -> void {
    ImGui::Render();
    if (draws_interface_()) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffers_[current_frame_]);
    }
}

auto renderer::get_shadow_map_texture_id(
    uint32 cascade_index
) const -> void* {
    struct Cache {
        std::array<vk::DescriptorSet, shadow_map::cascade_count> sets{};
        std::array<vk::ImageView, shadow_map::cascade_count> views{};
        vk::Sampler sampler = nullptr;
        bool valid        = false;
    };

    static Cache cache;

    vk::Sampler current_sampler = shadow_map_->get_debug_sampler();
    bool need_rebuild         = !cache.valid || cache.sampler != current_sampler;

    for (uint32 i = 0; i < shadow_map::cascade_count; ++i) {
        vk::ImageView v = shadow_map_->get_image_view(i);
        if (!cache.valid || cache.views[i] != v) {
            need_rebuild = true;
        }
    }

    if (need_rebuild) {
        cache.sampler = current_sampler;
        for (uint32 i = 0; i < shadow_map::cascade_count; ++i) {
            cache.views[i] = shadow_map_->get_image_view(i);
            cache.sets[i]  = ImGui_ImplVulkan_AddTexture(
                cache.sampler,
                cache.views[i],
                static_cast<VkImageLayout>(vk::ImageLayout::eDepthStencilReadOnlyOptimal)
            );
        }
        cache.valid = true;
    }

    return cache.sets[cascade_index];
}

auto renderer::find_depth_format() -> vk::Format {
    return find_supported_format(
        {
            vk::Format::eD32Sfloat,
            vk::Format::eD32SfloatS8Uint,
            vk::Format::eD24UnormS8Uint,
        },
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}

auto renderer::find_supported_format(
    const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features
) -> vk::Format {
    for (vk::Format format : candidates) {
        const vk::FormatProperties props =
            context_->get_physical_device().getFormatProperties(format);
        if (tiling == vk::ImageTiling::eLinear &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == vk::ImageTiling::eOptimal &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("failed to find supported format for physical device");
}

auto renderer::create_image(
    vk::Image& image,
    vk::DeviceMemory& image_memory,
    vk::Extent2D extent,
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::SampleCountFlagBits samples
) -> void {
    vk::ImageCreateInfo image_info{};
    image_info.imageType     = vk::ImageType::e2D;
    image_info.extent.width  = extent.width;
    image_info.extent.height = extent.height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = format;
    image_info.tiling        = tiling;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage         = usage;
    image_info.samples       = samples;
    image_info.sharingMode   = vk::SharingMode::eExclusive;

    image = vk_must(context_->get_device().createImage(image_info), "failed to create image");

    const vk::MemoryRequirements mem_requirements =
        context_->get_device().getImageMemoryRequirements(image);

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize  = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

    image_memory = vk_must(context_->get_device().allocateMemory(alloc_info), "failed to allocate image memory");

    vk_must(
        context_->get_device().bindImageMemory(image, image_memory, 0), "bind image memory"
    );
}

auto renderer::create_image_view(
    vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_flags
) -> vk::ImageView {
    vk::ImageViewCreateInfo view_info{};
    view_info.image                           = image;
    view_info.viewType                        = vk::ImageViewType::e2D;
    view_info.format                          = format;
    view_info.subresourceRange.aspectMask     = aspect_flags;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    vk::ImageView image_view;
    image_view = vk_must(context_->get_device().createImageView(view_info), "failed to create image view");

    return image_view;
}

auto renderer::find_memory_type(
    uint32 typeFilter, vk::MemoryPropertyFlags properties
) -> uint32 {
    const vk::PhysicalDeviceMemoryProperties mem_properties =
        context_->get_physical_device().getMemoryProperties();

    for (uint32 i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

auto renderer::choose_swap_surface_format(
    const std::vector<vk::SurfaceFormatKHR>& available_formats
) -> vk::SurfaceFormatKHR {
    for (const auto& available_format : available_formats) {
        if (available_format.format == vk::Format::eB8G8R8A8Srgb &&
            available_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return available_format;
        }
    }
    return available_formats[0];
}

auto renderer::choose_swap_present_mode(
    const std::vector<vk::PresentModeKHR>& available_present_modes
) -> vk::PresentModeKHR {
    for (const auto& available_present_mode : available_present_modes) {
        if (available_present_mode == vk::PresentModeKHR::eMailbox) {
            return available_present_mode;
        }
    }
    return vk::PresentModeKHR::eFifo;
}

auto renderer::choose_swap_extent(
    const vk::SurfaceCapabilitiesKHR& capabilities
) -> vk::Extent2D {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32>::max()) {
        return capabilities.currentExtent;
    }

    const vec2i size = window_->framebuffer_size();

    vk::Extent2D actual_extent = {static_cast<uint32>(size.x), static_cast<uint32>(size.y)};

    actual_extent.width = std::clamp(
        actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width
    );
    actual_extent.height = std::clamp(
        actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height
    );

    return actual_extent;
}

}  // namespace vw::gfx

namespace vw::gfx {

auto renderer::create_palette_descriptor_set_layout() -> void {
    vk::DescriptorSetLayoutBinding palette_layout_binding{};
    palette_layout_binding.binding            = 0;
    palette_layout_binding.descriptorType     = vk::DescriptorType::eStorageBuffer;
    palette_layout_binding.descriptorCount    = 1;
    palette_layout_binding.stageFlags         = vk::ShaderStageFlagBits::eVertex;
    palette_layout_binding.pImmutableSamplers = nullptr;

    vk::DescriptorSetLayoutCreateInfo palette_layout_info{};
    palette_layout_info.bindingCount = 1;
    palette_layout_info.pBindings    = &palette_layout_binding;

    palette_descriptor_set_layout_ = vk_must(context_->get_device().createDescriptorSetLayout(palette_layout_info), "failed to create palette descriptor set layout");
}

auto renderer::cleanup_palette_resources() -> void {
    if (palette_descriptor_set_layout_ != nullptr) {
        context_->get_device().destroyDescriptorSetLayout(palette_descriptor_set_layout_);
        palette_descriptor_set_layout_ = nullptr;
    }
}

}  // namespace vw::gfx

namespace vw::gfx {

auto renderer::create_point_lights_descriptor_set_layout() -> void {
    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};

    for (uint32 i = 0; i < bindings.size(); ++i) {
        bindings[i] = {
            .binding         = i,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags =
                vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute,
        };
    }

    point_lights_descriptor_set_layout_ = vk_must(
        context_->get_device().createDescriptorSetLayout({
            .bindingCount = static_cast<uint32>(bindings.size()),
            .pBindings    = bindings.data(),
        }),
        "failed to create point lights descriptor set layout"
    );
}

auto renderer::cleanup_point_lights_resources() -> void {
    if (point_lights_descriptor_set_layout_ != nullptr) {
        context_->get_device().destroyDescriptorSetLayout(point_lights_descriptor_set_layout_);
        point_lights_descriptor_set_layout_ = nullptr;
    }
}

}  // namespace vw::gfx

namespace vw::gfx {

auto renderer::create_shadow_uniform_buffers() -> void {
    vk::DeviceSize buffer_size = sizeof(shadow_uniform_buffer_object);
    shadow_uniform_buffers_.resize(frames_in_flight);

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        shadow_uniform_buffers_[i] = std::make_unique<uniform_buffer>(*context_, buffer_size);
    }
}

auto renderer::create_shadow_descriptor_sets() -> void {
    std::vector layouts(frames_in_flight, uniform_descriptor_set_layout_);
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = static_cast<uint32>(frames_in_flight);
    alloc_info.pSetLayouts        = layouts.data();

    shadow_descriptor_sets_ =
        vk_must(context_->get_device().allocateDescriptorSets(alloc_info), "allocate shadow descriptor sets");

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        vk::DescriptorBufferInfo ubo_buffer_info{};
        ubo_buffer_info.buffer = shadow_uniform_buffers_[i]->get_buffer();
        ubo_buffer_info.offset = 0;
        ubo_buffer_info.range  = sizeof(shadow_uniform_buffer_object);

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet          = shadow_descriptor_sets_[i];
        descriptor_write.dstBinding      = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType  = vk::DescriptorType::eUniformBuffer;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo     = &ubo_buffer_info;

        context_->get_device().updateDescriptorSets(descriptor_write, nullptr);
    }
}

auto renderer::create_shadow_map_descriptor_sets() -> void {
    std::vector layouts(frames_in_flight, shadow_descriptor_set_layout_);
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool     = descriptor_pool_;
    alloc_info.descriptorSetCount = static_cast<uint32>(frames_in_flight);
    alloc_info.pSetLayouts        = layouts.data();

    shadow_map_descriptor_sets_ =
        vk_must(context_->get_device().allocateDescriptorSets(alloc_info), "allocate shadow map descriptor sets");

    for (std::size_t i = 0; i < frames_in_flight; i++) {
        vk::DescriptorImageInfo image_info{};
        image_info.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        image_info.imageView   = shadow_map_->get_array_image_view();
        image_info.sampler     = shadow_map_->get_sampler();

        vk::WriteDescriptorSet descriptor_write{};
        descriptor_write.dstSet          = shadow_map_descriptor_sets_[i];
        descriptor_write.dstBinding      = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo      = &image_info;

        context_->get_device().updateDescriptorSets(descriptor_write, nullptr);
    }
}

auto renderer::create_shadow_pipeline() -> void {
    vk::PipelineShaderStageCreateInfo shader_stages[] = {
        shadow_vertex_shader_->get_stage_info(), shadow_fragment_shader_->get_stage_info()
    };

    auto binding_description    = quad::get_binding_descriptions();
    auto attribute_descriptions = quad::get_attribute_descriptions();

    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.vertexBindingDescriptionCount =
        static_cast<uint32>(binding_description.size());
    vertex_input_info.pVertexBindingDescriptions = binding_description.data();
    vertex_input_info.vertexAttributeDescriptionCount =
        static_cast<uint32>(attribute_descriptions.size());
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;
    input_assembly.primitiveRestartEnable = vk::False;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr;
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr;

    vk::DynamicState dynamic_states[] = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable        = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode             = vk::PolygonMode::eFill;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = vk::CullModeFlagBits::eFront;
    rasterizer.frontFace               = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable         = vk::True;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor    = 1.75f;
    rasterizer.depthBiasClamp          = 0.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable  = vk::False;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    color_blending.logicOpEnable   = vk::False;
    color_blending.attachmentCount = 0;
    color_blending.pAttachments    = nullptr;

    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable  = vk::True;
    depth_stencil.depthWriteEnable = vk::True;
    depth_stencil.depthCompareOp   = vk::CompareOp::eLess;
    depth_stencil.depthBoundsTestEnable = vk::False;
    depth_stencil.stencilTestEnable     = vk::False;

    vk::PushConstantRange push_constant_range{};
    push_constant_range.offset     = 0;
    push_constant_range.size       = sizeof(shadow_push_constant_data);
    push_constant_range.stageFlags = vk::ShaderStageFlagBits::eVertex;

    std::array shadow_descriptor_set_layouts = {
        uniform_descriptor_set_layout_, storage_descriptor_set_layout_
    };

    vk::PipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.setLayoutCount =
        static_cast<uint32>(shadow_descriptor_set_layouts.size());
    pipeline_layout_info.pSetLayouts            = shadow_descriptor_set_layouts.data();
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges    = &push_constant_range;

    shadow_pipeline_layout_ = vk_must(context_->get_device().createPipelineLayout(pipeline_layout_info), "failed to create shadow pipeline layout");

    vk::GraphicsPipelineCreateInfo pipeline_info{};

    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = shader_stages;
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = shadow_pipeline_layout_;
    pipeline_info.renderPass          = shadow_map_->get_render_pass();
    pipeline_info.subpass             = 0;
    pipeline_info.basePipelineHandle  = nullptr;

    shadow_pipeline_ =
        vk_must(context_->get_device().createGraphicsPipeline(nullptr, pipeline_info), "create shadow pipeline");
}

auto renderer::update_shadow_uniform_buffer() const -> void {
    shadow_uniform_buffer_object ubo{};
    const auto& light_space_matrices = shadow_map_->get_light_space_matrices();
    for (uint32 i = 0; i < shadow_map::cascade_count; ++i) {
        ubo.light_space_matrices[i] = light_space_matrices[i];
    }
    shadow_uniform_buffers_[current_frame_]->copy_from_struct(ubo);
}

auto renderer::render_shadow_pass(
    [[maybe_unused]] world_type& world, [[maybe_unused]] const camera& camera
) -> void {
    update_shadow_uniform_buffer();

    stats_.timing.shadow_cascades_drawn =
        static_cast<float32>(shadow_map_->get_pending_count());

    for (uint32 cascade_index = 0; cascade_index < shadow_map::cascade_count; ++cascade_index) {
        if (!shadow_map_->is_cascade_pending(cascade_index)) {
            continue;
        }

        const auto cascade_stage =
            static_cast<gpu_stage>(static_cast<uint32>(gpu_stage::shadow_cascade_0) + cascade_index);
        gpu_timer_->begin(command_buffers_[current_frame_], cascade_stage);

        vk::RenderPassBeginInfo render_pass_info{};
        render_pass_info.renderPass        = shadow_map_->get_render_pass();
        render_pass_info.framebuffer       = shadow_map_->get_framebuffer(cascade_index);
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = {
            shadow_map_->get_size(), shadow_map_->get_size()
        };

        vk::ClearValue clear_value{};
        clear_value.depthStencil = {1.0f, 0};

        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues    = &clear_value;

        command_buffers_[current_frame_].beginRenderPass(render_pass_info, vk::SubpassContents::eInline);

        vk::Viewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(shadow_map_->get_size());
        viewport.height   = static_cast<float>(shadow_map_->get_size());
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        command_buffers_[current_frame_].setViewport(0, viewport);

        vk::Rect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {shadow_map_->get_size(), shadow_map_->get_size()};
        command_buffers_[current_frame_].setScissor(0, scissor);

        command_buffers_[current_frame_].bindPipeline(vk::PipelineBindPoint::eGraphics,
            shadow_pipeline_);

        command_buffers_[current_frame_].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        shadow_pipeline_layout_,
        0,
        shadow_descriptor_sets_[current_frame_],
        nullptr
    );

        shadow_push_constant_data push_constants{
            .wind          = wind_push_(),
            .cascade_index = cascade_index,
        };
        command_buffers_[current_frame_].pushConstants<shadow_push_constant_data>(shadow_pipeline_layout_, vk::ShaderStageFlagBits::eVertex, 0, push_constants);

        const auto& buffers = combined_buffer_pool_->get_buffers();
        for (const auto& buffer : buffers) {
            if (buffer->is_empty()) {
                continue;
            }

            vk::Buffer instance_index_buffer = buffer->get_instance_index_buffer();

            vk::DescriptorSet buffer_descriptor_set = buffer->get_descriptor_set(current_frame_);
            command_buffers_[current_frame_].bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                shadow_pipeline_layout_,
                1,
                1,
                &buffer_descriptor_set,
                0,
                nullptr);

            constexpr vk::DeviceSize instance_offset = 0;
            command_buffers_[current_frame_].bindVertexBuffers(
                0, instance_index_buffer, instance_offset);
            command_buffers_[current_frame_].bindIndexBuffer(
                combined_buffer_pool_->get_index_buffer(), 0, vk::IndexType::eUint32);

            const uint32 max_draws  = buffer->get_draw_command_count();
            const uint32 pass_index = cascade_index + 1;
            if (max_draws > 0) {
                command_buffers_[current_frame_].drawIndexedIndirectCount(buffer->get_culled_indirect_buffer(),
                    static_cast<vk::DeviceSize>(pass_index) * max_draws * sizeof(draw_command),
                    buffer->get_count_buffer(),
                    pass_index * sizeof(uint32),
                    max_draws,
                    sizeof(draw_command));
            }
        }

        command_buffers_[current_frame_].endRenderPass();
        gpu_timer_->end(command_buffers_[current_frame_], cascade_stage);
    }

    shadow_map_->clear_pending();
}

}  // namespace vw::gfx
