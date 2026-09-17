export module vw.world:grid.visibility;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

class world;

template <typename LinksAt, typename IsSky, typename StartsIn, typename Visit>
auto walk_visible_chunks(
    vec3i origin, vec3i lo, vec3i hi, LinksAt&& links_at, IsSky&& is_sky, StartsIn&& starts_in,
    Visit&& visit
) -> void {
    static const asset::chunk_pocket open_pocket = asset::chunk_pocket::wide_open();

    constexpr uint64 seen_bit = uint64{1} << 63;
    std::unordered_map<vec3i, uint64> queued;
    std::vector<std::pair<vec3i, int32>> pending;

    const auto pockets_of = [&](vec3i coord) -> std::span<const asset::chunk_pocket> {
        const auto* links = links_at(coord);
        if (links == nullptr) {
            return {&open_pocket, 1};
        }
        return links->pockets;
    };

    // см. docs/world.md#обход
    {
        const auto pockets = pockets_of(origin);
        uint64 mask        = seen_bit;

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

        queued[origin] = mask;
        visit(origin);
    }

    while (!pending.empty()) {
        const auto [coord, pocket_index] = pending.back();
        pending.pop_back();

        const auto pockets = pockets_of(coord);
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

            const auto* next_links = links_at(next);
            if (next_links == nullptr && !is_sky(next)) {
                continue;
            }

            const auto next_pockets = next_links == nullptr
                ? std::span<const asset::chunk_pocket>{&open_pocket, 1}
                : std::span<const asset::chunk_pocket>{next_links->pockets};

            auto& mask = queued[next];
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
    walk_visible_chunks(
        origin, origin - extent, origin + extent, std::forward<LinksAt>(links_at),
        [](vec3i) { return true; }, [](const asset::chunk_pocket&) { return true; },
        std::forward<Visit>(visit)
    );
}

}  // namespace vw::ecs
