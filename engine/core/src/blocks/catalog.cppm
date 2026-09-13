export module vw.core:blocks.catalog;

import std;

import :types;
import :color;
import :blocks;

export namespace vw::blocks {

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

namespace creature {
inline constexpr auto category = block_category{2};

inline constexpr auto skin_light      = block_span{category,   1, 3};
inline constexpr auto skin_tan        = block_span{category,   4, 3};
inline constexpr auto skin_dark       = block_span{category,   7, 3};
inline constexpr auto skin_gray       = block_span{category,  10, 3};
inline constexpr auto hair_black      = block_span{category,  13, 3};
inline constexpr auto hair_brown      = block_span{category,  16, 3};
inline constexpr auto hair_blond      = block_span{category,  19, 3};
inline constexpr auto hair_red        = block_span{category,  22, 3};
inline constexpr auto hair_gray       = block_span{category,  25, 3};
inline constexpr auto hair_blue       = block_span{category,  28, 3};
inline constexpr auto hair_green      = block_span{category,  31, 3};
inline constexpr auto hair_purple     = block_span{category,  34, 3};
inline constexpr auto eye_pupil       = block_id{category,  37};
inline constexpr auto eye_white       = block_id{category,  38};
inline constexpr auto eye_iris_blue   = block_id{category,  39};
inline constexpr auto eye_iris_green  = block_id{category,  40};
inline constexpr auto eye_iris_brown  = block_id{category,  41};
inline constexpr auto eye_iris_amber  = block_id{category,  42};
inline constexpr auto eye_iris_red    = block_id{category,  43};
inline constexpr auto eye_iris_violet = block_id{category,  44};
inline constexpr auto cloth_red       = block_span{category,  45, 3};
inline constexpr auto cloth_blue      = block_span{category,  48, 3};
inline constexpr auto cloth_green     = block_span{category,  51, 3};
inline constexpr auto cloth_purple    = block_span{category,  54, 3};
inline constexpr auto cloth_yellow    = block_span{category,  57, 3};
inline constexpr auto cloth_white     = block_span{category,  60, 3};
inline constexpr auto cloth_cream     = block_span{category,  63, 3};
inline constexpr auto cloth_dark      = block_span{category,  66, 3};
inline constexpr auto leather         = block_span{category,  69, 3};
inline constexpr auto metal           = block_span{category,  72, 3};
inline constexpr auto metal_bright    = block_span{category,  75, 3};
inline constexpr auto gold            = block_span{category,  78, 3};
inline constexpr auto wood            = block_span{category,  81, 3};
inline constexpr auto ember           = block_span{category,  84, 3};
inline constexpr auto magic           = block_span{category,  87, 3};

inline constexpr std::array groups = {
    block_group{"skin",    skin_light[0],    12},
    block_group{"hair",    hair_black[0],    24},
    block_group{"eyes",    eye_pupil,         8},
    block_group{"cloth",   cloth_red[0],     24},
    block_group{"leather", leather[0],        3},
    block_group{"metal",   metal[0],          9},
    block_group{"wood",    wood[0],           3},
    block_group{"light",   ember[0],          6},
};
}  // namespace creature

}  // namespace vw::blocks

