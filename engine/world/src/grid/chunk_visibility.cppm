export module vw.world:grid.visibility;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

class world;

// см. docs/world.md#обход
struct cell_lookup {
    const asset::cell_links* links = nullptr;
    bool placed                    = false;

    cell_lookup() = default;

    cell_lookup(const asset::cell_links* placed_links)
        : links{placed_links}, placed{placed_links != nullptr} {}

    [[nodiscard]] static auto placed_without_links() -> cell_lookup {
        cell_lookup lookup;
        lookup.placed = true;
        return lookup;
    }

    [[nodiscard]] static auto nothing_placed() -> cell_lookup {
        return cell_lookup{};
    }
};

// см. docs/world.md#обход
struct chunk_walk_scratch {
    std::vector<uint64> queued;
    std::vector<std::pair<vec3i, int32>> pending;
};

template <typename LinksAt, typename IsSky, typename StartsIn, typename Visit>
auto walk_visible_chunks(
    chunk_walk_scratch& scratch, vec3i origin, vec3i lo, vec3i hi, LinksAt&& links_at,
    IsSky&& is_sky, StartsIn&& starts_in, Visit&& visit
) -> void {
    static const asset::chunk_pocket open_pocket = asset::chunk_pocket::wide_open();

    constexpr uint64 seen_bit = uint64{1} << 63;

    const vec3i span_lo{std::min(lo.x, origin.x), std::min(lo.y, origin.y),
                        std::min(lo.z, origin.z)};
    const vec3i span_hi{std::max(hi.x, origin.x), std::max(hi.y, origin.y),
                        std::max(hi.z, origin.z)};

    const auto width  = static_cast<std::size_t>(span_hi.x - span_lo.x + 1);
    const auto height = static_cast<std::size_t>(span_hi.y - span_lo.y + 1);
    const auto depth  = static_cast<std::size_t>(span_hi.z - span_lo.z + 1);

    auto& queued  = scratch.queued;
    auto& pending = scratch.pending;

    queued.assign(width * height * depth, 0);
    pending.clear();

    const auto slot_of = [&](vec3i cell) -> std::size_t {
        return ((static_cast<std::size_t>(cell.y - span_lo.y) * depth) +
                static_cast<std::size_t>(cell.z - span_lo.z)) *
                   width +
               static_cast<std::size_t>(cell.x - span_lo.x);
    };

    const auto pockets_of = [&](const cell_lookup& cell) -> std::span<const asset::chunk_pocket> {
        if (cell.links == nullptr) {
            return {&open_pocket, 1};
        }
        return cell.links->pockets;
    };

    {
        const cell_lookup origin_cell = links_at(origin);
        const auto pockets            = pockets_of(origin_cell);
        uint64 mask                   = seen_bit;

        bool found = false;
        for (std::size_t i = 0; i < pockets.size(); ++i) {
            if (!starts_in(pockets[i])) {
                continue;
            }
            found = true;
            mask |= uint64{1} << i;
            pending.emplace_back(origin, static_cast<int32>(i));
        }

        if (!found) {
            for (std::size_t i = 0; i < pockets.size(); ++i) {
                mask |= uint64{1} << i;
                pending.emplace_back(origin, static_cast<int32>(i));
            }
        }

        queued[slot_of(origin)] = mask;
        visit(origin);
    }

    while (!pending.empty()) {
        const auto [coord, pocket_index] = pending.back();
        pending.pop_back();

        const auto pockets = pockets_of(links_at(coord));
        if (static_cast<std::size_t>(pocket_index) >= pockets.size()) {
            continue;
        }
        const auto& pocket = pockets[static_cast<std::size_t>(pocket_index)];

        for (const face_direction face : all_face_directions) {
            if (!pocket.touches(face)) {
                continue;
            }

            const vec3i next = coord + offset_of(face);
            if (next.x < lo.x || next.y < lo.y || next.z < lo.z || next.x > hi.x ||
                next.y > hi.y || next.z > hi.z) {
                continue;
            }

            const cell_lookup next_cell = links_at(next);
            if (next_cell.links == nullptr && !next_cell.placed && !is_sky(next)) {
                continue;
            }

            const auto next_pockets = pockets_of(next_cell);

            auto& mask = queued[slot_of(next)];
            if ((mask & seen_bit) == 0) {
                mask |= seen_bit;
                visit(next);
            }

            for (std::size_t i = 0; i < next_pockets.size(); ++i) {
                if (!pocket.meets(next_pockets[i], face)) {
                    continue;
                }
                if ((mask & (uint64{1} << i)) != 0) {
                    continue;
                }
                mask |= uint64{1} << i;
                pending.emplace_back(next, static_cast<int32>(i));
            }
        }
    }
}

template <typename LinksAt, typename Visit>
auto walk_visible_chunks(vec3i origin, int32 radius, LinksAt&& links_at, Visit&& visit) -> void {
    const vec3i extent{radius, radius, radius};
    chunk_walk_scratch scratch;
    walk_visible_chunks(
        scratch, origin, origin - extent, origin + extent, std::forward<LinksAt>(links_at),
        [](vec3i) { return true; }, [](const asset::chunk_pocket&) { return true; },
        std::forward<Visit>(visit)
    );
}

}  // namespace vw::ecs
