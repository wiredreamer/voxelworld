export module vw.gfx:meshing;

import std;

import vw.core;
import vw.asset;
import vw.world;
import vulkan;

namespace vw::gfx::detail {
struct face_axis_mapping;
struct layer_rows;
}  // namespace vw::gfx::detail

export namespace vw::gfx {

// Один жадный прямоугольник, двенадцать байт. Ни вершинного, ни индексного
// буфера нет: отрисовка просит шесть вершин на квад, а вершинный шейдер достаёт
// угол из этой записи по gl_VertexIndex. Четыре вершины по восемь байт плюс
// шесть индексов по четыре выходили в пятьдесят шесть.
//
// data0: min.x[6:0] | min.y[13:7] | min.z[20:14] | normal_id[23:21]
//      | corners_ao[31:24]
// data1: span_u[6:0] | span_v[13:7] | voxel_slot[23:14]
//      | corners_convex[31:24]
// data2: corners_sky[15:0]
//
// Третье слово — цена небесного света. Четыре бита на угол не влезают в три
// оставшихся в data1, а урезать уровни до двух бит на угол значило бы выкинуть
// градиент, ради которого заливка и существует. Верхняя половина data2 отдана
// свету от блоков.
//
// Хранятся протяжённости по двум касательным осям, на единицу меньше числа
// ячеек, а не дальний угол: прямоугольник шириной 128 ячеек имеет дальний угол
// в 128, семь бит кончаются на 127, и старая упаковка заворачивала его в ноль на
// внешней грани модели. Протяжённость в 128 ячеек хранится как 127 и влезает
// ровно. Освободившиеся восемь бит заняла выпуклость.
//
// Слот вокселя занимает десять бит, а не восемь: личность вокселя — это категория и
// номер в ней, шестнадцать бит, и в запись она не влезает. Реестр раздаёт живым
// вокселям плотные номера, и сюда едет номер. Десять — это всё, что было свободно,
// и потолок каталога стоит там же.
//
// corners_ao: два бита на угол в порядке обхода, от 0 (открыт) до 3 (закрыт).
// corners_convex: два бита на угол в том же порядке, от 0 (вровень с соседями)
// до 3 (шип без обоих рёберных соседей). Только верхние грани.
// corners_sky: четыре бита на угол в том же порядке, от 0 (запечатан) до 15
// (под открытым небом).
//
// Затенение и выпуклость отвечают на две половины одного вопроса — сколько
// окружения видит угол — и пробовались одним знаковым числом по три бита на
// угол. Слияние, ради которого всё затевалось, отыграло 0,46% квадов и не
// больше. Две маски сохраняют обе силы и обе кривые настраиваемыми без
// перестроения меша, поэтому масок две.
//
// Выпуклость уже была здесь одним битом на угол и была убрана как дублирующая
// небесный свет. Она его не дублирует: под открытым небом свет всюду ровно
// пятнадцать, поэтому два плато на разной высоте выходят одного цвета и ступень
// между ними пропадает. Затенение тоже не покажет её — оно смотрит на слой перед
// гранью, а над верхней гранью этот слой по определению воздух.
struct quad {
    uint32 data0 = 0;
    uint32 data1 = 0;
    uint32 data2 = 0;

    quad() = default;

    [[nodiscard]] static auto pack(
        vec3i min_pos, vec3i max_pos, uint8 normal_id, voxel_slot slot, uint8 corners_ao,
        uint8 corners_convex, uint16 corners_sky, uint16 corners_block
    ) -> quad;

    // Через вершинный вход идёт только индекс инстанса; геометрия приходит из
    // storage-буфера.
    [[nodiscard]] static auto get_binding_descriptions()
        -> std::vector<vk::VertexInputBindingDescription>;

    [[nodiscard]] static auto get_attribute_descriptions()
        -> std::vector<vk::VertexInputAttributeDescription>;
};

struct mesh {
    std::vector<quad> quads;

    // Квады сгруппированы по направлению грани в порядке +X, -X, +Y, -Y, +Z, -Z,
    // потому что в этом порядке идёт мешер. Шесть длин ничего не стоят, а шейдер
    // отсева бросает по ним три отвёрнутых направления до всякой закраски.
    std::array<uint32, 6> face_counts{};

    // Какие грани чанка просматриваются в какие; снято с той же занятости, по
    // которой работает мешер. Переживает release_data: обход видимости нужен
    // долго после того, как геометрия ушла на устройство.
    vw::asset::chunk_links links;

    auto release_data() -> void {
        quads = {};
    }
};

struct mesh_options {
    // Связность читает только обход видимости, а он по умолчанию выключен.
    // Безусловное построение стоило десятой части всего мешинга.
    bool build_links = false;
};

// Модель и то, что знает о ней мир: плоскости соседей по швам и запечённый свет.
// У модели, которая чанком не является — а это всё, что открыто в редакторе, —
// чанковой половины нет вовсе, и мешер строит её как отдельно стоящую: за её
// границами ничего не известно, и известным оно не станет.
//
// Передаётся по значению: два указателя едут в регистрах, а по ссылке каждое
// обращение к вокселям стоило лишней загрузки. Замерено на advance — 568 против
// 549 мкс на чанк, три прогона на сборку, группы не пересекались.
struct mesh_source {
    const vw::asset::model& voxels;
    const vw::asset::chunk_volume* chunk = nullptr;

