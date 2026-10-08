module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {
namespace {
constexpr log::log_category lc_{"vulkan_context"};

auto dispatcher() -> vk::detail::DispatchLoaderDynamic& {
    return vk::detail::defaultDispatchLoaderDynamic;
}

#ifndef NDEBUG
auto debug_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
    vk::DebugUtilsMessageTypeFlagsEXT,
    const vk::DebugUtilsMessengerCallbackDataEXT* p_callback_data,
    void*
) -> vk::Bool32 {
    switch (message_severity) {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            log::error(lc_, "Validation layer: {}", p_callback_data->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            log::warn(lc_, "Validation layer: {}", p_callback_data->pMessage);
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
            log::debug(lc_, "Validation layer: {}", p_callback_data->pMessage);
            break;
        default:
            log::info(lc_, "Validation layer: {}", p_callback_data->pMessage);
            break;
    }
    return vk::False;
}
#endif

}  // namespace

vulkan_context::vulkan_context(
    window& window
)
    : window_(&window) {
    device_extensions_ = {vk::KHRSwapchainExtensionName, vk::EXTMemoryBudgetExtensionName};
#ifdef __APPLE__
    device_extensions_.push_back("VK_KHR_portability_subset");
#endif

    dispatcher().init();

    create_instance_();

#ifndef NDEBUG
    setup_debug_messenger_();
#endif

    create_surface_();
    pick_physical_device_();
    create_logical_device_();
    create_command_pool_();
}

vulkan_context::~vulkan_context() {
    device_.destroyCommandPool(command_pool_);
    device_.destroy();
#ifndef NDEBUG
    instance_.destroyDebugUtilsMessengerEXT(debug_messenger_);
#endif
    instance_.destroySurfaceKHR(surface_);
    instance_.destroy();
}

auto vulkan_context::get_instance() const -> vk::Instance {
    return instance_;
}

auto vulkan_context::get_device() const -> vk::Device {
    return device_;
}

auto vulkan_context::get_physical_device() const -> vk::PhysicalDevice {
    return physical_device_;
}

auto vulkan_context::get_graphics_queue() const -> vk::Queue {
    return graphics_queue_;
}

auto vulkan_context::get_present_queue() const -> vk::Queue {
    return present_queue_;
}

auto vulkan_context::get_surface() const -> vk::SurfaceKHR {
    return surface_;
}

auto vulkan_context::get_command_pool() const -> vk::CommandPool {
    return command_pool_;
}

auto vulkan_context::get_queue_families() const -> queue_family_indices {
    return queue_families_;
}

auto vulkan_context::query_swapchain_support_() const -> swapchain_support_details {
    return query_swapchain_support_(physical_device_);
}

auto vulkan_context::create_instance_() -> void {
    constexpr vk::ApplicationInfo app_info{
        .pApplicationName   = "Voxel World",
        .applicationVersion = vk::makeApiVersion(0u, 1u, 0u, 0u),
        .pEngineName        = "No Engine",
        .engineVersion      = vk::makeApiVersion(0u, 1u, 0u, 0u),
        .apiVersion         = vk::ApiVersion12,
    };

    auto extensions = plat::window::required_extensions();

#ifndef NDEBUG
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

    log::debug(lc_, "Requested instance extensions:");
    for (const auto& extension : extensions) {
        log::debug(lc_, "\t- {}", extension);
    }

    vk::InstanceCreateInfo create_info{
        .pApplicationInfo        = &app_info,
        .enabledExtensionCount   = static_cast<uint32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

#ifdef __APPLE__
    create_info.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

#ifndef NDEBUG
    static constexpr std::array validation_layers = {"VK_LAYER_KHRONOS_validation"};

    const auto available_layers =
        vk_must(vk::enumerateInstanceLayerProperties(), "enumerate instance layers");

    log::debug(lc_, "Requested validation layers:");
    for (const auto* layer : validation_layers) {
        const bool found = std::ranges::any_of(available_layers, [layer](const auto& available) {
            return std::string_view{available.layerName.data()} == layer;
        });
        if (found) {
            log::debug(lc_, "\t- {}", layer);
        } else {
            log::debug(lc_, "\t- {} - not found", layer);
        }
    }

    create_info.enabledLayerCount   = static_cast<uint32>(validation_layers.size());
    create_info.ppEnabledLayerNames = validation_layers.data();
#endif

    instance_ = vk_must(vk::createInstance(create_info), "create vulkan instance");
    dispatcher().init(instance_);
}

auto vulkan_context::create_surface_() -> void {
    const uint64 handle = window_->create_surface(
        reinterpret_cast<uint64>(static_cast<vk::Instance::NativeType>(instance_))
    );
    surface_ = vk::SurfaceKHR{reinterpret_cast<vk::SurfaceKHR::NativeType>(handle)};
}

auto vulkan_context::pick_physical_device_() -> void {
    const auto devices =
        vk_must(instance_.enumeratePhysicalDevices(), "enumerate physical devices");

    if (devices.empty()) {
        throw std::runtime_error("failed to find gpu with vulkan support");
    }

    log::info(lc_, "Found {} devices with vulkan support", devices.size());

    for (const auto& device : devices) {
        if (is_device_suitable_(device)) {
            physical_device_ = device;
            log::info(lc_, "Selected device: {}", device.getProperties().deviceName.data());
            return;
        }
    }

    log::error(lc_, "No suitable devices found!");
    throw std::runtime_error("failed to find a suitable gpu");
}

auto vulkan_context::create_logical_device_() -> void {
    queue_families_ = find_queue_families_(physical_device_);

    const std::set<uint32> unique_queue_families = {
        queue_families_.graphics_family.value(), queue_families_.present_family.value()
    };

    constexpr float32 queue_priority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(unique_queue_families.size());
    for (const uint32 queue_family : unique_queue_families) {
        queue_create_infos.push_back({
            .queueFamilyIndex = queue_family,
            .queueCount       = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    counts_pipeline_statistics_ =
        physical_device_.getFeatures().pipelineStatisticsQuery == vk::True;

    const vk::PhysicalDeviceFeatures device_features{
        .multiDrawIndirect         = vk::True,
        .drawIndirectFirstInstance = vk::True,
        .fillModeNonSolid          = vk::True,
        .pipelineStatisticsQuery   = counts_pipeline_statistics_ ? vk::True : vk::False,
    };

    const vk::StructureChain<
        vk::DeviceCreateInfo,
        vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceShaderDrawParametersFeatures>
        create_info{
            vk::DeviceCreateInfo{
                .queueCreateInfoCount    = static_cast<uint32>(queue_create_infos.size()),
                .pQueueCreateInfos       = queue_create_infos.data(),
                .enabledExtensionCount   = static_cast<uint32>(device_extensions_.size()),
                .ppEnabledExtensionNames = device_extensions_.data(),
                .pEnabledFeatures        = &device_features,
            },
            vk::PhysicalDeviceVulkan12Features{.drawIndirectCount = vk::True},
            vk::PhysicalDeviceShaderDrawParametersFeatures{.shaderDrawParameters = vk::True},
        };

    device_ = vk_must(
        physical_device_.createDevice(create_info.get<vk::DeviceCreateInfo>()),
        "create logical device"
    );
    dispatcher().init(device_);

    graphics_queue_ = device_.getQueue(queue_families_.graphics_family.value(), 0);
    present_queue_  = device_.getQueue(queue_families_.present_family.value(), 0);
}

auto vulkan_context::get_timestamp_period() const -> float32 {
    return physical_device_.getProperties().limits.timestampPeriod;
}

auto vulkan_context::get_timestamp_valid_bits() const -> uint32 {
    const auto families = physical_device_.getQueueFamilyProperties();
    const uint32 graphics = queue_families_.graphics_family.value();
    if (graphics >= families.size()) {
        return 0;
    }
    return families[graphics].timestampValidBits;
}

auto vulkan_context::create_command_pool_() -> void {
    command_pool_ = vk_must(
        device_.createCommandPool({
            .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = queue_families_.graphics_family.value(),
        }),
        "create command pool"
    );
}

auto vulkan_context::is_device_suitable_(
    vk::PhysicalDevice device
) -> bool {
    const auto properties = device.getProperties();

    log::debug(lc_, "Checking device: {}", properties.deviceName.data());
    log::debug(lc_, "Type: {}", vk::to_string(properties.deviceType));
    log::debug(
        lc_,
        "API Version: {}.{}.{}",
        vk::apiVersionMajor(properties.apiVersion),
        vk::apiVersionMinor(properties.apiVersion),
        vk::apiVersionPatch(properties.apiVersion)
    );
    log::debug(
        lc_,
        "Driver Version: {}.{}.{}",
        vk::apiVersionMajor(properties.driverVersion),
        vk::apiVersionMinor(properties.driverVersion),
        vk::apiVersionPatch(properties.driverVersion)
    );

    const queue_family_indices indices = find_queue_families_(device);
    log::debug(lc_, "Queue Support:");
    log::debug(lc_, "  Graphics: {}", indices.graphics_family.has_value() ? "Yes" : "No");
    log::debug(lc_, "  Present: {}", indices.present_family.has_value() ? "Yes" : "No");

    const bool extensions_supported = check_device_extension_support_(device);
    log::debug(lc_, "Extension Support: {}", extensions_supported ? "Yes" : "No");

    if (!extensions_supported) {
        log::debug(lc_, "Required Extensions:");
        for (const auto& extension : device_extensions_) {
            log::debug(lc_, "  - {}", extension);
        }

        log::debug(lc_, "Available Extensions:");
        for (const auto& extension :
             device.enumerateDeviceExtensionProperties(nullptr).value_or(
                 std::vector<vk::ExtensionProperties>{}
             )) {
            log::debug(lc_, "  - {}", extension.extensionName.data());
        }
    }

    bool swapchain_adequate = false;
    if (extensions_supported) {
        const swapchain_support_details swapchain_support = query_swapchain_support_(device);
        swapchain_adequate =
            !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();

        log::debug(lc_, "Swapchain Support: {}", swapchain_adequate ? "Yes" : "No");
        if (!swapchain_adequate) {
            log::debug(lc_, "  Formats: {}", swapchain_support.formats.size());
            log::debug(lc_, "  Present Modes: {}", swapchain_support.present_modes.size());
        }
    }

    const auto surface_support =
        device.getSurfaceSupportKHR(indices.graphics_family.value(), surface_).value_or(vk::False);
    log::debug(lc_, "Surface Support: {}", surface_support != vk::False ? "Yes" : "No");

    const bool suitable = indices.is_complete() && extensions_supported && swapchain_adequate;
    log::debug(lc_, "Suitable: {}", suitable ? "Yes" : "No");

    return suitable;
}

auto vulkan_context::find_queue_families_(
    vk::PhysicalDevice device
) -> queue_family_indices {
    queue_family_indices indices;

    const auto queue_families = device.getQueueFamilyProperties();
    for (uint32 i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics_family = i;
        }

        if (device.getSurfaceSupportKHR(i, surface_).value_or(vk::False) != vk::False) {
            indices.present_family = i;
        }

        if (indices.is_complete()) {
            break;
        }
    }

    return indices;
}

auto vulkan_context::check_device_extension_support_(
    vk::PhysicalDevice device
) -> bool {
    const auto available_extensions = device.enumerateDeviceExtensionProperties(nullptr).value_or(
        std::vector<vk::ExtensionProperties>{}
    );

    std::set<std::string> required_extensions(device_extensions_.begin(), device_extensions_.end());
    for (const auto& extension : available_extensions) {
        required_extensions.erase(extension.extensionName.data());
    }

    if (!required_extensions.empty()) {
        log::debug(lc_, "Missing extensions:");
        for (const auto& missing : required_extensions) {
            log::debug(lc_, "  - {}", missing);
        }
    }

    return required_extensions.empty();
}

auto vulkan_context::query_swapchain_support_(
    vk::PhysicalDevice device
) const -> swapchain_support_details {
    return {
        .capabilities =
            device.getSurfaceCapabilitiesKHR(surface_).value_or(vk::SurfaceCapabilitiesKHR{}),
        .formats =
            device.getSurfaceFormatsKHR(surface_).value_or(std::vector<vk::SurfaceFormatKHR>{}),
        .present_modes =
            device.getSurfacePresentModesKHR(surface_).value_or(std::vector<vk::PresentModeKHR>{}),
    };
}

#ifndef NDEBUG
auto vulkan_context::setup_debug_messenger_() -> void {
    debug_messenger_ = vk_must(
        instance_.createDebugUtilsMessengerEXT({
            .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
            .pfnUserCallback = debug_callback,
        }),
        "create debug messenger"
    );
}
#endif

}  // namespace vw::gfx
