export module vw.asset:model.links;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;

export namespace vw::asset {

// Одна ячейка сетки связности: куб в 32 вокселя, восьмая часть чанка.
struct cell_links {
    // У ячейки, изрезанной густой сетью тоннелей, карманов может быть очень много.
    // Сверх этого числа они сливаются в один, отчего обход может сообщить только
    // больше положенного, но никогда не меньше. Обход отмечает пройденные карманы
    // 64-битной маской и один бит держит себе, отсюда и потолок.
    static constexpr std::size_t max_pockets = 63;

    std::vector<chunk_pocket> pockets;
    bool merged = false;

    [[nodiscard]] auto is_sealed() const -> bool {
        return pockets.empty();
    }
};

struct chunk_links {
    static constexpr int32 cell_size      = 32;
    static constexpr int32 cells_per_side = chunk_occupancy::side / cell_size;
    static constexpr int32 cell_count = cells_per_side * cells_per_side * cells_per_side;

    std::array<cell_links, cell_count> cells;

    [[nodiscard]] static constexpr auto cell_index(int32 x, int32 y, int32 z) -> int32 {
        return (((y * cells_per_side) + z) * cells_per_side) + x;
    }

    [[nodiscard]] auto is_sealed() const -> bool {
        return std::ranges::all_of(cells, [](const cell_links& c) -> bool {
            return c.is_sealed();
        });
    }
};

// Рабочая память для build_chunk_links. Держится вызывающим, чтобы мешинг чанка
// не аллоцировал.
struct chunk_link_scratch {
    std::vector<uint64> masks;
    std::vector<int32> row_begin;
    std::vector<uint8> seen;
    std::vector<int32> stack;
};

// Заливка по пустым вокселям, отрезками, а не по вокселю: строка ячейки — часть
// одного слова, и пустые её участки — это несколько промежутков. Заливка по
// вокселю стоит дороже, чем мешинг всего чанка.
//
// Сохраняются только карманы, касающиеся грани: запечатанный пузырь в середине
// ничего не соединяет, и заглянуть в него нельзя.
[[nodiscard]] auto build_chunk_links(
    const chunk_occupancy& occupancy, chunk_link_scratch& scratch
) -> chunk_links;

[[nodiscard]] auto build_chunk_links(const chunk_occupancy& occupancy) -> chunk_links;

// Уровень излучения по идентификатору блока, 0..15. Отдельная таблица, а не сам
// реестр: проходу нужен байт на воксель, а block_type занимает три десятка —
// чтение на месте протаскивает через кэш килобайты ради байта ответа.
//
// Это копия, а не второй источник истины: излучение блока записано в реестре, а
// отсюда строится один раз и передаётся дальше.
using emission_table = block_table<uint8>;

[[nodiscard]] auto build_emission_table(const block_registry& registry) -> emission_table;

}  // namespace vw::asset
