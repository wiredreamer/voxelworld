module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

namespace {

class tuft_dice {
public:
    explicit tuft_dice(uint8 form) : state_{0xA0761D6478BD642FULL ^ (static_cast<uint64>(form) * 0x9E3779B97F4A7C15ULL)} {}

    auto roll(int32 low, int32 high) -> int32 {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64 z = state_;
        z        = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z        = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        return low + static_cast<int32>(z % static_cast<uint64>(high - low + 1));
    }

private:
    uint64 state_;
};

auto shade_of(voxel look, int32 step) -> voxel {
    for (const auto& group : voxels::groups) {
        const int32 first = group.first.value;
        const int32 last  = first + group.count - 1;
        if (look.value >= first && look.value <= last) {
            return voxel{static_cast<uint8>(std::clamp(look.value + step, first, last))};
        }
    }
    return look;
}

constexpr std::array<voxel, flower_colors> petal_colors{
    voxels::red[7], voxels::amber[9], voxels::purple[7], voxels::blue[8], voxels::white,
};

constexpr std::array<voxel, flower_colors> heart_colors{
    voxels::amber[8], voxels::brown[4], voxels::amber[9], voxels::amber[8], voxels::amber[8],
};

// см. docs/world.md#цветы
auto grow_flowers(
    asset::model_registry& models, uint8 form, voxel look
) -> std::shared_ptr<asset::model> {
    constexpr int32 side  = grass_tuft_footprint;
    constexpr int32 slots = 2;
    constexpr int32 stem  = grass_stalk_width;
    constexpr int32 crown = 6;

    auto bed = models.create_unnamed(side, grass_tuft_max_height, side);
    tuft_dice dice{form};

    const voxel stalk = shade_of(look, -1);
    const voxel petal = petal_colors[flower_color_of(form)];
    const voxel heart = heart_colors[flower_color_of(form)];

    const auto put = [&bed](int32 x, int32 y, int32 z, voxel v) {
        if (x >= 0 && x < side && z >= 0 && z < side && y >= 0 && y < grass_tuft_max_height) {
            bed->set_voxel(x, y, z, v);
        }
    };

    std::array<int32, slots * slots> order{};
    std::iota(order.begin(), order.end(), 0);
    for (int32 i = static_cast<int32>(order.size()) - 1; i > 0; --i) {
        std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(dice.roll(0, i))]);
    }

    for (int32 n = 0; n < flower_count_of(form); ++n) {
        const int32 sx = order[static_cast<std::size_t>(n)] / slots;
        const int32 sz = order[static_cast<std::size_t>(n)] % slots;

        const int32 half = side / slots;
        const int32 x    = (sx * half) + dice.roll(0, half - crown);
        const int32 z    = (sz * half) + dice.roll(0, half - crown);
        const int32 h    = dice.roll(flower_min_height, flower_max_height);
        const int32 top  = h - 1;
        const int32 core = (crown - stem) / 2;

        for (int32 y = 0; y < top; ++y) {
            for (int32 dx = 0; dx < stem; ++dx) {
                for (int32 dz = 0; dz < stem; ++dz) {
                    put(x + core + dx, y, z + core + dz, stalk);
                }
            }
        }

        for (int32 dx = 0; dx < crown; ++dx) {
            for (int32 dz = 0; dz < crown; ++dz) {
                const int32 off_x = std::abs((2 * dx) - (crown - 1));
                const int32 off_z = std::abs((2 * dz) - (crown - 1));
                if (off_x + off_z > crown + 1) {
                    continue;
                }
                const bool middle = dx >= core && dx < core + stem && dz >= core && dz < core + stem;
                put(x + dx, top, z + dz, middle ? heart : petal);
            }
        }
    }

    return bed;
}

}  // namespace

// см. docs/world.md#колоски
auto grow_grass_tuft(
    asset::model_registry& models, uint8 form, voxel look
) -> std::shared_ptr<asset::model> {
    if (is_flower_form(form)) {
        return grow_flowers(models, form, look);
    }

    constexpr int32 side  = grass_tuft_footprint;
    constexpr int32 slots = 3;

    auto tuft = models.create_unnamed(side, grass_tuft_max_height, side);
    tuft_dice dice{form};

    const voxel root  = shade_of(look, -2);
    const voxel blade = shade_of(look, -1);
    const voxel tip   = look;
    const voxel ear   = shade_of(look, 1);

    const auto put = [&tuft](int32 x, int32 y, int32 z, voxel v) {
        if (x >= 0 && x < side && z >= 0 && z < side && y >= 0 && y < grass_tuft_max_height) {
            tuft->set_voxel(x, y, z, v);
        }
    };

    const int32 lowest = grass_tuft_min_height + (grass_height_class_of(form) * grass_tuft_height_step);
    const int32 tallest = std::min(lowest + grass_tuft_height_step - 1, grass_tuft_max_height);

    const auto slot_start = [](int32 slot) -> int32 { return (slot * side) / slots; };

    for (int32 sx = 0; sx < slots; ++sx) {
        for (int32 sz = 0; sz < slots; ++sz) {
            if (dice.roll(0, 4) >= 3) {
                continue;
            }

            constexpr int32 w = grass_stalk_width;

            const int32 x = dice.roll(slot_start(sx), slot_start(sx + 1) - w);
            const int32 z = dice.roll(slot_start(sz), slot_start(sz + 1) - w);
            const int32 h = dice.roll(lowest, tallest);

            const bool eared  = dice.roll(0, 3) != 0;
            const int32 ear_h = h >= 4 ? 2 : 1;

            for (int32 y = 0; y < h; ++y) {
                const bool in_ear = eared && y >= h - ear_h;
                const voxel v     = y == 0 ? root : (in_ear ? ear : (y * 2 >= h ? tip : blade));
                for (int32 dx = 0; dx < w; ++dx) {
                    for (int32 dz = 0; dz < w; ++dz) {
                        put(x + dx, y, z + dz, v);
                    }
                }
            }
        }
    }

    return tuft;
}

}  // namespace vw::ecs