export namespace vw {

// Наборы, о которых знает встроенный каталог. Отсюда интерфейс берёт и список
// для выбора при создании модели, и разбиение палитры на разделы.
inline constexpr std::array default_block_sets = {
    block_set{blocks::terrain::category, "terrain", blocks::terrain::groups},
    block_set{blocks::creature::category, "creature", blocks::creature::groups},
};

// Каталог по умолчанию. Порядок записей задаёт слоты, поэтому наборы идут
// подряд: панель блоков группирует их одним проходом, не сортируя.
//
// Нумерация внутри набора начинается с единицы: ноль означает пустоту в любом
// наборе, потому что страница хранит только номер, а набор берётся у модели.
// Нулевой байт обязан читаться как воздух — на этом стоят и таблица страниц, и
// битовые проходы по вокселям.
//
// Цвета намеренно повторяются между материалами: лёд и кристалл — одни и те же
// три шага голубой рампы, и различает их только свечение. Ради этого разведение
// личности блока и его цвета и затевалось.
inline constexpr std::array default_block_catalog = {
    block_desc{blocks::air, "air", {}, block_surface::invisible},
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
    block_desc{blocks::creature::skin_light[0], "creature.skin_light_0", {colors::amber_3}},
    block_desc{blocks::creature::skin_light[1], "creature.skin_light_1", {colors::amber_4}},
    block_desc{blocks::creature::skin_light[2], "creature.skin_light_2", {colors::amber_5}},
    block_desc{blocks::creature::skin_tan[0], "creature.skin_tan_0", {colors::amber_1}},
    block_desc{blocks::creature::skin_tan[1], "creature.skin_tan_1", {colors::amber_2}},
    block_desc{blocks::creature::skin_tan[2], "creature.skin_tan_2", {colors::amber_3}},
    block_desc{blocks::creature::skin_dark[0], "creature.skin_dark_0", {colors::amber_0}},
    block_desc{blocks::creature::skin_dark[1], "creature.skin_dark_1", {colors::amber_1}},
    block_desc{blocks::creature::skin_dark[2], "creature.skin_dark_2", {colors::amber_2}},
    block_desc{blocks::creature::skin_gray[0], "creature.skin_gray_0", {colors::gray_5}},
    block_desc{blocks::creature::skin_gray[1], "creature.skin_gray_1", {colors::gray_6}},
    block_desc{blocks::creature::skin_gray[2], "creature.skin_gray_2", {colors::gray_7}},
    block_desc{blocks::creature::hair_black[0], "creature.hair_black_0", {colors::gray_0}},
    block_desc{blocks::creature::hair_black[1], "creature.hair_black_1", {colors::gray_1}},
    block_desc{blocks::creature::hair_black[2], "creature.hair_black_2", {colors::gray_2}},
    block_desc{blocks::creature::hair_brown[0], "creature.hair_brown_0", {colors::brown_0}},
    block_desc{blocks::creature::hair_brown[1], "creature.hair_brown_1", {colors::brown_1}},
    block_desc{blocks::creature::hair_brown[2], "creature.hair_brown_2", {colors::brown_2}},
    block_desc{blocks::creature::hair_blond[0], "creature.hair_blond_0", {colors::brown_3}},
    block_desc{blocks::creature::hair_blond[1], "creature.hair_blond_1", {colors::brown_4}},
    block_desc{blocks::creature::hair_blond[2], "creature.hair_blond_2", {colors::brown_5}},
    block_desc{blocks::creature::hair_red[0], "creature.hair_red_0", {colors::red_2}},
    block_desc{blocks::creature::hair_red[1], "creature.hair_red_1", {colors::red_3}},
    block_desc{blocks::creature::hair_red[2], "creature.hair_red_2", {colors::red_4}},
    block_desc{blocks::creature::hair_gray[0], "creature.hair_gray_0", {colors::gray_7}},
    block_desc{blocks::creature::hair_gray[1], "creature.hair_gray_1", {colors::gray_8}},
    block_desc{blocks::creature::hair_gray[2], "creature.hair_gray_2", {colors::gray_9}},
    block_desc{blocks::creature::hair_blue[0], "creature.hair_blue_0", {colors::blue_2}},
    block_desc{blocks::creature::hair_blue[1], "creature.hair_blue_1", {colors::blue_3}},
    block_desc{blocks::creature::hair_blue[2], "creature.hair_blue_2", {colors::blue_4}},
    block_desc{blocks::creature::hair_green[0], "creature.hair_green_0", {colors::green_2}},
    block_desc{blocks::creature::hair_green[1], "creature.hair_green_1", {colors::green_3}},
    block_desc{blocks::creature::hair_green[2], "creature.hair_green_2", {colors::green_4}},
    block_desc{blocks::creature::hair_purple[0], "creature.hair_purple_0", {colors::purple_2}},
    block_desc{blocks::creature::hair_purple[1], "creature.hair_purple_1", {colors::purple_3}},
    block_desc{blocks::creature::hair_purple[2], "creature.hair_purple_2", {colors::purple_4}},
    block_desc{blocks::creature::eye_pupil, "creature.eye_pupil", {colors::gray_0}},
    block_desc{blocks::creature::eye_white, "creature.eye_white", {colors::white}},
    block_desc{blocks::creature::eye_iris_blue, "creature.eye_iris_blue", {colors::blue_3}},
    block_desc{blocks::creature::eye_iris_green, "creature.eye_iris_green", {colors::green_3}},
    block_desc{blocks::creature::eye_iris_brown, "creature.eye_iris_brown", {colors::brown_2}},
    block_desc{blocks::creature::eye_iris_amber, "creature.eye_iris_amber", {colors::amber_4}},
    block_desc{blocks::creature::eye_iris_red, "creature.eye_iris_red", {colors::red_3}},
    block_desc{blocks::creature::eye_iris_violet, "creature.eye_iris_violet", {colors::purple_3}},
    block_desc{blocks::creature::cloth_red[0], "creature.cloth_red_0", {colors::red_2}},
    block_desc{blocks::creature::cloth_red[1], "creature.cloth_red_1", {colors::red_3}},
    block_desc{blocks::creature::cloth_red[2], "creature.cloth_red_2", {colors::red_4}},
    block_desc{blocks::creature::cloth_blue[0], "creature.cloth_blue_0", {colors::blue_0}},
    block_desc{blocks::creature::cloth_blue[1], "creature.cloth_blue_1", {colors::blue_1}},
    block_desc{blocks::creature::cloth_blue[2], "creature.cloth_blue_2", {colors::blue_2}},
    block_desc{blocks::creature::cloth_green[0], "creature.cloth_green_0", {colors::green_1}},
    block_desc{blocks::creature::cloth_green[1], "creature.cloth_green_1", {colors::green_2}},
    block_desc{blocks::creature::cloth_green[2], "creature.cloth_green_2", {colors::green_3}},
    block_desc{blocks::creature::cloth_purple[0], "creature.cloth_purple_0", {colors::purple_1}},
    block_desc{blocks::creature::cloth_purple[1], "creature.cloth_purple_1", {colors::purple_2}},
    block_desc{blocks::creature::cloth_purple[2], "creature.cloth_purple_2", {colors::purple_3}},
    block_desc{blocks::creature::cloth_yellow[0], "creature.cloth_yellow_0", {colors::amber_3}},
    block_desc{blocks::creature::cloth_yellow[1], "creature.cloth_yellow_1", {colors::amber_4}},
    block_desc{blocks::creature::cloth_yellow[2], "creature.cloth_yellow_2", {colors::amber_5}},
    block_desc{blocks::creature::cloth_white[0], "creature.cloth_white_0", {colors::gray_7}},
    block_desc{blocks::creature::cloth_white[1], "creature.cloth_white_1", {colors::gray_8}},
    block_desc{blocks::creature::cloth_white[2], "creature.cloth_white_2", {colors::gray_9}},
    block_desc{blocks::creature::cloth_cream[0], "creature.cloth_cream_0", {colors::brown_3}},
    block_desc{blocks::creature::cloth_cream[1], "creature.cloth_cream_1", {colors::brown_4}},
    block_desc{blocks::creature::cloth_cream[2], "creature.cloth_cream_2", {colors::brown_5}},
    block_desc{blocks::creature::cloth_dark[0], "creature.cloth_dark_0", {colors::gray_1}},
    block_desc{blocks::creature::cloth_dark[1], "creature.cloth_dark_1", {colors::gray_2}},
    block_desc{blocks::creature::cloth_dark[2], "creature.cloth_dark_2", {colors::gray_3}},
    block_desc{blocks::creature::leather[0], "creature.leather_0", {colors::brown_0}},
    block_desc{blocks::creature::leather[1], "creature.leather_1", {colors::brown_1}},
    block_desc{blocks::creature::leather[2], "creature.leather_2", {colors::brown_2}},
    block_desc{blocks::creature::metal[0], "creature.metal_0", {colors::gray_4}},
    block_desc{blocks::creature::metal[1], "creature.metal_1", {colors::gray_5}},
    block_desc{blocks::creature::metal[2], "creature.metal_2", {colors::gray_6}},
    block_desc{blocks::creature::metal_bright[0], "creature.metal_bright_0", {colors::gray_7}},
    block_desc{blocks::creature::metal_bright[1], "creature.metal_bright_1", {colors::gray_8}},
    block_desc{blocks::creature::metal_bright[2], "creature.metal_bright_2", {colors::gray_9}},
    block_desc{blocks::creature::gold[0], "creature.gold_0", {colors::amber_3}},
    block_desc{blocks::creature::gold[1], "creature.gold_1", {colors::amber_4}},
    block_desc{blocks::creature::gold[2], "creature.gold_2", {colors::amber_5}},
    block_desc{blocks::creature::wood[0], "creature.wood_0", {colors::amber_0}},
    block_desc{blocks::creature::wood[1], "creature.wood_1", {colors::amber_1}},
    block_desc{blocks::creature::wood[2], "creature.wood_2", {colors::amber_2}},
    block_desc{blocks::creature::ember[0], "creature.ember_0", {colors::red_2, 6, 220}},
    block_desc{blocks::creature::ember[1], "creature.ember_1", {colors::red_3, 6, 220}},
    block_desc{blocks::creature::ember[2], "creature.ember_2", {colors::red_4, 6, 220}},
    block_desc{blocks::creature::magic[0], "creature.magic_0", {colors::purple_3, 0, 180}},
    block_desc{blocks::creature::magic[1], "creature.magic_1", {colors::purple_4, 0, 180}},
    block_desc{blocks::creature::magic[2], "creature.magic_2", {colors::purple_5, 0, 180}},
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

}  // namespace detail

static_assert(default_block_catalog.size() <= block_slot_capacity,
              "каталог не влезает в десять бит слота в кваде");
static_assert(detail::catalog_ids_unique(), "в каталоге повторяется идентификатор");
static_assert(detail::groups_tile_sets(), "разделы не покрывают набор подряд и без дыр");

}  // namespace vw
