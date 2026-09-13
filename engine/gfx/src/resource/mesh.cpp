module;

#include <cstddef>

module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {


// Вдоль каких мировых осей идут две касательные каждой грани. Те же две таблицы
// несут шейдеры: прямоугольник, упакованный одной стороной и распакованный другой,
// обязан одинаково понимать, какая протяжённость какая.
static constexpr std::array<int32, 6> tangent_u_axis = {2, 2, 0, 0, 0, 0};
static constexpr std::array<int32, 6> tangent_v_axis = {1, 1, 2, 2, 1, 1};

auto quad::pack(
    vec3i min_pos,
    vec3i max_pos,
    uint8 normal_id,
    block_slot slot,
    uint8 corners_ao,
    uint8 corners_convex,
    uint16 corners_sky,
    uint16 corners_block
) -> quad {
    const int32 u_axis = tangent_u_axis[normal_id];
    const int32 v_axis = tangent_v_axis[normal_id];

    // На единицу меньше числа ячеек, поэтому полные 128 всё ещё влезают в семь
    // бит. Протяжённость вдоль оси грани всегда в одну ячейку и не хранится.
    const auto span_u = static_cast<uint32>(max_pos[u_axis] - min_pos[u_axis] - 1);
    const auto span_v = static_cast<uint32>(max_pos[v_axis] - min_pos[v_axis] - 1);

    quad q;
    q.data0 =                                               //
        (static_cast<uint32>(min_pos.x) & 0x7Fu) |          //
        ((static_cast<uint32>(min_pos.y) & 0x7Fu) << 7) |   //
        ((static_cast<uint32>(min_pos.z) & 0x7Fu) << 14) |  //
        ((static_cast<uint32>(normal_id) & 0x7u) << 21) |   //
        (static_cast<uint32>(corners_ao) << 24);

    q.data1 =                                               //
        (span_u & 0x7Fu) |                                  //
        ((span_v & 0x7Fu) << 7) |                           //
        (static_cast<uint32>(slot.value) << 14) |            //
        (static_cast<uint32>(corners_convex) << 24);

    // Оба канала в одном слове, небо в младшей половине. Старшая была свободна —
    // data2 держало шестнадцать бит и больше ничего, — поэтому второй канал не
    // стоил ни смены формата, ни лишнего вершинного атрибута, ни байта геометрии.
    q.data2 = static_cast<uint32>(corners_sky) | (static_cast<uint32>(corners_block) << 16);

    return q;
}

auto quad::get_binding_descriptions() -> std::vector<vk::VertexInputBindingDescription> {
    std::vector binding_descriptions = {
        vk::VertexInputBindingDescription{
            .binding   = 0,
            .stride    = sizeof(uint32),
            .inputRate = vk::VertexInputRate::eInstance,
        },
    };
    return binding_descriptions;
}

auto quad::get_attribute_descriptions() -> std::vector<vk::VertexInputAttributeDescription> {
    std::vector attribute_descriptions = {
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = vk::Format::eR32Uint,
            .offset   = 0,
        },
    };
    return attribute_descriptions;
}


static constexpr std::array<vec3i, 6> ao_normal = {
    vec3i{1, 0, 0},
    vec3i{-1, 0, 0},
    vec3i{0, 1, 0},
    vec3i{0, -1, 0},
    vec3i{0, 0, 1},
    vec3i{0, 0, -1},
};

static constexpr std::array<vec3i, 6> ao_tangent_u = {
    vec3i{0, 0, 1},
    vec3i{0, 0, 1},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
    vec3i{1, 0, 0},
};

static constexpr std::array<vec3i, 6> ao_tangent_v = {
    vec3i{0, 1, 0},
    vec3i{0, 1, 0},
    vec3i{0, 0, 1},
    vec3i{0, 0, 1},
    vec3i{0, 1, 0},
    vec3i{0, 1, 0},
};


