export module vw.core:blocks;

import std;

import :types;
import :color;

export namespace vw {

// Старший байт block_id. Категории независимы по построению: блок, добавленный
// в ландшафт, не сдвигает ни одного цвета палитры — и только поэтому в файлах
// моделей можно держать голые идентификаторы.
struct block_category {
    uint8 value = 0;

    constexpr block_category() = default;

    constexpr explicit block_category(
        uint8 value_
    )
        : value(value_) {}

    constexpr auto operator==(const block_category&) const -> bool = default;
};

// Личность блока: категория и номер внутри неё. Стабильна — её пишут в файлы и
// по ней блок находит игровой код. Номер в палитре устройства — другое число,
// его раздаёт реестр при сборке, и сборку оно не переживает.
struct block_id {
    uint16 value = 0;

    constexpr block_id() = default;

    constexpr block_id(
        block_category category, uint8 index
    )
        : value(static_cast<uint16>((static_cast<uint16>(category.value) << 8U) | index)) {}

    [[nodiscard]] static constexpr auto from_raw(
        uint16 raw
    ) -> block_id {
        block_id id;
        id.value = raw;
        return id;
    }

    [[nodiscard]] constexpr auto category() const -> block_category {
        return block_category{static_cast<uint8>(value >> 8U)};
    }

    [[nodiscard]] constexpr auto index() const -> uint8 {
        return static_cast<uint8>(value & 0xFFU);
    }

    constexpr auto operator==(const block_id&) const -> bool = default;
};

namespace blocks {
// Ноль во всех разрядах, и на это опираются: пустоту проверяют сравнением с
// нулём и таблица страниц, и битовые проходы по вокселям.
inline constexpr auto air = block_id{};
}  // namespace blocks

// Номер блока внутри набора — то, что лежит в странице модели. Полный
// идентификатор собирается из него и набора самой модели, поэтому воксель стоит
// байт, а не два.
//
// Ноль означает пустоту в любом наборе, и нумерация каталога начинается с
// единицы именно поэтому: нулевой байт обязан читаться как воздух, иначе он
// декодировался бы в первый блок набора модели, а битовый проход по вокселям,
// складывающий «байт ненулевой», сломался бы молча.
struct block_index {
    uint8 value = 0;

    constexpr block_index() = default;

    constexpr explicit block_index(
        uint8 value_
    )
        : value(value_) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return value == 0;
    }

    constexpr auto operator==(const block_index&) const -> bool = default;
};

// Варианты одного материала. Идут подряд по индексу, поэтому группа — это база
// и число, а не список: принадлежность проверяется двумя сравнениями, и вопрос
// «это вообще трава?» достаётся игровой логике бесплатно.
struct block_span {
    block_id first;
    uint8 count = 1;

    constexpr block_span() = default;

    constexpr block_span(
        block_category category, uint8 index, uint8 count_
    )
        : first(category, index), count(count_) {}

    [[nodiscard]] constexpr auto operator[](
        uint32 variant
    ) const -> block_id {
        return block_id{first.category(), static_cast<uint8>(first.index() + (variant % count))};
    }

    // Вариант по значению шума. Шум обязан быть пространственно связным —
    // пятнами в несколько вокселей, а не хешем позиции: жадный мешер сливает
    // соседей только при совпадении блока, и белый шум по вокселю разносит
    // плоскость луга с десятка квадов до пары тысяч.
    [[nodiscard]] constexpr auto pick(
        uint32 patch_noise
    ) const -> block_id {
        return (*this)[patch_noise];
    }

    [[nodiscard]] constexpr auto contains(
        block_id id
    ) const -> bool {
        return id.category() == first.category() && id.index() >= first.index() &&
               id.index() < first.index() + count;
    }
};

// Как блок ведёт себя в мешере. Прозрачных разрядов здесь ещё нет: за ними стоит
// отдельный проход с сортировкой и смешиванием, а признак, заведённый раньше
// прохода, — это ветка в горячем пути, которая никуда не ведёт.
enum class block_surface : uint8 {
    invisible,
    opaque,
};

