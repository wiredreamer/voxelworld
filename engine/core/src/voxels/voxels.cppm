export module vw.core:voxels;

import std;

import :types;
import :color;

export namespace vw {

// Старший байт voxel. Категории независимы по построению: воксель, добавленный
// в набор world, не сдвигает ни одного цвета палитры — и только поэтому в файлах
// моделей можно держать голые идентификаторы.
struct voxel_category {
    uint8 value = 0;

    constexpr voxel_category() = default;

    constexpr explicit voxel_category(
        uint8 value_
    )
        : value(value_) {}

    constexpr auto operator==(const voxel_category&) const -> bool = default;
};

// Личность вокселя: категория и номер внутри неё. Стабильна — её пишут в файлы и
// по ней воксель находит игровой код. Номер в палитре устройства — другое число,
// его раздаёт реестр при сборке, и сборку оно не переживает.
struct voxel {
    uint16 value = 0;

    constexpr voxel() = default;

    constexpr voxel(
        voxel_category category, uint8 index
    )
        : value(static_cast<uint16>((static_cast<uint16>(category.value) << 8U) | index)) {}

    [[nodiscard]] static constexpr auto from_raw(
        uint16 raw
    ) -> voxel {
        voxel id;
        id.value = raw;
        return id;
    }

    [[nodiscard]] constexpr auto category() const -> voxel_category {
        return voxel_category{static_cast<uint8>(value >> 8U)};
    }

    [[nodiscard]] constexpr auto index() const -> uint8 {
        return static_cast<uint8>(value & 0xFFU);
    }

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const voxel&) const -> bool = default;
};

namespace voxels {
// Ноль во всех разрядах, и на это опираются: пустоту проверяют сравнением с
// нулём и таблица страниц, и битовые проходы по вокселям.
inline constexpr auto air = voxel{};
}  // namespace voxels

// Номер вокселя внутри набора — то, что лежит в странице модели. Полный
// идентификатор собирается из него и набора самой модели, поэтому воксель стоит
// байт, а не два.
//
// Ноль означает пустоту в любом наборе, и нумерация каталога начинается с
// единицы именно поэтому: нулевой байт обязан читаться как воздух, иначе он
// декодировался бы в первый воксель набора модели, а битовый проход по вокселям,
// складывающий «байт ненулевой», сломался бы молча.
struct voxel_index {
    uint8 value = 0;

    constexpr voxel_index() = default;

    constexpr explicit voxel_index(
        uint8 value_
    )
        : value(value_) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const voxel_index&) const -> bool = default;
};

// Варианты одного материала. Идут подряд по индексу, поэтому группа — это база
// и число, а не список: принадлежность проверяется двумя сравнениями, и вопрос
// «это вообще трава?» достаётся игровой логике бесплатно.
struct voxel_span {
    voxel first;
    uint8 count = 1;

    constexpr voxel_span() = default;

    constexpr voxel_span(
        voxel_category category, uint8 index, uint8 count_
    )
        : first(category, index), count(count_) {}

    [[nodiscard]] constexpr auto operator[](
        uint32 variant
    ) const -> voxel {
        return voxel{first.category(), static_cast<uint8>(first.index() + (variant % count))};
    }

    // Вариант по значению шума. Шум обязан быть пространственно связным —
    // пятнами в несколько вокселей, а не хешем позиции: жадный мешер сливает
    // соседей только при совпадении вокселя, и белый шум по вокселю разносит
    // плоскость луга с десятка квадов до пары тысяч.
    [[nodiscard]] constexpr auto pick(
        uint32 patch_noise
    ) const -> voxel {
        return (*this)[patch_noise];
    }

    [[nodiscard]] constexpr auto contains(
        voxel id
    ) const -> bool {
        return                                    //
            id.category() == first.category() &&  //
            id.index() >= first.index() &&        //
            id.index() < first.index() + count;
    }
};

// Как воксель ведёт себя в мешере. Прозрачных разрядов здесь ещё нет: за ними стоит
// отдельный проход с сортировкой и смешиванием, а признак, заведённый раньше
// прохода, — это ветка в горячем пути, которая никуда не ведёт.
enum class voxel_surface : uint8 {
    invisible,
    opaque,
};

