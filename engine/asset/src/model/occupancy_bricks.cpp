module vw.asset;

import std;

import vw.core;

namespace vw::asset {

namespace {

constexpr int32 side = chunk_occupancy::side;

using cell_rows = std::array<uint64, static_cast<std::size_t>(side) * side>;

auto any_in_spans(uint64 bits, int32 span, int32 cells) -> uint64 {
    const uint64 span_mask = (uint64{1} << span) - 1;

    uint64 folded = 0;
    for (int32 cell = 0; cell < cells; ++cell) {
        if (((bits >> (cell * span)) & span_mask) != 0) {
            folded |= uint64{1} << cell;
        }
    }
    return folded;
}

auto fold_rows(const chunk_occupancy& solid, int32 level, cell_rows& folded) -> void {
    const int32 span  = 1 << level;
    const int32 cells = side >> level;

    for (int32 y = 0; y < cells; ++y) {
        for (int32 z = 0; z < cells; ++z) {
            uint64 any = 0;
            for (int32 dy = 0; dy < span; ++dy) {
                for (int32 dz = 0; dz < span; ++dz) {
                    any |= solid.row((y * span) + dy, (z * span) + dz);
                }
            }
            folded[static_cast<std::size_t>((y * side) + z)] = any_in_spans(any, span, cells);
        }
    }
}

}  // namespace

auto occupancy_brick_count(int32 level) -> std::size_t {
    const auto bricks = static_cast<std::size_t>((side / 2) >> level);
    return bricks * bricks * bricks;
}

auto pack_occupancy_bricks(const chunk_occupancy& solid, int32 level, std::span<uint8> out)
    -> void {
    if (out.size() != occupancy_brick_count(level)) {
        throw std::invalid_argument("occupancy bricks need a span of exactly one chunk");
    }

    cell_rows folded;
    const uint64* rows = solid.rows.data();
    if (level > 0) {
        fold_rows(solid, level, folded);
        rows = folded.data();
    }

    const int32 bricks = (side / 2) >> level;

    std::size_t at = 0;
    for (int32 z = 0; z < bricks; ++z) {
        for (int32 y = 0; y < bricks; ++y) {
            const uint64 near_low  = rows[(2 * y * side) + (2 * z)];
            const uint64 near_high = rows[(((2 * y) + 1) * side) + (2 * z)];
            const uint64 far_low   = rows[(2 * y * side) + (2 * z) + 1];
            const uint64 far_high  = rows[(((2 * y) + 1) * side) + (2 * z) + 1];

            if ((near_low | near_high | far_low | far_high) == 0) {
                std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(at), bricks, uint8{0});
                at += static_cast<std::size_t>(bricks);
                continue;
            }

            for (int32 x = 0; x < bricks; ++x) {
                const int32 shift = 2 * x;
                const uint64 brick = ((near_low >> shift) & 3U) |
                                     (((near_high >> shift) & 3U) << 2) |
                                     (((far_low >> shift) & 3U) << 4) |
                                     (((far_high >> shift) & 3U) << 6);
                out[at++] = static_cast<uint8>(brick);
            }
        }
    }
}

}  // namespace vw::asset
