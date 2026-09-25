export module vw.asset:model.light_field;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;

export namespace vw::asset {

class light_field {
public:
    static constexpr int32 side        = chunk_occupancy::side;
    static constexpr int32 page        = 8;
    static constexpr int32 pages_side  = side / page;
    static constexpr int32 page_count  = pages_side * pages_side * pages_side;
    static constexpr int32 page_voxels = page * page * page;
    static constexpr int32 page_bytes  = page_voxels / 2;

    using page_type = std::array<uint8, page_bytes>;

    // см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
    struct boundary_light {
        per_face<uint8> uniform{};
        per_face<std::vector<uint8>> packed{};

        std::array<std::array<uint8, side / 2>, shell_edge_count> edges{};
        std::array<uint8, shell_corner_count / 2> corners{};

        [[nodiscard]] static auto unpack(uint8 pair, int32 at) -> uint8 {
            return static_cast<uint8>((at % 2) == 0 ? (pair & 0xFU) : (pair >> 4));
        }

        static auto pack(uint8& pair, int32 at, uint8 level) -> void {
            pair = static_cast<uint8>(
                (at % 2) == 0 ? ((pair & 0xF0U) | level) : ((pair & 0x0FU) | (level << 4))
            );
        }

        [[nodiscard]] auto edge_level(vec3i step, int32 along) const -> uint8 {
            const auto& line = edges[static_cast<std::size_t>(shell_edge_index(step))];
            return unpack(line[static_cast<std::size_t>(along / 2)], along);
        }

        auto set_edge_level(vec3i step, int32 along, uint8 level) -> void {
            auto& line = edges[static_cast<std::size_t>(shell_edge_index(step))];
            pack(line[static_cast<std::size_t>(along / 2)], along, level);
        }

        [[nodiscard]] auto corner_level(vec3i step) const -> uint8 {
            const int32 at = shell_corner_index(step);
            return unpack(corners[static_cast<std::size_t>(at / 2)], at);
        }

        auto set_corner_level(vec3i step, uint8 level) -> void {
            const int32 at = shell_corner_index(step);
            pack(corners[static_cast<std::size_t>(at / 2)], at, level);
        }

        [[nodiscard]] auto level_at(face_direction face, int32 a, int32 b) const -> uint8 {
            const auto& plane = packed[face];
            if (plane.empty()) {
                return uniform[face];
            }

            const int32 at   = (a * side) + b;
            const uint8 pair = plane[static_cast<std::size_t>(at / 2)];

            return static_cast<uint8>((at % 2) == 0 ? (pair & 0xFU) : (pair >> 4));
        }

        [[nodiscard]] auto bytes() const -> std::size_t {
            std::size_t total = 0;
            for (const auto& plane : packed) {
                total += plane.size();
            }
            return total;
        }

        auto operator==(const boundary_light&) const -> bool = default;
    };

    light_field() = default;

    light_field(uint8 level, boundary_light around)
        : uniform_{level}, around_{std::move(around)} {}

    light_field(std::vector<uint16> table, std::vector<page_type> pages,
                    boundary_light around)
        : table_{std::move(table)}, pages_{std::move(pages)}, around_{std::move(around)} {}

    [[nodiscard]] auto level_at(int32 x, int32 y, int32 z) const -> uint8 {
        if (table_.empty()) {
            return uniform_;
        }

        const uint16 entry =
            table_[static_cast<std::size_t>(page_index(x / page, y / page, z / page))];
        if ((entry & 1U) == 0) {
            return static_cast<uint8>(entry >> 1);
        }

        const page_type& packed = pages_[entry >> 1];
        const int32 at          = (x % page) + ((y % page) * page) + ((z % page) * page * page);
        const uint8 pair        = packed[static_cast<std::size_t>(at / 2)];

        return static_cast<uint8>((at % 2) == 0 ? (pair & 0xFU) : (pair >> 4));
    }

    [[nodiscard]] auto level_at(vec3i pos) const -> uint8 {
        return level_at(pos.x, pos.y, pos.z);
    }

    // см. docs/lod-plan.md#у-мешера-двадцать-шесть-соседей
    [[nodiscard]] auto level_around(int32 x, int32 y, int32 z) const -> uint8 {
        const auto beyond = [](int32 v) -> int32 { return v < 0 ? -1 : (v >= side ? 1 : 0); };

        const vec3i step{beyond(x), beyond(y), beyond(z)};

        switch (shell_span(step)) {
            case 0:
                return level_at(x, y, z);
            case 1: {
                const face_direction face = shell_face(step);
                const vec2i on_plane      = project_onto_face_plane(face, vec3i{x, y, z});
                return around_.level_at(face, on_plane.x, on_plane.y);
            }
            case 2: {
                const int32 free = shell_free_axis(step);
                return around_.edge_level(step, vec3i{x, y, z}[free]);
            }
            default:
                return around_.corner_level(step);
        }
    }

    [[nodiscard]] auto is_uniform() const -> bool {
        return table_.empty();
    }

    [[nodiscard]] auto uniform_level() const -> uint8 {
        return uniform_;
    }

    [[nodiscard]] auto mixed_pages() const -> int32 {
        return static_cast<int32>(pages_.size());
    }

    [[nodiscard]] auto bytes() const -> std::size_t {
        return (table_.size() * sizeof(uint16)) + (pages_.size() * sizeof(page_type)) +
               around_.bytes();
    }

    [[nodiscard]] static auto page_index(int32 px, int32 py, int32 pz) -> int32 {
        return px + (py * pages_side) + (pz * pages_side * pages_side);
    }

    auto operator==(const light_field&) const -> bool = default;

private:
    uint8 uniform_ = 0;
    std::vector<uint16> table_;
    std::vector<page_type> pages_;
    boundary_light around_;
};

}  // namespace vw::asset
