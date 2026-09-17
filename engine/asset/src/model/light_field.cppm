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

    struct boundary_light {
        per_face<uint8> uniform{};
        per_face<std::vector<uint8>> packed{};

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

    [[nodiscard]] auto level_around(int32 x, int32 y, int32 z) const -> uint8 {
        const bool inside = x >= 0 && y >= 0 && z >= 0 && x < side && y < side && z < side;
        if (inside) {
            return level_at(x, y, z);
        }

        const auto clamp = [](int32 v) -> int32 { return std::clamp(v, 0, side - 1); };

        if (x < 0 || x >= side) {
            return around_.level_at(
                x < 0 ? face_direction::neg_x : face_direction::pos_x, clamp(y), clamp(z)
            );
        }
        if (y < 0 || y >= side) {
            return around_.level_at(
                y < 0 ? face_direction::neg_y : face_direction::pos_y, clamp(x), clamp(z)
            );
        }
        return around_.level_at(
            z < 0 ? face_direction::neg_z : face_direction::pos_z, clamp(x), clamp(y)
        );
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