struct voxel_material {
    color clr = colors::empty;

    // От нуля до пятнадцати, и это сразу два числа: насколько источник ярок и
    // насколько далеко достаёт. Шаг заливки стоит ровно единицу, поэтому яркому,
    // но близко бьющему вокселю понадобилось бы второе значение в каждом углу
    // квада, а полубайт там один, не два. Шкала Minecraft по той же причине:
    // факел 14, светокамень 15.
    uint8 emission = 0;

    // Насколько ярко воксель рисует сам себя; к тому, что он даёт соседям, отношения
    // не имеет. 255 означает, что он выводится ровно тем цветом, каким нарисован,
    // даже там, куда не доходит никакой свет.
    //
    // Свойства два, а не одно, потому что они расходятся: у лавы есть оба, у
    // кристалла, который светится, но не освещает комнату, — только это, а у
    // утопленной в стену лампы могло бы быть только другое. В шейдере они тоже
    // ничем не связаны: это слагаемое не знает перекрывающих, то — берёт затенение.
    uint8 glow = 0;

    constexpr auto operator==(const voxel_material&) const -> bool = default;
};

// Запись каталога. Одинакова для встроенной таблицы и для всего, что добавит
// игровая логика или файл, когда они появятся.
struct voxel_desc {
    voxel id;
    std::string_view name;
    voxel_material material;
    voxel_surface surface = voxel_surface::opaque;
};

// Раздел набора: соседние номера одного смысла — кожа, ткань, металл. Хранится
// диапазоном, а не полем в каждом вокселе: каталог и так разложен по смыслу, и
// группа стоит трёх чисел на всю группу вместо байта на воксель.
struct voxel_group {
    std::string_view name;
    voxel first;
    uint8 count = 1;

    [[nodiscard]] constexpr auto at(
        uint8 offset
    ) const -> voxel {
        return voxel{first.category(), static_cast<uint8>(first.index() + offset)};
    }
};

// Природа набора. Палитра — алфавит цветов: воксель в ней пиксель, каждый цвет
// лежит ровно в одном номере и ничего кроме цвета за номером не стоит. Материалы —
// вещество мира: за номером стоит поведение, а цвет всего лишь одно из свойств.
// Граница проходит по тому, действует ли игра на отдельный воксель: землю копают,
// рубашку — нет.
enum class voxel_set_kind : uint8 { palette, materials };

// Набор целиком: имя, под которым его выбирают, и разделы, на которые он бьётся.
// Без этого набор — голый байт, и интерфейсу нечего о нём сказать.
struct voxel_set {
    voxel_category category;
    std::string_view name;

    // Умолчание — материалы: палитра обещает полноту и единственность цвета, и
    // выдавать это обещание за того, кто о нём не просил, реестр не станет.
    voxel_set_kind kind = voxel_set_kind::materials;

    std::span<const voxel_group> groups;
};

// Плотный номер вокселя в палитре устройства. В кваде под него десять бит, а
// личность вокселя разрежена и туда не влезает — отсюда два числа вместо одного.
struct voxel_slot {
    uint16 value = 0;

    constexpr auto operator==(const voxel_slot&) const -> bool = default;
};

// Потолок ставит квад: десять свободных бит в data1 и ни одним больше, пока его
// запись остаётся двенадцатибайтовой.
inline constexpr uint32 voxel_slot_capacity = 1024;

// Блок, которого в реестре нет, читается как слот ноль и выводится кричащим
// цветом: невидимость на его месте прятала бы опечатку в каталоге до первого
// недоумения от картинки.
inline constexpr auto missing_voxel_slot = voxel_slot{0};

struct voxel_type {
    voxel id;
    voxel_slot slot;
    std::string_view name;
    voxel_material material;
    voxel_surface surface = voxel_surface::invisible;
};

