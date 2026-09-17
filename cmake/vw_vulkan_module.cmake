function(vw_add_vulkan_module)
    if(TARGET VulkanHppModule)
        return()
    endif()

    find_package(Vulkan REQUIRED)

    add_library(VulkanHppModule STATIC)

    target_sources(VulkanHppModule
        PUBLIC
            FILE_SET CXX_MODULES BASE_DIRS ${Vulkan_INCLUDE_DIR} FILES
                ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
    )

    target_compile_definitions(VulkanHppModule PUBLIC
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_USE_STD_EXPECTED
        VULKAN_HPP_NO_CONSTRUCTORS
        VULKAN_HPP_NO_SMART_HANDLE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING
    )

    target_link_libraries(VulkanHppModule PUBLIC Vulkan::Headers)
    target_compile_features(VulkanHppModule PUBLIC cxx_std_23)

    vw_use_std_module(VulkanHppModule)
endfunction()
