export module vw.core:blocks.catalog;

import std;

import :types;
import :color;
import :blocks;

export namespace vw::blocks {

namespace palette {
inline constexpr auto category = block_category{0};

// Номера повторяют порядок colors::all — рампа за рампой, шаг за шагом. Это не
// совпадение, а инвариант: он проверяется ниже и он же делает набор палитрой.
// Цвет здесь и есть личность блока, больше за номером не стоит ничего.
inline constexpr auto blue   = block_span{category,  1, 6};
inline constexpr auto green  = block_span{category,  7, 6};
inline constexpr auto brown  = block_span{category, 13, 6};
inline constexpr auto amber  = block_span{category, 19, 6};
inline constexpr auto red    = block_span{category, 25, 6};
inline constexpr auto purple = block_span{category, 31, 6};
inline constexpr auto gray   = block_span{category, 37, 10};
inline constexpr auto white  = block_id{category, 47};
inline constexpr auto black  = block_id{category, 48};

// Хвост свечения: цвет тот же, что в рампе, но блок рисует себя сам. Соседям он
// света не даёт — заливка комнаты остаётся делом ландшафта, где за ней стоит
// поведение, а не вид.
inline constexpr auto glow_blue   = block_id{category, 49};
inline constexpr auto glow_green  = block_id{category, 50};
inline constexpr auto glow_amber  = block_id{category, 51};
inline constexpr auto glow_red    = block_id{category, 52};
inline constexpr auto glow_purple = block_id{category, 53};
inline constexpr auto glow_white  = block_id{category, 54};

inline constexpr std::array groups = {
    block_group{"blue",   blue[0],    6},
    block_group{"green",  green[0],   6},
    block_group{"brown",  brown[0],   6},
    block_group{"amber",  amber[0],   6},
    block_group{"red",    red[0],     6},
    block_group{"purple", purple[0],  6},
    block_group{"gray",   gray[0],   10},
    block_group{"mono",   white,      2},
    block_group{"glow",   glow_blue,  6},
};
}  // namespace palette

namespace terrain {
inline constexpr auto category = block_category{1};

inline constexpr auto grass      = block_span{category,   1, 3};
inline constexpr auto grass_dark = block_span{category,   4, 3};
inline constexpr auto grass_dry  = block_span{category,   7, 3};
inline constexpr auto leaves     = block_span{category,  10, 3};
inline constexpr auto dirt       = block_span{category,  13, 3};
inline constexpr auto sand       = block_span{category,  16, 3};
inline constexpr auto wood       = block_span{category,  19, 3};
inline constexpr auto clay       = block_span{category,  22, 3};
inline constexpr auto stone      = block_span{category,  25, 3};
inline constexpr auto stone_deep = block_span{category,  28, 3};
inline constexpr auto bedrock    = block_id{category,  31};
inline constexpr auto gravel     = block_span{category,  32, 3};
inline constexpr auto snow       = block_span{category,  35, 3};
inline constexpr auto ice        = block_span{category,  38, 3};
inline constexpr auto ash        = block_span{category,  41, 3};
inline constexpr auto ore_gold   = block_span{category,  44, 3};
inline constexpr auto ore_iron   = block_span{category,  47, 3};
inline constexpr auto crystal    = block_span{category,  50, 3};
inline constexpr auto water      = block_id{category,  53};
inline constexpr auto lava       = block_id{category,  54};
inline constexpr auto magma      = block_id{category,  55};
inline constexpr auto glowstone  = block_id{category,  56};

// Разделы набора. Покрывают его номера подряд и без дыр — это проверяется ниже
// и держит порядок каталога осмысленным: блок, вставленный мимо своего раздела,
// иначе просто пропал бы из панели.
inline constexpr std::array groups = {
    block_group{"plants",  grass[0],      12},
    block_group{"soil",    dirt[0],        6},
    block_group{"wood",    wood[0],        3},
    block_group{"stone",   clay[0],       13},
    block_group{"ice",     snow[0],        6},
    block_group{"ash",     ash[0],         3},
    block_group{"ore",     ore_gold[0],    9},
    block_group{"liquids", water,          3},
    block_group{"light",   glowstone,      1},
};
}  // namespace terrain

}  // namespace vw::blocks