namespace detail {

face_axis_mapping::face_axis_mapping(
    mesh_source src, int face_dir
)
    : face_direction(face_dir), voxel_scale(src.voxels.voxel_scale()) {
    switch (face_dir / 2) {
        case 0:
            width  = src.voxels.depth();
            height = src.voxels.height();
            depth  = src.voxels.width();
            break;
        case 1:
            width  = src.voxels.width();
            height = src.voxels.depth();
            depth  = src.voxels.height();
            break;
        default:
            width  = src.voxels.width();
            height = src.voxels.height();
            depth  = src.voxels.depth();
            break;
    }
}

[[nodiscard]] auto face_axis_mapping::to_model_coords(
    int u, int v, int layer
) const -> std::tuple<int, int, int> {
    int d = (face_direction % 2 == 0) ? layer : depth - 1 - layer;
    switch (face_direction / 2) {
        case 0:
            return {d, v, u};
        case 1:
            return {u, d, v};
        default:
            return {u, v, d};
    }
}

[[nodiscard]] auto face_axis_mapping::to_local_min_max(
    int u, int v, int w, int h, int layer
) const -> std::pair<vec3i, vec3i> {
    int d_lo = (face_direction % 2 == 0) ? layer : depth - 1 - layer;
    int d_hi = d_lo + 1;
    int u_hi = u + w;
    int v_hi = v + h;

    switch (face_direction / 2) {
        case 0:
            return {{d_lo, v, u}, {d_hi, v_hi, u_hi}};
        case 1:
            return {{u, d_lo, v}, {u_hi, d_hi, v_hi}};
        default:
            return {{u, v, d_lo}, {u_hi, v_hi, d_hi}};
    }
}

auto is_solid_at(
    mesh_source src, vec3i p
) -> bool {
    const bool ox = p.x < 0 || p.x >= src.voxels.width();
    const bool oy = p.y < 0 || p.y >= src.voxels.height();
    const bool oz = p.z < 0 || p.z >= src.voxels.depth();

    if (!ox && !oy && !oz) {
        return !src.voxels.is_empty(p.x, p.y, p.z);
    }
    if (static_cast<int>(ox) + static_cast<int>(oy) + static_cast<int>(oz) > 1) {
        return false;
    }

    if (p.x >= src.voxels.width() && src.has_boundary_slice(0)) {
        return src.is_boundary_solid(0, 0, p.y, p.z);
    }
    if (p.x < 0 && src.has_boundary_slice(1)) {
        return src.is_boundary_solid(1, 0, p.y, p.z);
    }
    if (p.y >= src.voxels.height() && src.has_boundary_slice(2)) {
        return src.is_boundary_solid(2, p.x, 0, p.z);
    }
    if (p.y < 0 && src.has_boundary_slice(3)) {
        return src.is_boundary_solid(3, p.x, 0, p.z);
    }
    if (p.z >= src.voxels.depth() && src.has_boundary_slice(4)) {
        return src.is_boundary_solid(4, p.x, p.y, 0);
    }
    if (p.z < 0 && src.has_boundary_slice(5)) {
        return src.is_boundary_solid(5, p.x, p.y, 0);
    }

    return false;
}

// Насколько угол грани заперт плоскостью перед ним.
//
// Классические три выборки: две ячейки через ребро от угла и одна по диагонали от
// него. Два сплошных ребра запирают угол независимо от того, что за ними, поэтому
// этот случай выписан отдельно, а не сосчитан, — именно это правило и не даёт
// прямому углу посереть.
//
// Досягаемость — одна ячейка, и это намеренный предел, а не недосмотр. Взвешенное
// ядро шириной в две ячейки было построено и замерено: оно действительно видит
// стену через траншею шириной в три вокселя и стоит десяти процентов квадов при
// жадном слиянии, двух третей секунды стриминга и чёткости каждого угла. Три
// выборки — ещё и то, что позволяет получить всю грань из трёх сдвинутых строк
// занятости ниже, куда и уходила бо́льшая часть того времени стриминга.
[[nodiscard]] auto corner_level(bool edge_a, bool edge_b, bool diagonal) -> uint8 {
    if (edge_a && edge_b) {
        return 3;
    }
    return static_cast<uint8>(edge_a) + static_cast<uint8>(edge_b) +
           static_cast<uint8>(diagonal);
}

// Зеркало is_solid_at, и его нельзя записать как отрицание. За пределами модели,
// когда спросить граничный срез не у кого, is_solid_at отвечает «не сплошное», и
// для затенения это безобидная ошибка: нет перекрывающего — ничего не темнеет.
// Выпуклость же прочтёт тот же ответ как «соседа здесь нет», а это нарисовало бы
// яркую кайму вокруг габаритной коробки каждой модели без срезов — то есть всего в
// редакторе. Неизвестное обязано читаться заполненным.
[[nodiscard]] auto is_open_at(mesh_source src, vec3i p) -> bool {
    const bool ox = p.x < 0 || p.x >= src.voxels.width();
    const bool oy = p.y < 0 || p.y >= src.voxels.height();
    const bool oz = p.z < 0 || p.z >= src.voxels.depth();

    if (!ox && !oy && !oz) {
        return src.voxels.is_empty(p.x, p.y, p.z);
    }
    if (static_cast<int>(ox) + static_cast<int>(oy) + static_cast<int>(oz) > 1) {
        return false;
    }

    if (p.x >= src.voxels.width()) {
        return src.has_boundary_slice(0) && !src.is_boundary_solid(0, 0, p.y, p.z);
    }
    if (p.x < 0) {
        return src.has_boundary_slice(1) && !src.is_boundary_solid(1, 0, p.y, p.z);
    }
    if (p.y >= src.voxels.height()) {
        return src.has_boundary_slice(2) && !src.is_boundary_solid(2, p.x, 0, p.z);
    }
    if (p.y < 0) {
        return src.has_boundary_slice(3) && !src.is_boundary_solid(3, p.x, 0, p.z);
    }
    if (p.z >= src.voxels.depth()) {
        return src.has_boundary_slice(4) && !src.is_boundary_solid(4, p.x, p.y, 0);
    }
    return src.has_boundary_slice(5) && !src.is_boundary_solid(5, p.x, p.y, 0);
}

// Насколько угол грани торчит из поверхности, которой принадлежит: те же три
// выборки, что у затенения, но считаются на отсутствие и в том слое, где стоит сам
// воксель грани, а не в слое перед ним. Отсутствие обоих рёбер — это угол-шип, и он
// выписан отдельно по той же причине, что и прямой угол.
[[nodiscard]] auto corner_open_level(bool open_a, bool open_b, bool open_diagonal) -> uint8 {
    if (open_a && open_b) {
        return 3;
    }
    return static_cast<uint8>(open_a) + static_cast<uint8>(open_b) +
           static_cast<uint8>(open_diagonal);
}

[[nodiscard]] auto compute_corner_convexity(
    mesh_source src, int x, int y, int z, int face
) -> uint8 {
    if (face != convex_face) {
        return 0;
    }

    const vec3i host = vec3i{x, y, z};
    const vec3i u    = ao_tangent_u[face];
    const vec3i v    = ao_tangent_v[face];

    const bool open_mu = is_open_at(src, host - u);
    const bool open_pu = is_open_at(src, host + u);
    const bool open_mv = is_open_at(src, host - v);
    const bool open_pv = is_open_at(src, host + v);

    const bool diag_c0 = is_open_at(src, host - u - v);
    const bool diag_c1 = is_open_at(src, host + u - v);
    const bool diag_c2 = is_open_at(src, host + u + v);
    const bool diag_c3 = is_open_at(src, host - u + v);

    const uint8 c0 = corner_open_level(open_mu, open_mv, diag_c0);
    const uint8 c1 = corner_open_level(open_pu, open_mv, diag_c1);
    const uint8 c2 = corner_open_level(open_pu, open_pv, diag_c2);
    const uint8 c3 = corner_open_level(open_mu, open_pv, diag_c3);

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

auto compute_corner_darkness(
    mesh_source src, int x, int y, int z, int face
) -> uint8 {
    const vec3i n = vec3i{x, y, z} + ao_normal[face];
    const vec3i u = ao_tangent_u[face];
    const vec3i v = ao_tangent_v[face];

    const bool edge_mu = is_solid_at(src, n - u);
    const bool edge_pu = is_solid_at(src, n + u);
    const bool edge_mv = is_solid_at(src, n - v);
    const bool edge_pv = is_solid_at(src, n + v);

    const bool diag_c0 = is_solid_at(src, n - u - v);
    const bool diag_c1 = is_solid_at(src, n + u - v);
    const bool diag_c2 = is_solid_at(src, n + u + v);
    const bool diag_c3 = is_solid_at(src, n - u + v);

    const uint8 c0 = corner_level(edge_mu, edge_mv, diag_c0);
    const uint8 c1 = corner_level(edge_pu, edge_mv, diag_c1);
    const uint8 c2 = corner_level(edge_pu, edge_pv, diag_c2);
    const uint8 c3 = corner_level(edge_mu, edge_pv, diag_c3);

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}


// Угол — это среднее по четырём ячейкам, касающимся его с освещённой стороны.
// Сплошные не учитываются, а не считаются тёмными: их темнота — это то, ради чего
// существует затенение, и учёт её дважды делает каждый внутренний угол чёрным.
// Ячейка прямо перед гранью всегда воздух — иначе грань не была бы видна, — поэтому
// в среднем всегда что-то есть.
//
// Деление на один, два или четыре — это сдвиг; на три — умножение. Четыре целых
// деления на грань мешер себе позволить не может, а count бывает только одним из
// этих значений.
[[nodiscard]] auto average_of(int32 sum, int32 count) -> int32 {
    switch (count) {
        case 1:
            return sum;
        case 2:
            return sum >> 1;
        case 4:
            return sum >> 2;
        default:
            return (sum * 21846) >> 16;
    }
}

// Бит i в open_bits — это ячейка (du, dv) = (i % 3 - 1, i / 3 - 1), выставленный
// там, где эта ячейка воздух. Бит 4, центр, выставлен всегда.
auto corners_from_patch(
    mesh_source src, vec3i n, vec3i u, vec3i v, uint32 open_bits
) -> corner_light {
    const auto* sky   = src.sky_light();
    const auto* block = src.block_light();

    // У чанка, освещённого насквозь одинаково — порода ниже пещер, воздух над
    // поверхностью, — во всех углах всех граней один и тот же уровень, и для неба
    // таковы четыре пятых чанков. Для света блоков таковы почти все: там темно
    // везде, где не стоит лампа.
    //
    // Отсутствие поля вовсе означает модель, которая не является чанком мира: над
    // ней ничего нет и ламп в ней нет, значит сверху открыто, а внутри темно.
    // Только эта пара и оставляет модель редактора похожей на саму себя.
    const auto flat = [](const vw::asset::light_field* field, uint16 absent) -> uint16 {
        if (field == nullptr) {
            return absent;
        }
        return static_cast<uint16>(static_cast<uint16>(field->uniform_level()) * 0x1111U);
    };

    const bool walk_sky   = sky != nullptr && !sky->is_uniform();
    const bool walk_block = block != nullptr && !block->is_uniform();

    if (!walk_sky && !walk_block) {
        return corner_light{.sky = flat(sky, 0xFFFFU), .block = flat(block, 0x0000U)};
    }

    // Четыре угла вместе называют шестнадцать ячеек, но различных среди них только
    // девять: центр принадлежит всем четырём, а каждая боковая — двум. Выборка
    // читается один раз, обходом через прибавление касательных, а не пересчётом
    // позиции на каждую ячейку, и в каждой ячейке читаются оба канала: какие ячейки
    // открыты и где они — это для обоих один и тот же вопрос.
    std::array<int32, 9> lit_sky{};
    std::array<int32, 9> lit_block{};
    std::array<int32, 9> open{};

    vec3i row = n - u - v;

    for (int32 dv = 0; dv < 3; ++dv) {
        vec3i cell = row;

        for (int32 du = 0; du < 3; ++du) {
            const auto slot = static_cast<std::size_t>((dv * 3) + du);

            open[slot] = static_cast<int32>((open_bits >> slot) & 1U);
            if (open[slot] != 0) {
                // level_around, а не level_at: грань на оболочке чанка читает
                // ячейку перед собой, а та принадлежит соседу. Зажатие обратно
                // внутрь попадает в сплошной воксель, которому грань принадлежит,
                // а его заливка оставляет нулём — и каждая наружная грань каждого
                // чанка выходит чёрной.
                if (walk_sky) {
                    lit_sky[slot] = sky->level_around(cell.x, cell.y, cell.z);
                }
                if (walk_block) {
                    lit_block[slot] = block->level_around(cell.x, cell.y, cell.z);
                }
            }

            cell = cell + u;
        }

        row = row + v;
    }

    const auto pack_corners = [&](const std::array<int32, 9>& lit) -> uint16 {
        const auto corner = [&](std::size_t a, std::size_t b, std::size_t c) -> uint16 {
            const int32 sum   = lit[4] + lit[a] + lit[b] + lit[c];
            const int32 count = 1 + open[a] + open[b] + open[c];
            return static_cast<uint16>(average_of(sum, count));
        };

        const uint16 c0 = corner(0, 1, 3);
        const uint16 c1 = corner(1, 2, 5);
        const uint16 c2 = corner(5, 7, 8);
        const uint16 c3 = corner(3, 6, 7);

        return static_cast<uint16>(c0 | (c1 << 4) | (c2 << 8) | (c3 << 12));
    };

    return corner_light{
        .sky   = walk_sky ? pack_corners(lit_sky) : flat(sky, 0xFFFFU),
        .block = walk_block ? pack_corners(lit_block) : flat(block, 0x0000U),
    };
}

// Медленный путь — для всего, у чего нет строк занятости: девять обращений к
// вокселям ради того, что битовый путь уже знает.
auto compute_corner_light(
    mesh_source src, int x, int y, int z, int face
) -> corner_light {
    const vec3i n = vec3i{x, y, z} + ao_normal[face];
    const vec3i u = ao_tangent_u[face];
    const vec3i v = ao_tangent_v[face];

    uint32 open_bits = 1U << 4;

    for (int32 dv = -1; dv <= 1; ++dv) {
        for (int32 du = -1; du <= 1; ++du) {
            const auto slot = static_cast<uint32>(((dv + 1) * 3) + (du + 1));
            if (slot == 4) {
                continue;
            }

            vec3i at = n;
            if (du < 0) {
                at = at - u;
            } else if (du > 0) {
                at = at + u;
            }
            if (dv < 0) {
                at = at - v;
            } else if (dv > 0) {
                at = at + v;
            }

            open_bits |= is_solid_at(src, at) ? 0U : (1U << slot);
        }
    }

    return corners_from_patch(src, n, u, v, open_bits);
}

// А это быстрый: плоскость перед гранью уже есть три строки бит занятости — те же
// три, что читает ядро затенения.
auto light_from_rows(
    mesh_source src, const layer_rows& rows, int32 u_at, int32 v_at, int x,
    int y, int z, int face
) -> corner_light {
    uint32 open_bits = 0;

    for (int32 dv = -1; dv <= 1; ++dv) {
        const uint64 row = rows.front[v_at + dv];
        for (int32 du = -1; du <= 1; ++du) {
            const auto slot = static_cast<uint32>(((dv + 1) * 3) + (du + 1));
            open_bits |= ((row >> (u_at + du)) & 1U) == 0 ? (1U << slot) : 0U;
        }
    }
    open_bits |= 1U << 4;

    return corners_from_patch(
        src, vec3i{x, y, z} + ao_normal[face], ao_tangent_u[face], ao_tangent_v[face],
        open_bits
    );
}

auto is_face_visible(
    mesh_source src, int x, int y, int z, int face_direction
) -> bool {
    static constexpr int dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int dz[6] = {0, 0, 0, 0, 1, -1};

    int nx = x + dx[face_direction];
    int ny = y + dy[face_direction];
    int nz = z + dz[face_direction];

    if (nx < 0 || nx >= src.voxels.width() || ny < 0 || ny >= src.voxels.height() || nz < 0 ||
        nz >= src.voxels.depth()) {
        if (src.has_boundary_slice(face_direction)) {
            return !src.is_boundary_solid(face_direction, x, y, z);
        }
        return true;
    }

    return src.voxels.is_empty(nx, ny, nz);
}

auto build_face_mask(
    mesh_generation_storage& storage,
    mesh_source src,
    const face_axis_mapping& axes,
    int face_direction,
    int layer,
    [[maybe_unused]] mesh_options opts
) -> void {
    constexpr int ps = vw::asset::model::page_size;

    auto idx = [&](int u, int v) -> std::size_t {
        return (static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height)) + static_cast<std::size_t>(v);
    };

    constexpr face_mask_cell empty_cell{block_index{}, 0};

    for (int u_block = 0; u_block < axes.width; u_block += ps) {
        int u_end = std::min(u_block + ps, axes.width);
        for (int v_block = 0; v_block < axes.height; v_block += ps) {
            int v_end = std::min(v_block + ps, axes.height);

            auto [pmx, pmy, pmz] = axes.to_model_coords(u_block, v_block, layer);
            auto pm              = src.voxels.get_page_mode(pmx / ps, pmy / ps, pmz / ps);

            if (pm == vw::asset::page_mode::empty) {
                for (int u = u_block; u < u_end; u++) {
                    for (int v = v_block; v < v_end; v++) {
                        storage.mask[idx(u, v)] = empty_cell;
                    }
                }
                continue;
            }

            if (pm == vw::asset::page_mode::uniform) {
                const auto fid = src.voxels.get_page_fill_index(pmx / ps, pmy / ps, pmz / ps);
                for (int u = u_block; u < u_end; u++) {
                    for (int v = v_block; v < v_end; v++) {
                        auto [mx, my, mz] = axes.to_model_coords(u, v, layer);
                        if (is_face_visible(src, mx, my, mz, face_direction)) {
                            storage.mask[idx(u, v)] = {
                                fid,
                                compute_corner_darkness(src, mx, my, mz, face_direction),
                                compute_corner_light(src, mx, my, mz, face_direction),
                                compute_corner_convexity(src, mx, my, mz, face_direction)
                            };
                        } else {
                            storage.mask[idx(u, v)] = empty_cell;
                        }
                    }
                }
                continue;
            }

            auto* page = src.voxels.get_page(pmx / ps, pmy / ps, pmz / ps);
            for (int u = u_block; u < u_end; u++) {
                for (int v = v_block; v < v_end; v++) {
                    auto [mx, my, mz] = axes.to_model_coords(u, v, layer);
                    const int lx      = mx % ps;
                    const int ly      = my % ps;
                    const int lz      = mz % ps;
                    auto& vx          = (*page)[lx + ly * ps + lz * ps * ps];
                    if (!vx.is_empty() && is_face_visible(src, mx, my, mz, face_direction)) {
                        storage.mask[idx(u, v)] = {
                            vx,
                            compute_corner_darkness(src, mx, my, mz, face_direction),
                            compute_corner_light(src, mx, my, mz, face_direction),
                            compute_corner_convexity(src, mx, my, mz, face_direction)
                        };
                    } else {
                        storage.mask[idx(u, v)] = empty_cell;
                    }
                }
            }
        }
    }
}

auto add_quad(
    std::vector<quad>& quads,
    int face_direction,
    vec3i min_pos,
    vec3i max_pos,
    block_slot slot,
    uint8 corner_ao,
    uint8 corner_convex,
    corner_light light
) -> void {
    // Из какого из двух углов каждая вершина в порядке обхода берёт свои
    // составляющие. Та же таблица живёт в voxel.vert и shadow.vert — шесть вершин
    // теперь разворачивает шейдер, и стороны обязаны совпадать.
    static constexpr uint8 winding_to_corner[6][4] = {
        {0, 1, 3, 2},  // +X
        {0, 2, 3, 1},  // -X
        {0, 1, 3, 2},  // +Y
        {0, 2, 3, 1},  // -Y
        {0, 2, 3, 1},  // +Z
        {1, 3, 2, 0},  // -Z
    };

    static constexpr uint8 corner_to_ao[4] = {0, 1, 3, 2};

    const auto normal_id = static_cast<uint8>(face_direction);

    const uint8 c_ao[4] = {
        static_cast<uint8>(corner_ao & 0x3u),
        static_cast<uint8>((corner_ao >> 2) & 0x3u),
        static_cast<uint8>((corner_ao >> 4) & 0x3u),
        static_cast<uint8>((corner_ao >> 6) & 0x3u),
    };

    // Двумя битами насквозь, а не сравнением с нулём. Выборка различает угол,
    // задетый одним диагональным блоком, и угол, закрытый двумя гранями, и вся
    // разница между тенью как глубиной и тенью как контуром — именно в этом.
    uint8 ao_winding     = 0;
    uint8 convex_winding = 0;
    uint16 sky_winding   = 0;
    uint16 block_winding = 0;
    for (int i = 0; i < 4; i++) {
        const uint8 corner_i = winding_to_corner[face_direction][i];
        const uint8 ao_i     = corner_to_ao[corner_i];
        ao_winding |= static_cast<uint8>(c_ao[ao_i] << (i * 2));

        // Небо и выпуклость проходят ту же перестановку, что и затенение, и обязаны
        // её проходить: шейдер читает все три из тех же четырёх углов той же
        // билинейной выборкой, и если одна придёт в порядке выборки, она смешает
        // другой прямоугольник, чем две остальные.
        //
        // Ошибку выпуклости здесь не ловит ни один тест, и это не пробел в тестах.
        // На +Y две таблицы складываются в тождество, а +Y — единственная грань,
        // для которой выпуклость считается, так что переставлять тут нечего.
        // Выписано всё равно: день, когда правило граней расширят, — не тот день,
        // чтобы об этом вспоминать.
        const auto level = static_cast<uint16>((light.sky >> (ao_i * 4)) & 0xFu);
        sky_winding |= static_cast<uint16>(level << (i * 4));

        const auto lamp = static_cast<uint16>((light.block >> (ao_i * 4)) & 0xFu);
        block_winding |= static_cast<uint16>(lamp << (i * 4));

        const auto out = static_cast<uint8>((corner_convex >> (ao_i * 2)) & 0x3u);
        convex_winding |= static_cast<uint8>(out << (i * 2));
    }

    quads.push_back(quad::pack(
        min_pos, max_pos, normal_id, slot, ao_winding, convex_winding, sky_winding,
        block_winding
    ));
}


// Соседняя плоскость в той ориентации, какая нужна этой грани. Для ±Y и ±Z
// хранимая плоскость уже разложена строками по u; ±X держит строки по y, поэтому
// она собирается по биту — а случается это на одном слое из шестидесяти четырёх.
auto boundary_row(
    mesh_source src, int face_direction, int v
) -> uint64 {
    if (!src.has_boundary_slice(face_direction)) {
        return 0;  // no neighbour: the far side is open, every face is visible
    }

    const auto& face = src.boundary_face(face_direction);

    if (face_direction / 2 == 0) {
        uint64 bits = 0;
        for (int z = 0; z < 64; ++z) {
            if (face.test(v, z)) {
                bits |= uint64{1} << z;
            }
        }
        return bits;
    }

    return face.rows[v];
}

auto build_layer_rows(
    mesh_source src,
    const vw::asset::chunk_occupancy& occupancy,
    const face_axis_mapping& axes,
    int face_direction,
    int layer,
    layer_rows& out
) -> bool {
    const int d    = (face_direction % 2 == 0) ? layer : axes.depth - 1 - layer;
    const int step = (face_direction % 2 == 0) ? 1 : -1;
    const int nd   = d + step;
    const bool inside = nd >= 0 && nd < 64;

    out.front_outside = !inside;

    uint64 any = 0;

    for (int v = 0; v < 64; ++v) {
        uint64 own  = 0;
        uint64 front = 0;

        switch (face_direction / 2) {
            case 0:  // +-X: rows of z at (y = v, x = layer plane)
                own   = occupancy.zrow(v, d);
                front = inside ? occupancy.zrow(v, nd) : boundary_row(src, face_direction, v);
                break;
            case 1:  // +-Y: rows of x at (y = layer plane, z = v)
                own   = occupancy.row(d, v);
                front = inside ? occupancy.row(nd, v) : boundary_row(src, face_direction, v);
                break;
            default:  // +-Z: rows of x at (y = v, z = layer plane)
                own   = occupancy.row(v, d);
                front = inside ? occupancy.row(v, nd) : boundary_row(src, face_direction, v);
                break;
        }

        out.front[v]   = front;
        out.own[v]     = own;
        out.visible[v] = own & ~front;
        any |= out.visible[v];
    }

    return any != 0;
}

// Затенение одной ячейки, прочитанное из трёх соседних битовых строк вместо
// восьми обращений к страничному объёму. Строки сдвинуты заранее, поэтому все
// восемь выборок вокруг ячейки — это бит u какой-нибудь из масок.
struct corner_samples {
    uint64 edge_mu = 0;
    uint64 edge_pu = 0;
    uint64 edge_mv = 0;
    uint64 edge_pv = 0;
    uint64 diag_c0 = 0;
    uint64 diag_c1 = 0;
    uint64 diag_c2 = 0;
    uint64 diag_c3 = 0;
};

auto samples_from_rows(uint64 row_mv, uint64 row, uint64 row_pv) -> corner_samples {
    return {
        .edge_mu = row << 1,
        .edge_pu = row >> 1,
        .edge_mv = row_mv,
        .edge_pv = row_pv,
        .diag_c0 = row_mv << 1,
        .diag_c1 = row_mv >> 1,
        .diag_c2 = row_pv >> 1,
        .diag_c3 = row_pv << 1,
    };
}

auto pack_corners(const corner_samples& s, int u) -> uint8 {
    const auto bit = [u](uint64 mask) -> bool { return ((mask >> u) & 1U) != 0; };

    const uint8 c0 = corner_level(bit(s.edge_mu), bit(s.edge_mv), bit(s.diag_c0));
    const uint8 c1 = corner_level(bit(s.edge_pu), bit(s.edge_mv), bit(s.diag_c1));
    const uint8 c2 = corner_level(bit(s.edge_pu), bit(s.edge_pv), bit(s.diag_c2));
    const uint8 c3 = corner_level(bit(s.edge_mu), bit(s.edge_pv), bit(s.diag_c3));

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

// Те же восемь выборок, прочитанные наоборот: со строк собственного слоя грани, а
// не слоя перед ней. Сброшенный бит — это отсутствующий сосед.
auto pack_corners_convex(const corner_samples& s, int u) -> uint8 {
    const auto open = [u](uint64 mask) -> bool { return ((mask >> u) & 1U) == 0; };

    const uint8 c0 = corner_open_level(open(s.edge_mu), open(s.edge_mv), open(s.diag_c0));
    const uint8 c1 = corner_open_level(open(s.edge_pu), open(s.edge_mv), open(s.diag_c1));
    const uint8 c2 = corner_open_level(open(s.edge_pu), open(s.edge_pv), open(s.diag_c2));
    const uint8 c3 = corner_open_level(open(s.edge_mu), open(s.edge_pv), open(s.diag_c3));

    return static_cast<uint8>(c0 | (c1 << 2) | (c2 << 4) | (c3 << 6));
}

auto emit_rect(
    mesh_generation_storage& storage,
    const face_axis_mapping& axes,
    int face_direction,
    int layer,
    int u_start,
    int v_start,
    int w,
    int h,
    const face_mask_cell& cell,
    const std::array<uint16, 256>& slots
) -> void {
    auto [min_pos, max_pos] = axes.to_local_min_max(u_start, v_start, w, h, layer);

    // Номер в наборе переводится в слот палитры здесь — один раз на выпущенный
    // прямоугольник, по строке, снятой с реестра один раз на меш.
    add_quad(
        storage.quads,
        face_direction,
        min_pos,
        max_pos,
        block_slot{slots[cell.index.value]},
        cell.corner_ao,
        cell.corner_convex,
        cell.light
    );
}

}  // namespace detail


auto simple_mesh_generator::generate_mesh_data(
    mesh_source src, const block_registry& registry, mesh_options opts
) -> mesh {
    std::vector<quad> quads;
    std::array<uint32, 6> face_counts{};

    // Грань — самый внешний цикл, поэтому квады выходят сгруппированными по
    // направлению, как их и так выдаёт жадный мешер. Сравнивающий тест смотрит на
    // множество граней, а не на порядок, а группировка нужна шейдеру отсева.
    for (int face = 0; face < 6; face++) {
        const auto before = quads.size();

        for (int x = 0; x < src.voxels.width(); x++) {
            for (int y = 0; y < src.voxels.height(); y++) {
                for (int z = 0; z < src.voxels.depth(); z++) {
                    if (voxel voxel_obj = src.voxels.get_voxel(x, y, z);
                        !voxel_obj.is_empty()) {
                        if (is_face_visible(src, x, y, z, face)) {
                            add_cube_face(
                                quads, src, x, y, z, face, voxel_obj.id, registry, opts
                            );
                        }
                    }
                }
            }
        }

        face_counts[static_cast<std::size_t>(face)] = static_cast<uint32>(quads.size() - before);
    }

    return mesh{.quads = std::move(quads), .face_counts = face_counts};
}

auto simple_mesh_generator::add_cube_face(
    std::vector<quad>& quads,
    mesh_source src,
    int x,
    int y,
    int z,
    int face_direction,
    block_id voxel_id,
    const block_registry& registry,
    [[maybe_unused]] mesh_options opts
) -> void {
    detail::add_quad(
        quads,
        face_direction,
        {x, y, z},
        {x + 1, y + 1, z + 1},
        registry.slot_of(voxel_id),
        detail::compute_corner_darkness(src, x, y, z, face_direction),
        detail::compute_corner_convexity(src, x, y, z, face_direction),
        detail::compute_corner_light(src, x, y, z, face_direction)
    );
}

auto simple_mesh_generator::is_face_visible(
    mesh_source src, int x, int y, int z, int face_direction
) -> bool {
    static constexpr int dx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int dy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int dz[6] = {0, 0, 0, 0, 1, -1};

    int nx = x + dx[face_direction];
    int ny = y + dy[face_direction];
    int nz = z + dz[face_direction];

    if (nx < 0 || nx >= src.voxels.width() || ny < 0 || ny >= src.voxels.height() || nz < 0 ||
        nz >= src.voxels.depth()) {
        return true;
    }

    return src.voxels.is_empty(nx, ny, nz);
}


auto strip_mesh_generator::generate_mesh_data(
    mesh_generation_storage& storage,
    mesh_source src,
    const block_registry& registry,
    mesh_options opts
) -> mesh {
    storage.clear();

    auto total    = src.voxels.width() * src.voxels.height() * src.voxels.depth();
    auto estimate = static_cast<std::size_t>(total / 4);

    if (storage.quads.capacity() < estimate) {
        storage.quads.reserve(estimate);
    }

    std::array<uint32, 6> face_counts{};

    for (int face_direction = 0; face_direction < 6; face_direction++) {
        const auto before = storage.quads.size();
        generate_face_quads(storage, src, face_direction, registry, opts);
        face_counts[static_cast<std::size_t>(face_direction)] =
            static_cast<uint32>(storage.quads.size() - before);
    }

    // Перемещается, а не копируется: хранилище существует ради переиспользования, а
    // копирование его обратно наружу стоило аллокации и memcpy всего меша на чанк.
    // Ёмкость восстанавливает reserve в начале следующего вызова.
    // Связность здесь не строится — эта ветка мешера её не считает.
    return mesh{std::move(storage.quads), face_counts, {}};
}

auto strip_mesh_generator::merge_and_emit_strips(
    mesh_generation_storage& storage,
    mesh_source src,
    const detail::face_axis_mapping& axes,
    int face_direction,
    int layer,
    const block_registry& registry,
    [[maybe_unused]] mesh_options opts
) -> void {
    const auto& slots = registry.slot_row(src.voxels.category());

    auto idx = [&](int u, int v) -> std::size_t {
        return static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height) + static_cast<std::size_t>(v);
    };