    [[nodiscard]] auto has_boundary_slice(int32 face_direction) const -> bool {
        return chunk != nullptr && chunk->has_boundary_slice(face_direction);
    }

    // Спрашивать вправе только тот, кому has_boundary_slice это подтвердил.
    [[nodiscard]] auto is_boundary_solid(int32 face_direction, int32 x, int32 y, int32 z) const
        -> bool {
        return chunk->is_boundary_solid(face_direction, x, y, z);
    }

    [[nodiscard]] auto boundary_face(int32 face_direction) const
        -> const vw::asset::face_occupancy& {
        return chunk->get_boundary_face(face_direction);
    }

    [[nodiscard]] auto sky_light() const -> const vw::asset::light_field* {
        return chunk != nullptr ? chunk->get_sky_light() : nullptr;
    }

    [[nodiscard]] auto block_light() const -> const vw::asset::light_field* {
        return chunk != nullptr ? chunk->get_block_light() : nullptr;
    }
};

class simple_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto add_cube_face(
        std::vector<quad>& quads,
        mesh_source src,
        int32 x,
        int32 y,
        int32 z,
        int32 face_direction,
        voxel voxel_id,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    [[nodiscard]]
    static auto is_face_visible(
        mesh_source src, int32 x, int32 y, int32 z,
        int32 face_direction
    ) -> bool;
};

// Два запечённых канала в четырёх углах одной грани, по четыре бита, в том же
// порядке, что corner_ao. Берутся вместе, потому что это одна и та же выборка:
// девять ячеек, одни и те же, различается только читаемое в них поле.
struct corner_light {
    uint16 sky   = 0;
    uint16 block = 0;

    [[nodiscard]] auto operator==(const corner_light&) const -> bool = default;
};

struct face_mask_cell {
    // Номер в наборе, а не идентификатор: набор у модели один, и собирать его на
    // каждый воксель маски незачем — это делается один раз при выпуске квада.
    voxel_index index;
    uint8 corner_ao;

    // Часть ключа слияния: две ячейки сливаются, только если свет совпал по всему
    // кругу — из-за этого градиент и стоит квадов. Оба канала, и оба должны
    // совпасть: стена наполовину под дневным светом и наполовину под факелом
    // несёт два градиента, а не один.
    corner_light light{};

    // Снова два бита на угол, и тоже в ключе слияния: ячейки сливаются, только
    // если форма вокруг них согласна. Столько это и стоит — 6,6% квадов, против
    // 13,2% когда выпуклость считалась для всех шести граней, а не для верхней.
    uint8 corner_convex = 0;

    [[nodiscard]]
    auto operator==(const face_mask_cell&) const -> bool = default;

    [[nodiscard]]
    auto is_empty() const -> bool {
        return index.is_empty();
    }
};

struct mesh_generation_storage {
    std::vector<quad> quads;
    std::vector<face_mask_cell> mask;
    std::vector<bool> depth_has_pages;

    // 64 КБ битового объёма; воркер переиспользует его между чанками и строит
    // один раз на меш. В куче, а не полем, чтобы стек воркера остался маленьким.
    std::unique_ptr<vw::asset::chunk_occupancy> occupancy;
    bool occupancy_valid = false;

    // Переиспользуется между чанками, поэтому заливка связности не аллоцирует.
    vw::asset::chunk_link_scratch link_scratch;

    auto clear() -> void {
        quads.clear();
    }
};

class strip_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_generation_storage& storage,
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    static auto merge_and_emit_strips(
        mesh_generation_storage& storage,
        mesh_source src,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        int32 face_direction,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;
};

class greedy_mesh_generator {
public:
    [[nodiscard]]
    static auto generate_mesh_data(
        mesh_generation_storage& storage,
        mesh_source src,
        const voxel_registry& registry,
        mesh_options opts = {}
    ) -> mesh;

private:
    // Идёт по битам видимости вместо прохода по маске: серия начинается с
    // countr_zero, прямоугольник гасится одним and-not на строку, а маска нужна
    // только чтобы сравнить ключи заведомо выставленных ячеек.
    static auto merge_and_emit_rects_bits(
        mesh_generation_storage& storage,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        detail::layer_rows& rows,
        const std::array<uint16, 256>& slots
    ) -> void;

    static auto merge_and_emit_rects(
        mesh_generation_storage& storage,
        mesh_source src,
        const detail::face_axis_mapping& axes,
        int32 face_direction,
        int32 layer,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;

    static auto generate_face_quads(
        mesh_generation_storage& storage,
        mesh_source src,
        int32 face_direction,
        const voxel_registry& registry,
        mesh_options opts
    ) -> void;
};
}  // namespace vw::gfx