export namespace vw {

// Наборы, о которых знает встроенный каталог. Отсюда интерфейс берёт и список
// для выбора при создании модели, и разбиение палитры на разделы.
inline constexpr std::array default_block_sets = {
    block_set{
        blocks::palette::category, "palette", block_set_kind::palette, blocks::palette::groups
    },
    block_set{
        blocks::terrain::category, "terrain", block_set_kind::materials, blocks::terrain::groups
    },
};

// Каталог по умолчанию. Порядок записей задаёт слоты, поэтому наборы идут
// подряд: панель блоков группирует их одним проходом, не сортируя. Палитра идёт
// первой и занимает нулевую категорию как набор по умолчанию: модель, созданная
// ни о чём не спросив, оказывается в ней.
//
// Нумерация внутри набора начинается с единицы: ноль означает пустоту в любом
// наборе, потому что страница хранит только номер, а набор берётся у модели.
// Нулевой байт обязан читаться как воздух — на этом стоят и таблица страниц, и
// битовые проходы по вокселям. Для палитры это тот же ноль, что и для прочих:
// воздух — её пустой номер, а не отдельный набор.
//
// Цвета намеренно повторяются между материалами: лёд и кристалл — одни и те же
// три шага голубой рампы, и различает их только свечение. Ради этого разведение
// личности блока и его цвета и затевалось. В палитре наоборот: там цвет и есть
// личность, и повторяться ему нечего ради.
inline constexpr std::array default_block_catalog = {
    block_desc{blocks::air, "air", {}, block_surface::invisible},
    block_desc{blocks::palette::blue[0], "palette.blue_0", {colors::blue_0}},
    block_desc{blocks::palette::blue[1], "palette.blue_1", {colors::blue_1}},
    block_desc{blocks::palette::blue[2], "palette.blue_2", {colors::blue_2}},
    block_desc{blocks::palette::blue[3], "palette.blue_3", {colors::blue_3}},
    block_desc{blocks::palette::blue[4], "palette.blue_4", {colors::blue_4}},
    block_desc{blocks::palette::blue[5], "palette.blue_5", {colors::blue_5}},
    block_desc{blocks::palette::green[0], "palette.green_0", {colors::green_0}},
    block_desc{blocks::palette::green[1], "palette.green_1", {colors::green_1}},
    block_desc{blocks::palette::green[2], "palette.green_2", {colors::green_2}},
    block_desc{blocks::palette::green[3], "palette.green_3", {colors::green_3}},
    block_desc{blocks::palette::green[4], "palette.green_4", {colors::green_4}},
    block_desc{blocks::palette::green[5], "palette.green_5", {colors::green_5}},
    block_desc{blocks::palette::brown[0], "palette.brown_0", {colors::brown_0}},
    block_desc{blocks::palette::brown[1], "palette.brown_1", {colors::brown_1}},
    block_desc{blocks::palette::brown[2], "palette.brown_2", {colors::brown_2}},
    block_desc{blocks::palette::brown[3], "palette.brown_3", {colors::brown_3}},
    block_desc{blocks::palette::brown[4], "palette.brown_4", {colors::brown_4}},
    block_desc{blocks::palette::brown[5], "palette.brown_5", {colors::brown_5}},
    block_desc{blocks::palette::amber[0], "palette.amber_0", {colors::amber_0}},
    block_desc{blocks::palette::amber[1], "palette.amber_1", {colors::amber_1}},
    block_desc{blocks::palette::amber[2], "palette.amber_2", {colors::amber_2}},
    block_desc{blocks::palette::amber[3], "palette.amber_3", {colors::amber_3}},
    block_desc{blocks::palette::amber[4], "palette.amber_4", {colors::amber_4}},
    block_desc{blocks::palette::amber[5], "palette.amber_5", {colors::amber_5}},
    block_desc{blocks::palette::red[0], "palette.red_0", {colors::red_0}},
    block_desc{blocks::palette::red[1], "palette.red_1", {colors::red_1}},
    block_desc{blocks::palette::red[2], "palette.red_2", {colors::red_2}},
    block_desc{blocks::palette::red[3], "palette.red_3", {colors::red_3}},
    block_desc{blocks::palette::red[4], "palette.red_4", {colors::red_4}},
    block_desc{blocks::palette::red[5], "palette.red_5", {colors::red_5}},
    block_desc{blocks::palette::purple[0], "palette.purple_0", {colors::purple_0}},
    block_desc{blocks::palette::purple[1], "palette.purple_1", {colors::purple_1}},
    block_desc{blocks::palette::purple[2], "palette.purple_2", {colors::purple_2}},
    block_desc{blocks::palette::purple[3], "palette.purple_3", {colors::purple_3}},
    block_desc{blocks::palette::purple[4], "palette.purple_4", {colors::purple_4}},
    block_desc{blocks::palette::purple[5], "palette.purple_5", {colors::purple_5}},
    block_desc{blocks::palette::gray[0], "palette.gray_0", {colors::gray_0}},
    block_desc{blocks::palette::gray[1], "palette.gray_1", {colors::gray_1}},
    block_desc{blocks::palette::gray[2], "palette.gray_2", {colors::gray_2}},
    block_desc{blocks::palette::gray[3], "palette.gray_3", {colors::gray_3}},
    block_desc{blocks::palette::gray[4], "palette.gray_4", {colors::gray_4}},
    block_desc{blocks::palette::gray[5], "palette.gray_5", {colors::gray_5}},
    block_desc{blocks::palette::gray[6], "palette.gray_6", {colors::gray_6}},
    block_desc{blocks::palette::gray[7], "palette.gray_7", {colors::gray_7}},
    block_desc{blocks::palette::gray[8], "palette.gray_8", {colors::gray_8}},
    block_desc{blocks::palette::gray[9], "palette.gray_9", {colors::gray_9}},
    block_desc{blocks::palette::white, "palette.white", {colors::white}},
    block_desc{blocks::palette::black, "palette.black", {colors::black}},
    block_desc{blocks::palette::glow_blue, "palette.glow_blue", {colors::blue_4, 0, 200}},
    block_desc{blocks::palette::glow_green, "palette.glow_green", {colors::green_4, 0, 200}},
    block_desc{blocks::palette::glow_amber, "palette.glow_amber", {colors::amber_5, 0, 200}},
    block_desc{blocks::palette::glow_red, "palette.glow_red", {colors::red_4, 0, 200}},
    block_desc{blocks::palette::glow_purple, "palette.glow_purple", {colors::purple_4, 0, 200}},
    block_desc{blocks::palette::glow_white, "palette.glow_white", {colors::white, 0, 200}},
    block_desc{blocks::terrain::grass[0], "terrain.grass_0", {colors::green_2}},
    block_desc{blocks::terrain::grass[1], "terrain.grass_1", {colors::green_3}},
    block_desc{blocks::terrain::grass[2], "terrain.grass_2", {colors::green_4}},
    block_desc{blocks::terrain::grass_dark[0], "terrain.grass_dark_0", {colors::green_0}},
    block_desc{blocks::terrain::grass_dark[1], "terrain.grass_dark_1", {colors::green_1}},
    block_desc{blocks::terrain::grass_dark[2], "terrain.grass_dark_2", {colors::green_2}},
    block_desc{blocks::terrain::grass_dry[0], "terrain.grass_dry_0", {colors::green_3}},
    block_desc{blocks::terrain::grass_dry[1], "terrain.grass_dry_1", {colors::green_4}},
    block_desc{blocks::terrain::grass_dry[2], "terrain.grass_dry_2", {colors::green_5}},
    block_desc{blocks::terrain::leaves[0], "terrain.leaves_0", {colors::green_1}},
    block_desc{blocks::terrain::leaves[1], "terrain.leaves_1", {colors::green_2}},
    block_desc{blocks::terrain::leaves[2], "terrain.leaves_2", {colors::green_3}},
    block_desc{blocks::terrain::dirt[0], "terrain.dirt_0", {colors::brown_0}},
    block_desc{blocks::terrain::dirt[1], "terrain.dirt_1", {colors::brown_1}},
    block_desc{blocks::terrain::dirt[2], "terrain.dirt_2", {colors::brown_2}},
    block_desc{blocks::terrain::sand[0], "terrain.sand_0", {colors::brown_3}},
    block_desc{blocks::terrain::sand[1], "terrain.sand_1", {colors::brown_4}},
    block_desc{blocks::terrain::sand[2], "terrain.sand_2", {colors::brown_5}},
    block_desc{blocks::terrain::wood[0], "terrain.wood_0", {colors::amber_0}},
    block_desc{blocks::terrain::wood[1], "terrain.wood_1", {colors::amber_1}},
    block_desc{blocks::terrain::wood[2], "terrain.wood_2", {colors::amber_2}},
    block_desc{blocks::terrain::clay[0], "terrain.clay_0", {colors::amber_2}},
    block_desc{blocks::terrain::clay[1], "terrain.clay_1", {colors::amber_3}},
    block_desc{blocks::terrain::clay[2], "terrain.clay_2", {colors::amber_4}},
    block_desc{blocks::terrain::stone[0], "terrain.stone_0", {colors::gray_4}},
    block_desc{blocks::terrain::stone[1], "terrain.stone_1", {colors::gray_5}},
    block_desc{blocks::terrain::stone[2], "terrain.stone_2", {colors::gray_6}},
    block_desc{blocks::terrain::stone_deep[0], "terrain.stone_deep_0", {colors::gray_1}},
    block_desc{blocks::terrain::stone_deep[1], "terrain.stone_deep_1", {colors::gray_2}},
    block_desc{blocks::terrain::stone_deep[2], "terrain.stone_deep_2", {colors::gray_3}},
    block_desc{blocks::terrain::bedrock, "terrain.bedrock", {colors::gray_0}},
    block_desc{blocks::terrain::gravel[0], "terrain.gravel_0", {colors::gray_3}},
    block_desc{blocks::terrain::gravel[1], "terrain.gravel_1", {colors::gray_4}},
    block_desc{blocks::terrain::gravel[2], "terrain.gravel_2", {colors::gray_5}},
    block_desc{blocks::terrain::snow[0], "terrain.snow_0", {colors::gray_8}},
    block_desc{blocks::terrain::snow[1], "terrain.snow_1", {colors::gray_9}},
    block_desc{blocks::terrain::snow[2], "terrain.snow_2", {colors::white}},
    block_desc{blocks::terrain::ice[0], "terrain.ice_0", {colors::blue_3}},
    block_desc{blocks::terrain::ice[1], "terrain.ice_1", {colors::blue_4}},
    block_desc{blocks::terrain::ice[2], "terrain.ice_2", {colors::blue_5}},
    block_desc{blocks::terrain::ash[0], "terrain.ash_0", {colors::gray_0}},
    block_desc{blocks::terrain::ash[1], "terrain.ash_1", {colors::gray_1}},
    block_desc{blocks::terrain::ash[2], "terrain.ash_2", {colors::gray_2}},
    block_desc{blocks::terrain::ore_gold[0], "terrain.ore_gold_0", {colors::amber_3}},
    block_desc{blocks::terrain::ore_gold[1], "terrain.ore_gold_1", {colors::amber_4}},
    block_desc{blocks::terrain::ore_gold[2], "terrain.ore_gold_2", {colors::amber_5}},
    block_desc{blocks::terrain::ore_iron[0], "terrain.ore_iron_0", {colors::gray_5}},
    block_desc{blocks::terrain::ore_iron[1], "terrain.ore_iron_1", {colors::gray_6}},
    block_desc{blocks::terrain::ore_iron[2], "terrain.ore_iron_2", {colors::gray_7}},
    block_desc{blocks::terrain::crystal[0], "terrain.crystal_0", {colors::blue_3, 0, 160}},
    block_desc{blocks::terrain::crystal[1], "terrain.crystal_1", {colors::blue_4, 0, 160}},
    block_desc{blocks::terrain::crystal[2], "terrain.crystal_2", {colors::blue_5, 0, 160}},
    block_desc{blocks::terrain::water, "terrain.water", {colors::blue_2}},
    block_desc{blocks::terrain::lava, "terrain.lava", {colors::red_5, 15, 255}},
    block_desc{blocks::terrain::magma, "terrain.magma", {colors::amber_2, 8, 120}},
    block_desc{blocks::terrain::glowstone, "terrain.glowstone", {colors::amber_5, 14, 200}},
};

namespace detail {

// Каталог правят руками, а повтор идентификатора молча отобрал бы у одного из
// двух блоков слот и имя. Квадратичная проверка на сотне записей ничего не стоит.
[[nodiscard]] consteval auto catalog_ids_unique() -> bool {
    for (std::size_t i = 0; i < default_block_catalog.size(); ++i) {
        for (std::size_t j = i + 1; j < default_block_catalog.size(); ++j) {
            if (default_block_catalog[i].id == default_block_catalog[j].id) {
                return false;
            }
        }
    }
    return true;
}

// Раздел обязан покрывать набор подряд и ровно один раз. Панель блоков идёт по
// разделам, а не по каталогу, поэтому блок вне раздела не нарисуется вовсе, а
// накрытый дважды нарисуется дважды.
[[nodiscard]] consteval auto groups_tile_sets() -> bool {
    for (const block_set& set : default_block_sets) {
        uint32 next = 1;
        for (const block_group& group : set.groups) {
            if (group.first.category() != set.category || group.first.index() != next) {
                return false;
            }
            next += group.count;
        }

        for (const block_desc& desc : default_block_catalog) {
            const bool mine = desc.id.category() == set.category && desc.id != blocks::air;
            if (mine && desc.id.index() >= next) {
                return false;
            }
        }
    }
    return true;
}

// Цвет любой записи обязан быть цветом палитры. Иначе в мире заведётся оттенок,
// которого художнику не выдать, и ландшафт начнёт спорить по тону с тем, что
// стоит на нём.
[[nodiscard]] consteval auto catalog_colours_are_palette() -> bool {
    for (const block_desc& desc : default_block_catalog) {
        if (desc.material.clr == colors::empty) {
            continue;
        }
        if (std::ranges::find(colors::all, desc.material.clr) == colors::all.end()) {
            return false;
        }
    }
    return true;
}

// Палитра обязана держать всю палитру: недостающий цвет художнику взять неоткуда,
// а понять, что его нет, можно только не найдя его в панели.
[[nodiscard]] consteval auto palette_covers_colours() -> bool {
    for (const block_set& set : default_block_sets) {
        if (set.kind != block_set_kind::palette) {
            continue;
        }

        for (const color& clr : colors::all) {
            bool found = false;
            for (const block_desc& desc : default_block_catalog) {
                if (desc.id.category() == set.category && desc.material.clr == clr) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    return true;
}

// И держать каждый ровно один раз. Два номера с одним материалом — это выбор, в
// котором художнику нечего выбирать, зато мешер их не сольёт и разведёт соседние
// грани по разным квадам. Свечение входит в материал, поэтому хвост glow-вариантов
// проверке не мешает: их цвет повторяется, а материал — нет.
[[nodiscard]] consteval auto palette_materials_unique() -> bool {
    for (const block_set& set : default_block_sets) {
        if (set.kind != block_set_kind::palette) {
            continue;
        }

        for (std::size_t i = 0; i < default_block_catalog.size(); ++i) {
            const block_desc& left = default_block_catalog[i];
            if (left.id.category() != set.category || left.id == blocks::air) {
                continue;
            }

            for (std::size_t j = i + 1; j < default_block_catalog.size(); ++j) {
                const block_desc& right = default_block_catalog[j];
                if (right.id.category() != set.category || right.id == blocks::air) {
                    continue;
                }
                if (left.material == right.material) {
                    return false;
                }
            }
        }
    }
    return true;
}

}  // namespace detail

static_assert(default_block_catalog.size() <= block_slot_capacity,
              "каталог не влезает в десять бит слота в кваде");
static_assert(detail::catalog_ids_unique(), "в каталоге повторяется идентификатор");
static_assert(detail::groups_tile_sets(), "разделы не покрывают набор подряд и без дыр");
static_assert(detail::catalog_colours_are_palette(), "в каталоге цвет мимо палитры");
static_assert(detail::palette_covers_colours(), "палитра не покрывает палитру целиком");
static_assert(detail::palette_materials_unique(), "в палитре повторяется материал");

}  // namespace vw