    for (int v = 0; v < axes.height; v++) {
        int u = 0;
        while (u < axes.width) {
            face_mask_cell cell = storage.mask[idx(u, v)];
            if (cell.is_empty()) {
                u++;
                continue;
            }

            int strip_start = u;
            u++;
            while (u < axes.width && storage.mask[idx(u, v)] == cell) {
                u++;
            }
            int w = u - strip_start;

            detail::emit_rect(
                storage, axes, face_direction, layer, strip_start, v, w, 1, cell, slots
            );
        }
    }
}

auto strip_mesh_generator::generate_face_quads(
    mesh_generation_storage& storage,
    mesh_source src,
    int face_direction,
    const block_registry& registry,
    mesh_options opts
) -> void {
    detail::face_axis_mapping axes(src, face_direction);
    constexpr int ps = vw::asset::model::page_size;

    auto mask_size = static_cast<std::size_t>(axes.width) * static_cast<std::size_t>(axes.height);
    storage.mask.resize(mask_size);

    const int depth_pages = (axes.depth + ps - 1) / ps;
    const int u_pages     = (axes.width + ps - 1) / ps;
    const int v_pages     = (axes.height + ps - 1) / ps;

    storage.depth_has_pages.resize(depth_pages);
    std::ranges::fill(storage.depth_has_pages, false);
    for (int pd = 0; pd < depth_pages; pd++) {
        for (int pu = 0; pu < u_pages && !storage.depth_has_pages[pd]; pu++) {
            for (int pv = 0; pv < v_pages && !storage.depth_has_pages[pd]; pv++) {
                auto [mx, my, mz] = axes.to_model_coords(pu * ps, pv * ps, pd * ps);
                if (src.voxels.get_page_mode(mx / ps, my / ps, mz / ps) != vw::asset::page_mode::empty) {
                    storage.depth_has_pages[pd] = true;
                }
            }
        }
    }

    for (int layer = 0; layer < axes.depth; layer++) {
        if (!storage.depth_has_pages[layer / ps]) {
            layer = ((layer / ps) + 1) * ps - 1;
            continue;
        }

        detail::build_face_mask(storage, src, axes, face_direction, layer, opts);

        bool has_faces = false;
        for (std::size_t i = 0; i < mask_size && !has_faces; i++) {
            has_faces = !storage.mask[i].is_empty();
        }
        if (!has_faces)
            continue;

        merge_and_emit_strips(storage, src, axes, face_direction, layer, registry, opts);
    }
}


