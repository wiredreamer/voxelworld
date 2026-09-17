export module vw.gfx:resource.buffer;

import std;

import vw.core;
import vulkan;

export namespace vw::gfx {
class vulkan_context;

class buffer {
public:
    buffer(
        vulkan_context& context,
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties
    );
    virtual ~buffer();

    buffer(const buffer&)            = delete;
    buffer& operator=(const buffer&) = delete;

    buffer(buffer&& other) noexcept;
    buffer& operator=(buffer&& other) noexcept;

    [[nodiscard]]
    auto get_buffer() const -> vk::Buffer {
        return buffer_;
    }

    [[nodiscard]]
    auto get_memory() const -> vk::DeviceMemory {
        return memory_;
    }

    [[nodiscard]]
    auto get_size() const -> vk::DeviceSize {
        return size_;
    }

    [[nodiscard]]
    auto map() -> void*;

    auto unmap() -> void;

    auto copy_from(const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) -> void;

    auto copy_to(void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) -> void;

    template <typename T>
    auto copy_from_struct(
        const T& data, vk::DeviceSize offset = 0
    ) -> void {
        copy_from(&data, sizeof(T), offset);
    }

    template <typename T>
    auto copy_from_vector(
        const std::vector<T>& data, vk::DeviceSize offset = 0
    ) -> void {
        copy_from(data.data(), data.size() * sizeof(T), offset);
    }

    template <typename T>
    auto copy_to_struct(
        T& dst, vk::DeviceSize offset = 0
    ) -> void {
        copy_to(&dst, sizeof(T), offset);
    }

protected:
    vk::DeviceSize size_;

private:
    auto cleanup() -> void;

    [[nodiscard]]
    auto find_memory_type(uint32 type_filter, vk::MemoryPropertyFlags properties) const -> uint32;

    vulkan_context* context_;

    vk::Buffer buffer_;
    vk::DeviceMemory memory_;
    void* mapped_memory_;
};

class vertex_buffer final : public buffer {
public:
    vertex_buffer(
        vulkan_context& context, vk::DeviceSize size
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eVertexBuffer |     //
                  vk::BufferUsageFlagBits::eTransferSrc |   //
                  vk::BufferUsageFlagBits::eTransferDst,    //
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {}

    template <typename T>
    vertex_buffer(
        vulkan_context& context, const std::vector<T>& vertices
    )
        : buffer(
              context,
              sizeof(T) * vertices.size(),
              vk::BufferUsageFlagBits::eVertexBuffer |     //
                  vk::BufferUsageFlagBits::eTransferSrc |   //
                  vk::BufferUsageFlagBits::eTransferDst,    //
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {
        copy_from_vector(vertices);
    }
};

class index_buffer final : public buffer {
public:
    index_buffer(
        vulkan_context& context, vk::DeviceSize size
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eIndexBuffer |      //
                  vk::BufferUsageFlagBits::eTransferSrc |   //
                  vk::BufferUsageFlagBits::eTransferDst,    //
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {}

    template <typename T>
    index_buffer(
        vulkan_context& context, const std::vector<T>& indices
    )
        : buffer(
              context,
              sizeof(T) * indices.size(),
              vk::BufferUsageFlagBits::eIndexBuffer |      //
                  vk::BufferUsageFlagBits::eTransferSrc |   //
                  vk::BufferUsageFlagBits::eTransferDst,    //
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {
        copy_from(indices.data(), size_);
    }
};

class uniform_buffer final : public buffer {
public:
    uniform_buffer(
        vulkan_context& context, vk::DeviceSize size
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eUniformBuffer,
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {}

    template <typename T>
    uniform_buffer(
        vulkan_context& context, const T& data
    )
        : buffer(
              context,
              sizeof(T),
              vk::BufferUsageFlagBits::eUniformBuffer,
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {
        copy_from(&data, size_);
    }
};

class storage_buffer final : public buffer {
public:
    storage_buffer(
        vulkan_context& context, vk::DeviceSize size, vk::BufferUsageFlags additional_usage = {}
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eStorageBuffer | additional_usage,
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {}

    template <typename T>
    storage_buffer(
        vulkan_context& context, const std::vector<T>& data, vk::BufferUsageFlags additional_usage = {}
    )
        : buffer(
              context,
              sizeof(T) * data.size(),
              vk::BufferUsageFlagBits::eStorageBuffer | additional_usage,
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
          ) {
        copy_from_vector(data);
    }
};
class device_vertex_buffer final : public buffer {
public:
    device_vertex_buffer(
        vulkan_context& context, vk::DeviceSize size
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eVertexBuffer |      //
                  vk::BufferUsageFlagBits::eTransferSrc |    //
                  vk::BufferUsageFlagBits::eTransferDst,     //
              vk::MemoryPropertyFlagBits::eDeviceLocal
          ) {}
};

class device_index_buffer final : public buffer {
public:
    device_index_buffer(
        vulkan_context& context, vk::DeviceSize size
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eIndexBuffer |        //
                  vk::BufferUsageFlagBits::eTransferSrc |    //
                  vk::BufferUsageFlagBits::eTransferDst,     //
              vk::MemoryPropertyFlagBits::eDeviceLocal
          ) {}
};

class device_storage_buffer final : public buffer {
public:
    device_storage_buffer(
        vulkan_context& context, vk::DeviceSize size, vk::BufferUsageFlags additional_usage = {}
    )
        : buffer(
              context,
              size,
              vk::BufferUsageFlagBits::eStorageBuffer |      //
                  vk::BufferUsageFlagBits::eTransferSrc |    //
                  vk::BufferUsageFlagBits::eTransferDst |    //
                  additional_usage,
              vk::MemoryPropertyFlagBits::eDeviceLocal
          ) {}
};

}  // namespace vw::gfx
