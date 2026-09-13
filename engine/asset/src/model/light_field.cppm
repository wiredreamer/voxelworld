export module vw.asset:model.light_field;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;

export namespace vw::asset {

// Один чанк запечённого света в том виде, в каком он и хранится: страницы 8x8x8
// по четыре бита на воксель. Полубайт здесь не хитрая экономия — уровень бывает
// от 0 до 15 и никаким другим, поэтому байт был бы наполовину набивкой.
//
// Почти всё вытягивают два вырожденных случая. Для небесного света порода ниже
// пещер темна целиком, а воздух над поверхностью целиком равен 15, и любой из
// этих случаев стоит одного байта и вовсе никакой таблицы; для света блоков почти
// каждый чанк мира — тёмный случай. Оставшееся держит таблицу из 512 записей, по
// одной на страницу, и упакованную страницу только для тех, что действительно
// меняются. На настоящем рельефе меняются 2,4% небесных страниц, остальной чанк —
// это таблица.
class light_field {
public:
    static constexpr int32 side        = chunk_occupancy::side;
    static constexpr int32 page        = 8;
    static constexpr int32 pages_side  = side / page;
    static constexpr int32 page_count  = pages_side * pages_side * pages_side;
    static constexpr int32 page_voxels = page * page * page;
    static constexpr int32 page_bytes  = page_voxels / 2;

    using page_type = std::array<uint8, page_bytes>;

    // Плоскость света в одном вокселе снаружи чанка, по граням, в собственном
    // порядке граней модели: +X, -X, +Y, -Y, +Z, -Z.
    //
    // Грань на оболочке чанка берёт свой свет из ячейки перед собой, а та
    // принадлежит соседу. Без этого мешеру неоткуда его прочесть, а зажатие
    // обратно внутрь попадает в сплошной воксель, которому грань принадлежит, — а
    // его заливка оставляет нулём, отчего каждая наружная грань каждого чанка
    // выходит чёрной.
    //
    // Заливка и так покрывает пятнадцать вокселей за чанком, поэтому заполнение
    // этих плоскостей ничего не стоит: они запекаются вместе с полем, а не
    // передаются потом, как плоскости занятости. Плоскость — это один уровень либо
    // 4096 полубайтов.
    struct boundary_light {
        static constexpr int32 face_count = 6;

        std::array<uint8, face_count> uniform{};
        std::array<std::vector<uint8>, face_count> packed{};

        [[nodiscard]] auto level_at(int32 face, int32 a, int32 b) const -> uint8 {
            const auto& plane = packed[static_cast<std::size_t>(face)];
            if (plane.empty()) {
                return uniform[static_cast<std::size_t>(face)];
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

    // Тьма — именно так и должен выглядеть ещё не освещённый чанк.
    light_field() = default;

    light_field(uint8 level, boundary_light around)
        : uniform_{level}, around_{std::move(around)} {}

    // Строится light_column::bake — единственным, кто умеет заполнять таблицу.
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

    // То же, но любая одна координата может быть -1 или side: мешер читает воксель
    // наружу вдоль нормали грани, а для грани на оболочке это уже соседский.
    // Ячейка, вышедшая наружу сразу по двум осям — самый край чанка, — зажимается
    // в плоскость; это ошибка не больше уровня и шириной в один воксель.
    [[nodiscard]] auto level_around(int32 x, int32 y, int32 z) const -> uint8 {
        const bool inside = x >= 0 && y >= 0 && z >= 0 && x < side && y < side && z < side;
        if (inside) {
            return level_at(x, y, z);
        }

        const auto clamp = [](int32 v) -> int32 { return std::clamp(v, 0, side - 1); };

        if (x < 0 || x >= side) {
            return around_.level_at(x < 0 ? 1 : 0, clamp(y), clamp(z));
        }
        if (y < 0 || y >= side) {
            return around_.level_at(y < 0 ? 3 : 2, clamp(x), clamp(z));
        }
        return around_.level_at(z < 0 ? 5 : 4, clamp(x), clamp(y));
    }

    // Таблицы нет: весь чанк равен uniform_level().
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

    // Перезаливка колонки запекает каждый её чанк независимо от того, изменилось
    // ли в нём хоть что-то, а чанк, чей свет остался прежним, мешить заново
    // нельзя. Пять килобайт сравнения против полного перестроения меша — выбор
    // очевидный.
    //
    // Формы канонические, поэтому сравнение настолько точно, насколько выглядит:
    // bake сворачивает чанк одного уровня в однородный, а плоскость одного уровня
    // — в её uniform, и никогда не оставляет таблицу, говорящую то же, что сказала
    // бы более короткая.
    auto operator==(const light_field&) const -> bool = default;

private:
    uint8 uniform_ = 0;
    std::vector<uint16> table_;
    std::vector<page_type> pages_;
    boundary_light around_;
};

}  // namespace vw::asset