auto greedy_mesh_generator::generate_mesh_data(
    mesh_generation_storage& storage,
    mesh_source src,
    const block_registry& registry,
    mesh_options opts
) -> mesh {
    storage.clear();

    // Раньше здесь резервировалась четверть объёма чанка — 768 КБ на меш, занятые
    // независимо от того, есть ли у чанка геометрия, и уезжающие в каждый готовый
    // меш, потому что вектор перемещается наружу. Рост от маленького числа стоит
    // одного-двух удвоений и по замерам быстрее. Оценка по предыдущему чанку
    // пробовалась и хуже: один поверхностный чанк ставит высокую планку, которую
    // наследует каждый пустой чанк за ним.
    constexpr std::size_t estimate = 4096;

    if (storage.quads.capacity() < estimate) {
        storage.quads.reserve(estimate);
    }

    // Строится один раз на весь меш и читается всеми шестью направлениями. Модели,
    // не являющиеся 64-кубами (из Sculptor), уходят на путь по ячейкам.
    if (!storage.occupancy) {
        storage.occupancy = std::make_unique<vw::asset::chunk_occupancy>();
    }
    storage.occupancy_valid = src.voxels.build_occupancy(*storage.occupancy);

    std::array<uint32, 6> face_counts{};

    for (int face_direction = 0; face_direction < 6; face_direction++) {
        const auto before = storage.quads.size();
        generate_face_quads(storage, src, face_direction, registry, opts);
        face_counts[static_cast<std::size_t>(face_direction)] =
            static_cast<uint32>(storage.quads.size() - before);
    }

    // У моделей, не являющихся 64-кубами, нет ни занятости, ни чанковой координаты,
    // поэтому в обходе связности они не участвуют никогда. Оставленные пустыми, они
    // читались бы как «запечатанные»; один карман, открытый по всем граням, не
    // скрывает ничего. То же подставляется, когда обход выключен и связей никто не
    // просил.
    vw::asset::chunk_links links;
    if (opts.build_links && storage.occupancy_valid) {
        links = vw::asset::build_chunk_links(*storage.occupancy, storage.link_scratch);
    } else {
        for (auto& cell : links.cells) {
            cell.pockets.assign(1, vw::asset::chunk_pocket::wide_open());
        }
    }

    // Перемещается, а не копируется: хранилище существует ради переиспользования, а
    // копирование его обратно наружу стоило аллокации и memcpy всего меша на чанк.
    // Ёмкость восстанавливает reserve в начале следующего вызова.
    return mesh{std::move(storage.quads), face_counts, std::move(links)};
}

