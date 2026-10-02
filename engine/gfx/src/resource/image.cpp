module;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

module vw.gfx;

import std;

import vw.core;

namespace vw::gfx {

namespace {

auto append_encoded(void* context, void* data, int32 size) -> void {
    auto* encoded     = static_cast<std::vector<uint8>*>(context);
    const auto* bytes = static_cast<const uint8*>(data);
    encoded->insert(encoded->end(), bytes, bytes + size);
}

}  // namespace

auto encode_png(const image_rgba& image) -> std::expected<std::vector<uint8>, png_error> {
    if (image.empty()) {
        return std::unexpected(png_error::empty_image);
    }
    if (!image.is_consistent()) {
        return std::unexpected(png_error::size_mismatch);
    }

    std::vector<uint8> encoded;

    const auto row_bytes = static_cast<int32>(image.width * image_rgba::channels);
    const int32 written  = stbi_write_png_to_func(
        &append_encoded, &encoded, static_cast<int32>(image.width),
        static_cast<int32>(image.height), static_cast<int32>(image_rgba::channels),
        image.pixels.data(), row_bytes
    );

    if (written == 0 || encoded.empty()) {
        return std::unexpected(png_error::encoder_failed);
    }
    return encoded;
}

auto tiled(std::span<const image_rgba> tiles, uint32 columns) -> image_rgba {
    if (tiles.empty() || columns == 0) {
        return {};
    }

    uint32 cell_width  = 0;
    uint32 cell_height = 0;
    for (const image_rgba& tile : tiles) {
        if (!tile.is_consistent()) {
            return {};
        }
        cell_width  = std::max(cell_width, tile.width);
        cell_height = std::max(cell_height, tile.height);
    }

    const auto count = static_cast<uint32>(tiles.size());
    const uint32 across = std::min(columns, count);
    const uint32 down   = (count + across - 1) / across;

    image_rgba sheet{
        .width  = cell_width * across,
        .height = cell_height * down,
        .pixels = {},
    };
    sheet.pixels.assign(
        static_cast<std::size_t>(sheet.width) * sheet.height * image_rgba::channels, uint8{0}
    );

    for (uint32 index = 0; index < count; ++index) {
        const image_rgba& tile = tiles[index];
        const uint32 left      = (index % across) * cell_width;
        const uint32 top       = (index / across) * cell_height;

        const std::size_t row_bytes = static_cast<std::size_t>(tile.width) * image_rgba::channels;
        for (uint32 y = 0; y < tile.height; ++y) {
            const std::size_t from = static_cast<std::size_t>(y) * row_bytes;
            const std::size_t to =
                (static_cast<std::size_t>(top + y) * sheet.width + left) * image_rgba::channels;
            std::copy_n(tile.pixels.begin() + static_cast<std::ptrdiff_t>(from), row_bytes,
                        sheet.pixels.begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
    return sheet;
}

auto shrunk_to_fit(const image_rgba& image, uint32 longest_side) -> image_rgba {
    const uint32 longest = std::max(image.width, image.height);
    if (image.empty() || !image.is_consistent() || longest_side == 0 || longest <= longest_side) {
        return image;
    }

    const auto scaled = [longest, longest_side](uint32 side) -> uint32 {
        const uint64 rounded =
            ((static_cast<uint64>(side) * longest_side) + (longest / 2)) / longest;
        return std::max<uint32>(1, static_cast<uint32>(rounded));
    };

    image_rgba small{
        .width  = scaled(image.width),
        .height = scaled(image.height),
        .pixels = {},
    };
    small.pixels.resize(
        static_cast<std::size_t>(small.width) * small.height * image_rgba::channels
    );

    const auto edge = [](uint32 index, uint32 from_count, uint32 to_count) -> uint32 {
        return static_cast<uint32>((static_cast<uint64>(index) * from_count) / to_count);
    };

    for (uint32 y = 0; y < small.height; ++y) {
        const uint32 top    = edge(y, image.height, small.height);
        const uint32 bottom = std::max(top + 1, edge(y + 1, image.height, small.height));

        for (uint32 x = 0; x < small.width; ++x) {
            const uint32 left  = edge(x, image.width, small.width);
            const uint32 right = std::max(left + 1, edge(x + 1, image.width, small.width));

            std::array<uint64, image_rgba::channels> sums{};
            for (uint32 from_y = top; from_y < bottom; ++from_y) {
                for (uint32 from_x = left; from_x < right; ++from_x) {
                    const std::size_t at =
                        (static_cast<std::size_t>(from_y) * image.width + from_x) *
                        image_rgba::channels;
                    for (uint32 channel = 0; channel < image_rgba::channels; ++channel) {
                        sums[channel] += image.pixels[at + channel];
                    }
                }
            }

            const uint64 covered = static_cast<uint64>(bottom - top) * (right - left);
            const std::size_t to =
                (static_cast<std::size_t>(y) * small.width + x) * image_rgba::channels;
            for (uint32 channel = 0; channel < image_rgba::channels; ++channel) {
                small.pixels[to + channel] =
                    static_cast<uint8>((sums[channel] + (covered / 2)) / covered);
            }
        }
    }

    return small;
}

}  // namespace vw::gfx
