export module vw.gfx:render.vulkan_context;

import std;

import vw.core;
import vw.platform;
import vulkan;

namespace vw::gfx {
using namespace ::vw::plat;
}

export namespace vw::gfx {


struct queue_family_indices {
    std::optional<uint32> graphics_family;
    std::optional<uint32> present_family;

    [[nodiscard]]
    auto is_complete() const -> bool {
        return graphics_family.has_value() && present_family.has_value();
    }
};

struct swapchain_support_details {
    vk::SurfaceCapabilitiesKHR capabilities{};
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> present_modes;
};

class vulkan_context final {
public:
    explicit vulkan_context(window& window);
    ~vulkan_context();

    vulkan_context(const vulkan_context&)                    = delete;
    auto operator=(const vulkan_context&) -> vulkan_context& = delete;

    [[nodiscard]] auto get_instance() const -> vk::Instance;
    [[nodiscard]] auto get_device() const -> vk::Device;
    [[nodiscard]] auto get_physical_device() const -> vk::PhysicalDevice;
    [[nodiscard]] auto get_graphics_queue() const -> vk::Queue;
    [[nodiscard]] auto get_present_queue() const -> vk::Queue;
    [[nodiscard]] auto get_surface() const -> vk::SurfaceKHR;
    [[nodiscard]] auto get_command_pool() const -> vk::CommandPool;
    [[nodiscard]] auto get_queue_families() const -> queue_family_indices;
    [[nodiscard]] auto query_swapchain_support_() const -> swapchain_support_details;

    [[nodiscard]] auto get_timestamp_period() const -> float32;
    [[nodiscard]] auto get_timestamp_valid_bits() const -> uint32;

    [[nodiscard]] auto counts_samples_exactly() const -> bool {
        return counts_samples_exactly_;
    }

    [[nodiscard]] auto counts_pipeline_statistics() const -> bool {
        return counts_pipeline_statistics_;
    }

private:
    auto create_instance_() -> void;
    auto create_surface_() -> void;
    auto pick_physical_device_() -> void;
    auto create_logical_device_() -> void;
    auto create_command_pool_() -> void;

    [[nodiscard]] auto is_device_suitable_(vk::PhysicalDevice device) -> bool;
    [[nodiscard]] auto find_queue_families_(vk::PhysicalDevice device) -> queue_family_indices;
    [[nodiscard]] auto check_device_extension_support_(vk::PhysicalDevice device) -> bool;
    [[nodiscard]] auto query_swapchain_support_(vk::PhysicalDevice device) const
        -> swapchain_support_details;

    window* window_;

    vk::Instance instance_;
    vk::SurfaceKHR surface_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;
    vk::Queue graphics_queue_;
    vk::Queue present_queue_;
    vk::CommandPool command_pool_;
    queue_family_indices queue_families_;
    bool counts_pipeline_statistics_ = false;
    bool counts_samples_exactly_     = false;

    std::vector<const char*> device_extensions_;

#ifndef NDEBUG
    vk::DebugUtilsMessengerEXT debug_messenger_;

    auto setup_debug_messenger_() -> void;
#endif
};
}  // namespace vw::gfx