auto greedy_mesh_generator::merge_and_emit_rects_bits(
    mesh_generation_storage& storage,
    const detail::face_axis_mapping& axes,
    int face_direction,
    int layer,
    detail::layer_rows& rows,
    const std::array<uint16, 256>& slots
) -> void {
    // Построчно по u, поэтому расширение серии идёт по непрерывной памяти.
    auto idx = [&](int u, int v) -> std::size_t {
        return (static_cast<std::size_t>(v) * static_cast<std::size_t>(axes.width)) +
               static_cast<std::size_t>(u);
    };

    const auto keys_match = [&](int v, int u, int w, const face_mask_cell& key) -> bool {
        for (int du = 0; du < w; ++du) {
            if (storage.mask[idx(u + du, v)] != key) {
                return false;
            }
        }
        return true;
    };

    for (int v = 0; v < axes.height; ++v) {
        uint64 row = rows.visible[v];

        while (row != 0) {
            const int u = std::countr_zero(row);
            const face_mask_cell key = storage.mask[idx(u, v)];

            int w = 1;
            while (u + w < axes.width && ((row >> (u + w)) & 1U) != 0 &&
                   storage.mask[idx(u + w, v)] == key) {
                ++w;
            }

            const uint64 span =
                (w == 64) ? ~uint64{0} : (((uint64{1} << w) - 1) << u);

            int h = 1;
            while (v + h < axes.height && (rows.visible[v + h] & span) == span &&
                   keys_match(v + h, u, w, key)) {
                rows.visible[v + h] &= ~span;
                ++h;
            }

            row &= ~span;
            detail::emit_rect(storage, axes, face_direction, layer, u, v, w, h, key, slots);
        }

        rows.visible[v] = 0;
    }
}