// Значение на воксель, разложенное по категориям: строка на живую категорию плюс
// строка умолчаний. Плоские 65 536 записей стоили бы 64 КБ ради сотни-другой, а
// проход по вокселям читает одну-две категории — то есть заголовок и одну
// строку, ровно как читал плоские 256 байт до того, как идентификаторы стали
// разрежёнными.
template <typename T>
class voxel_table {
public:
    voxel_table() = default;

    explicit voxel_table(
        T fallback
    ) {
        rows_[0].fill(fallback);
    }

    [[nodiscard]] auto get(
        voxel id
    ) const -> const T& {
        return rows_[row_of_[id.category().value]][id.index()];
    }

    // Целая строка набора. Проход по вокселям одной модели читает ровно её,
    // поэтому заголовок стоит взять один раз, а не на каждый воксель.
    [[nodiscard]] auto row(
        voxel_category category
    ) const -> const std::array<T, 256>& {
        return rows_[row_of_[category.value]];
    }

    auto set(
        voxel id, T value
    ) -> void {
        uint16& row = row_of_[id.category().value];
        if (row == 0) {
            const row_type defaults = rows_.front();
            row                     = static_cast<uint16>(rows_.size());
            rows_.push_back(defaults);
        }
        rows_[row][id.index()] = std::move(value);
    }

private:
    using row_type = std::array<T, 256>;

    std::vector<row_type> rows_{1};
    std::array<uint16, 256> row_of_{};
};

class voxel_registry {
public:
    voxel_registry();

    // Каталог по умолчанию плюс то, что добавят игровая логика, мод или файл,
    // когда появятся. Точка расширения заведена сразу, чтобы её не пришлось
    // потом прорубать сквозь готовый реестр.
    explicit voxel_registry(std::span<const voxel_desc> extra);

    [[nodiscard]] auto get(
        voxel id
    ) const -> const voxel_type& {
        return by_slot_[slot_of(id).value];
    }

    [[nodiscard]] auto get(
        voxel_slot slot
    ) const -> const voxel_type& {
        return by_slot_[slot.value];
    }

    [[nodiscard]] auto slot_of(
        voxel id
    ) const -> voxel_slot {
        return voxel_slot{slots_.get(id)};
    }

    // Слоты целого набора. Мешер идёт по одной модели, набор у неё один, и
    // заголовок таблицы стоит взять один раз на меш, а не на каждый квад.
    [[nodiscard]] auto slot_row(
        voxel_category category
    ) const -> const std::array<uint16, 256>& {
        return slots_.row(category);
    }

    [[nodiscard]] auto find(std::string_view name) const -> std::optional<voxel>;

    [[nodiscard]] auto sets() const -> std::span<const voxel_set> {
        return sets_;
    }

    // Нулевой указатель значит набор, о котором каталог не знает: расширение
    // вправе завести воксели и не заводить разделов. Показать их всё равно есть
    // чем — списком, — а выдумывать за него имя реестр не станет.
    [[nodiscard]] auto set_of(voxel_category category) const -> const voxel_set*;

    // Первый набор нужной природы. По нему выбирают, чем открыть документ:
    // структуру строят из вещества, всё прочее красят палитрой, и знать имена
    // конкретных наборов для этого никому не нужно.
    [[nodiscard]] auto first_set(voxel_set_kind kind) const -> const voxel_set*;

    // Разложены по слотам, поэтому позиция записи здесь и есть её слот. Так их
    // читают и буфер палитры, и панель вокселей.
    [[nodiscard]] auto all() const -> std::span<const voxel_type> {
        return by_slot_;
    }

private:
    auto add_(const voxel_desc& desc) -> void;

    std::vector<voxel_type> by_slot_;
    std::vector<voxel_set> sets_;
    voxel_table<uint16> slots_;
    std::unordered_map<std::string_view, voxel> by_name_;
};

// Реестр встроенного каталога, один на процесс. Нужен там, где реестр брать
// неоткуда, а каталог всё равно один: заголовочные тесты и мир, поднятый без
// движка. Владелец настоящего реестра — engine, и он раздаёт свой: расширение
// каталога иначе пришлось бы настраивать после конструирования.
[[nodiscard]] auto default_voxel_registry() -> const voxel_registry&;

}  // namespace vw
