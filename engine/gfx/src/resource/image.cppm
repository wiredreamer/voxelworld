export module vw.gfx:resource.image;

import std;

import vw.core;

export namespace vw::gfx {

struct image_rgba {
    static constexpr uint32 channels = 4;

    uint32 width  = 0;
    uint32 height = 0;
    std::vector<uint8> pixels;

    [[nodiscard]] auto empty() const -> bool {
        return width == 0 || height == 0;
    }

    [[nodiscard]] auto is_consistent() const -> bool {
        return pixels.size() ==
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels;
    }
};

enum class png_error : uint8 {
    empty_image,
    size_mismatch,
    encoder_failed,
};

[[nodiscard]] auto encode_png(const image_rgba& image)
    -> std::expected<std::vector<uint8>, png_error>;

[[nodiscard]] auto shrunk_to_fit(const image_rgba& image, uint32 longest_side) -> image_rgba;

[[nodiscard]] auto tiled(std::span<const image_rgba> tiles, uint32 columns) -> image_rgba;

}  // namespace vw::gfx
