export module vw.world:light.column;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

using namespace ::vw::asset;

struct light_scratch {
    std::vector<uint8> levels;
    std::vector<uint64> solid;
    std::vector<uint64> sky;
    std::vector<int32> frontier;
    std::vector<int32> next;

    std::array<std::vector<int32>, 16> seeds;
};

class light_column {
public:
    static constexpr int32 side      = chunk_occupancy::side;
    static constexpr int32 apron     = 15;
    static constexpr int32 span      = side + (2 * apron);
    static constexpr int32 page      = 8;
    static constexpr uint8 max_level = 15;

    struct column_slice {
        std::span<const chunk_occupancy* const> occupancy;
        std::span<const model* const> models;
    };

    using neighbourhood = std::array<column_slice, 9>;

    explicit light_column(const neighbourhood& around)
        : light_column{around, emission_table{}, {}} {}

    light_column(const neighbourhood& around, const emission_table& emission,
                 light_scratch scratch);

    explicit light_column(std::span<const chunk_occupancy* const> chunks_bottom_up);

    [[nodiscard]] auto release() && -> light_scratch {
        return std::move(buffers_);
    }

    [[nodiscard]] auto height() const -> int32 {
        return height_;
    }

    [[nodiscard]] auto level_at(
        int32 middle_column_x,
        int32 y_from_bottom,
        int32 middle_column_z,
        light_channel channel = light_channel::sky
    ) const -> uint8 {
        const uint8 byte = buffers_.levels[static_cast<std::size_t>(
            index_(middle_column_x + apron, y_from_bottom, middle_column_z + apron)
        )];
        return static_cast<uint8>((byte >> shift_of(channel)) & 0x0FU);
    }

    [[nodiscard]] auto bake(int32 y_base, light_channel channel) const -> light_field;

private:
    [[nodiscard]] auto row_(int32 y, int32 z) const -> const uint8* {
        return &buffers_.levels[static_cast<std::size_t>(index_(apron, y, z + apron))];
    }

    [[nodiscard]] static auto index_(int32 x, int32 y, int32 z) -> int32 {
        return (((y * span) + z) * span) + x;
    }

    [[nodiscard]] auto solid_at_(int32 x, int32 y, int32 z) const -> bool;

    auto build_solid_(const neighbourhood& around) -> void;

    auto seed_sky_() -> void;

    auto seed_block_(const neighbourhood& around, const emission_table& emission) -> void;

    auto spread_(light_channel channel) -> void;

    int32 height_ = 0;
    light_scratch buffers_;
};

}  // namespace vw::ecs