struct block_material {
    color clr = colors::empty;

    // От нуля до пятнадцати, и это сразу два числа: насколько источник ярок и
    // насколько далеко достаёт. Шаг заливки стоит ровно единицу, поэтому яркому,
    // но близко бьющему блоку понадобилось бы второе значение в каждом углу
    // квада, а полубайт там один, не два. Шкала Minecraft по той же причине:
    // факел 14, светокамень 15.
    uint8 emission = 0;

    // Насколько ярко блок рисует сам себя; к тому, что он даёт соседям, отношения
    // не имеет. 255 означает, что он выводится ровно тем цветом, каким нарисован,
    // даже там, куда не доходит никакой свет.
    //
    // Свойства два, а не одно, потому что они расходятся: у лавы есть оба, у
    // кристалла, который светится, но не освещает комнату, — только это, а у
    // утопленной в стену лампы могло бы быть только другое. В шейдере они тоже
    // ничем не связаны: это слагаемое не знает перекрывающих, то — берёт затенение.
    uint8 glow = 0;

    constexpr auto operator==(const block_material&) const -> bool = default;
};

// Запись каталога. Одинакова для встроенной таблицы и для всего, что добавит
// игровая логика или файл, когда они появятся.
struct block_desc {
    block_id id;
    std::string_view name;
    block_material material;
    block_surface surface = block_surface::opaque;
};

// Раздел набора: соседние номера одного смысла — кожа, ткань, металл. Хранится
// диапазоном, а не полем в каждом блоке: каталог и так разложен по смыслу, и
// группа стоит трёх чисел на всю группу вместо байта на блок.
struct block_group {
    std::string_view name;
    block_id first;
    uint8 count = 1;

    [[nodiscard]] constexpr auto at(
        uint8 offset
    ) const -> block_id {
        return block_id{first.category(), static_cast<uint8>(first.index() + offset)};
    }
};

// Природа набора. Палитра — алфавит цветов: воксель в ней пиксель, каждый цвет
// лежит ровно в одном номере и ничего кроме цвета за номером не стоит. Материалы —
// вещество мира: за номером стоит поведение, а цвет всего лишь одно из свойств.
// Граница проходит по тому, действует ли игра на отдельный воксель: землю копают,
// рубашку — нет.
enum class block_set_kind : uint8 { palette, materials };

// Набор целиком: имя, под которым его выбирают, и разделы, на которые он бьётся.
// Без этого набор — голый байт, и интерфейсу нечего о нём сказать.
struct block_set {
    block_category category;
    std::string_view name;

    // Умолчание — материалы: палитра обещает полноту и единственность цвета, и
    // выдавать это обещание за того, кто о нём не просил, реестр не станет.
    block_set_kind kind = block_set_kind::materials;

    std::span<const block_group> groups;
};

// Плотный номер блока в палитре устройства. В кваде под него десять бит, а
// личность блока разрежена и туда не влезает — отсюда два числа вместо одного.
struct block_slot {
    uint16 value = 0;

    constexpr auto operator==(const block_slot&) const -> bool = default;
};

// Потолок ставит квад: десять свободных бит в data1 и ни одним больше, пока его
// запись остаётся двенадцатибайтовой.
inline constexpr uint32 block_slot_capacity = 1024;

// Блок, которого в реестре нет, читается как слот ноль и выводится кричащим
// цветом: невидимость на его месте прятала бы опечатку в каталоге до первого
// недоумения от картинки.
inline constexpr auto missing_block_slot = block_slot{0};

struct block_type {
    block_id id;
    block_slot slot;
    std::string_view name;
    block_material material;
    block_surface surface = block_surface::invisible;
};