auto greedy_mesh_generator::merge_and_emit_rects(
    mesh_generation_storage& storage,
    mesh_source src,
    const detail::face_axis_mapping& axes,
    int face_direction,
    int layer,
    const block_registry& registry,
    [[maybe_unused]] mesh_options opts
) -> void {
    const auto& slots = registry.slot_row(src.voxels.category());

    auto idx = [&](int u, int v) -> std::size_t {
        return static_cast<std::size_t>(u) * static_cast<std::size_t>(axes.height) + static_cast<std::size_t>(v);
    };

    face_mask_cell empty_cell{block_index{}, 0};

    for (int v = 0; v < axes.height; v++) {
        for (int u = 0; u < axes.width; u++) {
            face_mask_cell cell = storage.mask[idx(u, v)];
            if (cell.is_empty())
                continue;

            int w = 1;
            while (u + w < axes.width && storage.mask[idx(u + w, v)] == cell) {
                w++;
            }

            int h = 1;
            while (v + h < axes.height) {
                bool row_ok = true;
                for (int du = 0; du < w; du++) {
                    if (storage.mask[idx(u + du, v + h)] != cell) {
                        row_ok = false;
                        break;
                    }
                }
                if (!row_ok)
                    break;
                h++;
            }

            for (int dv = 0; dv < h; dv++) {
                for (int du = 0; du < w; du++) {
                    storage.mask[idx(u + du, v + dv)] = empty_cell;
                }
            }

            detail::emit_rect(storage, axes, face_direction, layer, u, v, w, h, cell, slots);
        }
    }
}

