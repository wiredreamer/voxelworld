export module vw.asset:model.occupancy;

import std;

import vw.core;
import :model.identity;

export namespace vw::asset {

// Одна грань чанка битами занятости: 64x64 в 64 словах. Передать её соседу — это
// копия в 512 байт без аллокаций, а мешер берёт целую строку соседских бит одним
// чтением вместо опроса по вокселю.
struct face_occupancy {
    static constexpr int32 side = 64;

    std::array<uint64, side> rows{};

    [[nodiscard]] auto test(int32 a, int32 b) const -> bool {
        return ((rows[b] >> a) & 1U) != 0;
    }

    auto set(int32 a, int32 b) -> void {
        rows[b] |= uint64{1} << a;
    }

    auto clear() -> void {
        rows.fill(0);
    }
};

// Шесть соседних плоскостей, которыми модель закрывает свою внешнюю сторону там,
// где к ней прижата другая модель, — чанки мира и всё прочее, собранное из
// нескольких моделей. Три килобайта, нужные с момента, когда соседи известны, и
// до конца работы мешера, ни кадром дольше; поэтому модель держит их за
// указателем, а не всю свою жизнь.
struct model_boundary {
    std::array<face_occupancy, 6> faces{};
    uint8 valid = 0;
};

// Целый чанк битами занятости: слово на строку вдоль X, индексируется по (y, z).
// 32 КБ, строится один раз на меш и читается построчно — проходы, которые мешер
// делает на ячейку, превращаются здесь в одиночные инструкции.
struct chunk_occupancy {
    static constexpr int32 side = 64;

    // Две ориентации одного объёма. Строки вдоль X обслуживают грани ±Y и ±Z,
    // строки вдоль Z — грани ±X. Обе заполняются за один проход, что дешевле, чем
    // потом транспонировать битовые плоскости 64x64.
    std::array<uint64, side * side> rows{};   // rows[y * side + z], bit x
    std::array<uint64, side * side> zrows{};  // zrows[y * side + x], bit z

    auto clear() -> void {
        rows.fill(0);
        zrows.fill(0);
    }

    [[nodiscard]] auto row(int32 y, int32 z) const -> uint64 {
        return rows[(y * side) + z];
    }

    [[nodiscard]] auto zrow(int32 y, int32 x) const -> uint64 {
        return zrows[(y * side) + x];
    }

    [[nodiscard]] auto test(int32 x, int32 y, int32 z) const -> bool {
        return ((row(y, z) >> x) & 1U) != 0;
    }

    auto set_row(int32 y, int32 z, uint64 bits) -> void {
        rows[(y * side) + z] |= bits;
    }

    auto set_zrow(int32 y, int32 x, uint64 bits) -> void {
        zrows[(y * side) + x] |= bits;
    }
};

// Где чанк пропускает взгляд — карманы воздуха внутри него.
//
// Карман — это один связный объём пустых вокселей, описанный блоками, до которых
// он достаёт на каждой из шести граней: грань, огрублённая до 8x8 блоков, по биту
// на блок. Взгляд входит через карман и выходит через тот же карман, и больше
// нигде.
//
// Две вещи здесь получены замерами, и обе важны.
//
// Карманы, а не пары граней: над склоном и небо, и тоннели под ним касаются
// боковой стороны чанка — в разных местах и разными карманами. Пары граней делают
// из этого одно отверстие, и обход, построенный на них, не скрыл вообще ничего от
// пещерной системы, которую воксельная заливка показывала полностью запечатанной.
//
// Ячейки по 32 вокселя, а не целые чанки: пещерная сеть связна почти везде,
// поэтому одно ложное отверстие где угодно заливает всё. На ячейках в 64 вокселя
// скрытым оставалось 0%, на 32 скрывается 66%, а 16 не даёт уже ничего.
struct chunk_pocket {
    static constexpr int32 face_count = 6;
    static constexpr int32 face_span  = 8;

    // Порядок граней: -X, +X, -Y, +Y, -Z, +Z. Противоположная грани — face ^ 1.
    std::array<uint64, face_count> faces{};

    // Объём ячейки блоками 4x4x4, по биту на блок, выставленным там, где у
    // кармана есть хоть один воксель. Нужен только чтобы понять, в каком кармане
    // стоит наблюдатель: старт со всех отдал бы камере в открытом воздухе тоннели
    // под её ногами, а через них — всю связную сеть.
    static constexpr int32 volume_span = 4;
    uint64 volume = 0;

    [[nodiscard]] static constexpr auto volume_bit(int32 x, int32 y, int32 z, int32 block)
        -> uint64 {
        return uint64{1}
            << ((((y / block) * volume_span) + (z / block)) * volume_span + (x / block));
    }

    [[nodiscard]] auto holds(int32 x, int32 y, int32 z, int32 block) const -> bool {
        return (volume & volume_bit(x, y, z, block)) != 0;
    }

    [[nodiscard]] auto touches(int32 face) const -> bool {
        return faces[face] != 0;
    }

    // Две ячейки делят грань, и этот карман смыкается с тем только там, где оба
    // открыты в одном и том же блоке.
    [[nodiscard]] auto meets(const chunk_pocket& other, int32 face) const -> bool {
        return (faces[face] & other.faces[face ^ 1]) != 0;
    }

    [[nodiscard]] static auto wide_open() -> chunk_pocket {
        chunk_pocket pocket;
        pocket.faces.fill(~uint64{0});
        return pocket;
    }
};

}  // namespace vw::asset