// Значение на блок, разложенное по категориям: строка на живую категорию плюс
// строка умолчаний. Плоские 65 536 записей стоили бы 64 КБ ради сотни-другой, а
// проход по вокселям читает одну-две категории — то есть заголовок и одну
// строку, ровно как читал плоские 256 байт до того, как идентификаторы стали
// разрежёнными.
template <typename T>
class block_table {
public:
    block_table() = default;

    explicit block_table(
        T fallback
    ) {
        rows_[0].fill(fallback);
    }

    [[nodiscard]] auto get(
        block_id id
    ) const -> const T& {
        return rows_[row_of_[id.category().value]][id.index()];
    }

    // Целая строка набора. Проход по вокселям одной модели читает ровно её,
    // поэтому заголовок стоит взять один раз, а не на каждый воксель.
    [[nodiscard]] auto row(
        block_category category
    ) const -> const std::array<T, 256>& {
        return rows_[row_of_[category.value]];
    }

    auto set(
        block_id id, T value
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

class block_registry {
public:
    block_registry();

    // Каталог по умолчанию плюс то, что добавят игровая логика, мод или файл,
    // когда появятся. Точка расширения заведена сразу, чтобы её не пришлось
    // потом прорубать сквозь готовый реестр.
    explicit block_registry(std::span<const block_desc> extra);

    [[nodiscard]] auto get(
        block_id id
    ) const -> const block_type& {
        return by_slot_[slot_of(id).value];
    }

    [[nodiscard]] auto get(
        block_slot slot
    ) const -> const block_type& {
        return by_slot_[slot.value];
    }

    [[nodiscard]] auto slot_of(
        block_id id
    ) const -> block_slot {
        return block_slot{slots_.get(id)};
    }

    // Слоты целого набора. Мешер идёт по одной модели, набор у неё один, и
    // заголовок таблицы стоит взять один раз на меш, а не на каждый квад.
    [[nodiscard]] auto slot_row(
        block_category category
    ) const -> const std::array<uint16, 256>& {
        return slots_.row(category);
    }

    [[nodiscard]] auto find(std::string_view name) const -> std::optional<block_id>;

    [[nodiscard]] auto sets() const -> std::span<const block_set> {
        return sets_;
    }

    // Нулевой указатель значит набор, о котором каталог не знает: расширение
    // вправе завести блоки и не заводить разделов. Показать их всё равно есть
    // чем — списком, — а выдумывать за него имя реестр не станет.
    [[nodiscard]] auto set_of(block_category category) const -> const block_set*;

    // Первый набор нужной природы. По нему выбирают, чем открыть документ:
    // структуру строят из вещества, всё прочее красят палитрой, и знать имена
    // конкретных наборов для этого никому не нужно.
    [[nodiscard]] auto first_set(block_set_kind kind) const -> const block_set*;

    // Разложены по слотам, поэтому позиция записи здесь и есть её слот. Так их
    // читают и буфер палитры, и панель блоков.
    [[nodiscard]] auto all() const -> std::span<const block_type> {
        return by_slot_;
    }

private:
    auto add_(const block_desc& desc) -> void;

    std::vector<block_type> by_slot_;
    std::vector<block_set> sets_;
    block_table<uint16> slots_;
    std::unordered_map<std::string_view, block_id> by_name_;
};

// Реестр встроенного каталога, один на процесс. Нужен там, где реестр брать
// неоткуда, а каталог всё равно один: заголовочные тесты и мир, поднятый без
// движка. Владелец настоящего реестра — engine, и он раздаёт свой: расширение
// каталога иначе пришлось бы настраивать после конструирования.
[[nodiscard]] auto default_block_registry() -> const block_registry&;

struct voxel {
    block_id id = blocks::air;

    constexpr voxel() = default;

    constexpr explicit voxel(
        block_id block_id
    )
        : id(block_id) {}

    [[nodiscard]] constexpr auto is_empty() const -> bool {
        return id == blocks::air;
    }

    constexpr auto operator==(const voxel&) const -> bool = default;
};

inline constexpr auto empty_voxel = voxel{};

}  // namespace vw
