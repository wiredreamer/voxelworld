export module vw.gfx:resource.staging_buffer;

import std;

import vw.core;
import :frames_in_flight;
import :resource.buffer;
import vulkan;

export namespace vw::gfx {

class vulkan_context;

class staging_buffer {
public:
    explicit staging_buffer(
        vulkan_context& context,
        vk::DeviceSize frame_capacity  = 1 * 1024 * 1024
    );
    ~staging_buffer();

    staging_buffer(const staging_buffer&)                    = delete;
    auto operator=(const staging_buffer&) -> staging_buffer& = delete;
    staging_buffer(staging_buffer&&)                          = delete;
    auto operator=(staging_buffer&&) -> staging_buffer&       = delete;

    auto begin_frame() -> void;

    auto stage(const void* data, vk::DeviceSize size) -> vk::DeviceSize;

    template <typename T>
    auto stage_struct(const T& data) -> vk::DeviceSize {
        return stage(&data, sizeof(T));
    }

    template <typename T>
    auto stage_vector(const std::vector<T>& data) -> vk::DeviceSize {
        return stage(data.data(), data.size() * sizeof(T));
    }

    auto copy_to(
        vk::Buffer dst, vk::DeviceSize dst_offset, vk::DeviceSize staging_offset, vk::DeviceSize size
    ) -> void;

    auto copy_buffer(
        vk::Buffer src, vk::DeviceSize src_offset,
        vk::Buffer dst, vk::DeviceSize dst_offset,
        vk::DeviceSize size
    ) -> void;


    [[nodiscard]] auto available() const -> vk::DeviceSize {
        return frame_end_offset_ - write_offset_;
    }

    [[nodiscard]] auto frame_capacity() const -> vk::DeviceSize {
        return frame_capacity_;
    }

    auto replace_buffer(vk::Buffer old_buf, vk::Buffer new_buf) -> void;

    auto flush(vk::CommandBuffer cmd) -> void;

private:
    struct pending_copy {
        vk::Buffer src;
        vk::Buffer dst;
        vk::BufferCopy region;
    };

    vulkan_context* context_;
    vk::Buffer buffer_          = nullptr;
    vk::DeviceMemory memory_    = nullptr;
    void* mapped_             = nullptr;

    vk::DeviceSize frame_capacity_    = 0;
    uint32 current_frame_index_     = 0;
    vk::DeviceSize write_offset_      = 0;
    vk::DeviceSize frame_end_offset_  = 0;

    std::vector<pending_copy> pending_copies_;
    std::vector<vk::BufferCopy> flush_regions_;
};

}  // namespace vw::gfx