auto greedy_mesh_generator::generate_face_quads(
    mesh_generation_storage& storage,
    mesh_source src,
    int face_direction,
    const block_registry& registry,
    mesh_options opts
) -> void {
    // Строка слотов снимается один раз на грань: набор у модели один, и таблица
    // до самого выпуска квадов больше не нужна.
    const auto& slots = registry.slot_row(src.voxels.category());

    detail::face_axis_mapping axes(src, face_direction);
    constexpr int ps = vw::asset::model::page_size;

    auto mask_size = static_cast<std::size_t>(axes.width) * static_cast<std::size_t>(axes.height);
    storage.mask.resize(mask_size);

    int depth_pages = (axes.depth + ps - 1) / ps;
    int u_pages     = (axes.width + ps - 1) / ps;
    int v_pages     = (axes.height + ps - 1) / ps;

    storage.depth_has_pages.resize(depth_pages);
    std::ranges::fill(storage.depth_has_pages, false);
    for (int pd = 0; pd < depth_pages; pd++) {
        for (int pu = 0; pu < u_pages && !storage.depth_has_pages[pd]; pu++) {
            for (int pv = 0; pv < v_pages && !storage.depth_has_pages[pd]; pv++) {
                auto [mx, my, mz] = axes.to_model_coords(pu * ps, pv * ps, pd * ps);
                if (src.voxels.get_page_mode(mx / ps, my / ps, mz / ps) != vw::asset::page_mode::empty) {
                    storage.depth_has_pages[pd] = true;
                }
            }
        }
    }

    auto idx = [&](int u, int v) -> std::size_t {
        return (static_cast<std::size_t>(v) * static_cast<std::size_t>(axes.width)) +
               static_cast<std::size_t>(u);
    };

    detail::layer_rows rows;

    for (int layer = 0; layer < axes.depth; layer++) {
        if (!storage.depth_has_pages[layer / ps]) {
            layer = ((layer / ps) + 1) * ps - 1;
            continue;
        }

        if (storage.occupancy_valid) {
            // Видимость целой строки из 64 ячеек — это один and-not, а пустой слой
            // распознаётся вовсе без касания маски. Пишутся только те ячейки, что
            // действительно несут грань.
            if (!detail::build_layer_rows(
                    src, *storage.occupancy, axes, face_direction, layer, rows
                )) {
                continue;
            }

            for (int v = 0; v < axes.height; v++) {
                uint64 bits = rows.visible[v];
                if (bits == 0) {
                    continue;
                }

                // Выборки затенения стоят на ячейку в стороне по u и v, поэтому
                // ячейка на краю слоя дотягивается до соседнего чанка и остаётся на
                // скалярном пути, который умеет туда спросить. То же и когда
                // выбираемая плоскость целиком вне чанка.
                const bool interior_v = v > 0 && v + 1 < axes.height;
                const bool bit_ao     = interior_v && !rows.front_outside;

                const auto samples = detail::samples_from_rows(
                    interior_v ? rows.front[v - 1] : 0,
                    rows.front[v],
                    interior_v ? rows.front[v + 1] : 0
                );

                // Выпуклость читает тот же механизм сдвинутых строк, но по
                // собственному слою — тому, в котором стоят грани, а не тому, что
                // перед ними. Ей безразлично, вне ли чанка передняя плоскость, но
                // флаг внутренности она всё равно разделяет: платой будет только
                // откат на скалярный путь на граничном слое, а об одном флаге
                // рассуждать дешевле, чем о двух.
                const bool wants_convex = face_direction == detail::convex_face;

                const auto own_samples = detail::samples_from_rows(
                    interior_v && wants_convex ? rows.own[v - 1] : 0,
                    wants_convex ? rows.own[v] : 0,
                    interior_v && wants_convex ? rows.own[v + 1] : 0
                );

                while (bits != 0) {
                    const int u = std::countr_zero(bits);
                    bits &= bits - 1;

                    auto [mx, my, mz] = axes.to_model_coords(u, v, layer);

                    const bool interior = bit_ao && u > 0 && u + 1 < axes.width;

                    const uint8 dark   = interior ? detail::pack_corners(samples, u)
                                                  : detail::compute_corner_darkness(
                                                        src, mx, my, mz, face_direction
                                                    );
                    uint8 convex = 0;
                    if (wants_convex) {
                        convex = interior
                            ? detail::pack_corners_convex(own_samples, u)
                            : detail::compute_corner_convexity(src, mx, my, mz, face_direction);
                    }
                    const corner_light light =
                        interior
                            ? detail::light_from_rows(src, rows, u, v, mx, my, mz, face_direction)
                            : detail::compute_corner_light(src, mx, my, mz, face_direction);

                    storage.mask[idx(u, v)] = {
                        src.voxels.get_index(mx, my, mz), dark, light, convex
                    };
                }
            }

            merge_and_emit_rects_bits(storage, axes, face_direction, layer, rows, slots);
            continue;
        }

        detail::build_face_mask(storage, src, axes, face_direction, layer, opts);

        bool has_faces = false;
        for (std::size_t i = 0; i < mask_size && !has_faces; i++) {
            has_faces = !storage.mask[i].is_empty();
        }
        if (!has_faces)
            continue;

        merge_and_emit_rects(storage, src, axes, face_direction, layer, registry, opts);
    }
}

}  // namespace vw::gfx
