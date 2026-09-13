export module vw.world:grid.visibility;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

class world;

// Направления в порядке, которым пользуется chunk_links: -X, +X, -Y, +Y, -Z, +Z.
// Противоположное направлению — его пара, то есть d ^ 1.
inline constexpr std::array<vec3i, 6> chunk_face_offsets{
    vec3i{-1, 0, 0}, vec3i{1, 0, 0}, vec3i{0, -1, 0},
    vec3i{0, 1, 0},  vec3i{0, 0, -1}, vec3i{0, 0, 1},
};

// Порядок, которым пользуются мешер и set_boundary_slice, — другой:
// +X, -X, +Y, -Y, +Z, -Z. Противоположное по-прежнему d ^ 1.
inline constexpr std::array<vec3i, 6> boundary_face_offsets{
    vec3i{1, 0, 0},  vec3i{-1, 0, 0}, vec3i{0, 1, 0},
    vec3i{0, -1, 0}, vec3i{0, 0, 1},  vec3i{0, 0, -1},
};

// Обход в ширину от чанка, в котором стоит наблюдатель, по карманам воздуха,
// смыкающимся через грани чанков. Взгляд входит в чанк через один карман и может
// выйти только через отверстия того же кармана, поэтому сплошная порода — или
// склон с тоннелями за ним — обход обрывает. Это и убирает из кадра целую систему
// пещер, когда на неё смотрят сверху.
//
// Консервативен там, где может себе это позволить: загруженный чанк с ещё
// неизвестной связностью считается полностью открытым, поэтому обход сообщает
// больше видимого, чем есть, а не меньше.
//
// Пустая ячейка — это либо небо, либо ничто. Небо открыто, в нём стоит камера. В
// пробел внутри объёма мира обход не заходит вовсе: видимой геометрии там нет, а
// считать его открытым значит позволить тоннелю, уходящему за край загруженного
// мира, увести обход под всё и вывести с другой стороны.
//
// Границы включающие.
//
// Координаты здесь — ячейки связности, а не чанки: чанк держит их
// chunk_links::cells_per_side по каждой оси. Целые чанки для обхода слишком
// грубы, см. примечание к chunk_links.
//
// links_at(vec3i) -> const asset::cell_links* для загруженных ячеек и null для
// остальных, is_sky(vec3i) -> bool, starts_in(const asset::chunk_pocket&) -> bool
// выбирает карман, который занимает наблюдатель, visit(vec3i) -> void.
template <typename LinksAt, typename IsSky, typename StartsIn, typename Visit>
auto walk_visible_chunks(
    vec3i origin, vec3i lo, vec3i hi, LinksAt&& links_at, IsSky&& is_sky, StartsIn&& starts_in,
    Visit&& visit
) -> void {
    constexpr int32 face_count = asset::chunk_pocket::face_count;

    // Ячейка без собственной связности — небо или ещё не смешенный чанк — это
    // один карман, открытый по всем граням.
    static const asset::chunk_pocket open_pocket = asset::chunk_pocket::wide_open();

    // Какие карманы какого чанка уже поставлены в очередь, плюс старший бит под
    // «уже сообщён». Чанк обходится по разу на карман, а не по разу на грань
    // входа: два кармана одного чанка — это два разных места.
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

    // Наблюдатель смотрит из того кармана, в котором стоит, и только из него.
    // Камера в открытом воздухе над склоном делит свою ячейку с тоннелями под
    // ним, и старт со всех карманов отдал бы ей всю сеть — что он и делал.
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

        // Внутри породы либо об этой ячейке ничего не известно: откатываемся ко
        // всем карманам — это сообщает лишнее, а не прячет что-то.
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

        for (int32 face = 0; face < face_count; ++face) {
            if (!pocket.touches(face)) {
                continue;
            }

            const vec3i next = coord + chunk_face_offsets[face];
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

            // Считается увиденным независимо от того, идёт ли взгляд дальше:
            // карман достаёт до этой грани, значит ближняя сторона соседа видна.
            // Стена — это ровно тот чанк, на котором обход встал, и его надо
            // нарисовать.
            auto& mask = queued[next];
            if ((mask & seen_bit) == 0) {
                mask |= seen_bit;
                visit(next);
            }

            // Дальше — только через карман, совпадающий с этим.
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

// Всё пустое считается небом. Для тестов и для вызывающих, под чьим обходом мира
// нет.
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
