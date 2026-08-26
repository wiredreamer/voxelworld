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

namespace character {
inline constexpr auto category = block_category{2};

inline constexpr auto skin_light   = block_span{category,   1, 3};
inline constexpr auto skin_tan     = block_span{category,   4, 3};
inline constexpr auto skin_dark    = block_span{category,   7, 3};
inline constexpr auto hair_black   = block_span{category,  10, 3};
inline constexpr auto hair_brown   = block_span{category,  13, 3};
inline constexpr auto hair_blond   = block_span{category,  16, 3};
inline constexpr auto hair_red     = block_span{category,  19, 3};
inline constexpr auto hair_gray    = block_span{category,  22, 3};
inline constexpr auto eye_pupil    = block_id{category,  25};
inline constexpr auto eye_white    = block_id{category,  26};
inline constexpr auto eye_iris     = block_id{category,  27};
inline constexpr auto cloth_red    = block_span{category,  28, 3};
inline constexpr auto cloth_blue   = block_span{category,  31, 3};
inline constexpr auto cloth_green  = block_span{category,  34, 3};
inline constexpr auto cloth_purple = block_span{category,  37, 3};
inline constexpr auto cloth_white  = block_span{category,  40, 3};
inline constexpr auto cloth_dark   = block_span{category,  43, 3};
inline constexpr auto leather      = block_span{category,  46, 3};
inline constexpr auto metal        = block_span{category,  49, 3};
inline constexpr auto metal_bright = block_span{category,  52, 3};
inline constexpr auto gold         = block_span{category,  55, 3};
inline constexpr auto wood         = block_span{category,  58, 3};
inline constexpr auto bowstring    = block_span{category,  61, 3};
inline constexpr auto ember        = block_span{category,  64, 3};
inline constexpr auto magic        = block_span{category,  67, 3};

inline constexpr std::array groups = {
    block_group{"skin",    skin_light[0],   9},
    block_group{"hair",    hair_black[0],  15},
    block_group{"eyes",    eye_pupil,       3},
    block_group{"cloth",   cloth_red[0],   18},
    block_group{"leather", leather[0],      3},
    block_group{"metal",   metal[0],        9},
    block_group{"wood",    wood[0],         3},
    block_group{"string",  bowstring[0],    3},
    block_group{"glow",    ember[0],        6},
};
}  // namespace character

}  // namespace vw::blocks

export namespace vw {

// Наборы, о которых знает встроенный каталог. Отсюда интерфейс берёт и список
// для выбора при создании модели, и разбиение палитры на разделы.
inline constexpr std::array default_block_sets = {
    block_set{blocks::terrain::category, "terrain", blocks::terrain::groups},
    block_set{blocks::character::category, "character", blocks::character::groups},
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
    block_desc{blocks::terrain::grass[0], "terrain.grass_0", {colors::all[8]}},
    block_desc{blocks::terrain::grass[1], "terrain.grass_1", {colors::all[9]}},
    block_desc{blocks::terrain::grass[2], "terrain.grass_2", {colors::all[10]}},
    block_desc{blocks::terrain::grass_dark[0], "terrain.grass_dark_0", {colors::all[6]}},
    block_desc{blocks::terrain::grass_dark[1], "terrain.grass_dark_1", {colors::all[7]}},
    block_desc{blocks::terrain::grass_dark[2], "terrain.grass_dark_2", {colors::all[8]}},
    block_desc{blocks::terrain::grass_dry[0], "terrain.grass_dry_0", {colors::all[9]}},
    block_desc{blocks::terrain::grass_dry[1], "terrain.grass_dry_1", {colors::all[10]}},
    block_desc{blocks::terrain::grass_dry[2], "terrain.grass_dry_2", {colors::all[11]}},
    block_desc{blocks::terrain::leaves[0], "terrain.leaves_0", {colors::all[7]}},
    block_desc{blocks::terrain::leaves[1], "terrain.leaves_1", {colors::all[8]}},
    block_desc{blocks::terrain::leaves[2], "terrain.leaves_2", {colors::all[9]}},
    block_desc{blocks::terrain::dirt[0], "terrain.dirt_0", {colors::all[12]}},
    block_desc{blocks::terrain::dirt[1], "terrain.dirt_1", {colors::all[13]}},
    block_desc{blocks::terrain::dirt[2], "terrain.dirt_2", {colors::all[14]}},
    block_desc{blocks::terrain::sand[0], "terrain.sand_0", {colors::all[15]}},
    block_desc{blocks::terrain::sand[1], "terrain.sand_1", {colors::all[16]}},
    block_desc{blocks::terrain::sand[2], "terrain.sand_2", {colors::all[17]}},
    block_desc{blocks::terrain::wood[0], "terrain.wood_0", {colors::all[18]}},
    block_desc{blocks::terrain::wood[1], "terrain.wood_1", {colors::all[19]}},
    block_desc{blocks::terrain::wood[2], "terrain.wood_2", {colors::all[20]}},
    block_desc{blocks::terrain::clay[0], "terrain.clay_0", {colors::all[20]}},
    block_desc{blocks::terrain::clay[1], "terrain.clay_1", {colors::all[21]}},
    block_desc{blocks::terrain::clay[2], "terrain.clay_2", {colors::all[22]}},
    block_desc{blocks::terrain::stone[0], "terrain.stone_0", {colors::all[40]}},
    block_desc{blocks::terrain::stone[1], "terrain.stone_1", {colors::all[41]}},
    block_desc{blocks::terrain::stone[2], "terrain.stone_2", {colors::all[42]}},
    block_desc{blocks::terrain::stone_deep[0], "terrain.stone_deep_0", {colors::all[37]}},
    block_desc{blocks::terrain::stone_deep[1], "terrain.stone_deep_1", {colors::all[38]}},
    block_desc{blocks::terrain::stone_deep[2], "terrain.stone_deep_2", {colors::all[39]}},
    block_desc{blocks::terrain::bedrock, "terrain.bedrock", {colors::all[36]}},
    block_desc{blocks::terrain::gravel[0], "terrain.gravel_0", {colors::all[39]}},
    block_desc{blocks::terrain::gravel[1], "terrain.gravel_1", {colors::all[40]}},
    block_desc{blocks::terrain::gravel[2], "terrain.gravel_2", {colors::all[41]}},
    block_desc{blocks::terrain::snow[0], "terrain.snow_0", {colors::all[44]}},
    block_desc{blocks::terrain::snow[1], "terrain.snow_1", {colors::all[45]}},
    block_desc{blocks::terrain::snow[2], "terrain.snow_2", {colors::all[46]}},
    block_desc{blocks::terrain::ice[0], "terrain.ice_0", {colors::all[3]}},
    block_desc{blocks::terrain::ice[1], "terrain.ice_1", {colors::all[4]}},
    block_desc{blocks::terrain::ice[2], "terrain.ice_2", {colors::all[5]}},
    block_desc{blocks::terrain::ash[0], "terrain.ash_0", {colors::all[36]}},
    block_desc{blocks::terrain::ash[1], "terrain.ash_1", {colors::all[37]}},
    block_desc{blocks::terrain::ash[2], "terrain.ash_2", {colors::all[38]}},
    block_desc{blocks::terrain::ore_gold[0], "terrain.ore_gold_0", {colors::all[21]}},
    block_desc{blocks::terrain::ore_gold[1], "terrain.ore_gold_1", {colors::all[22]}},
    block_desc{blocks::terrain::ore_gold[2], "terrain.ore_gold_2", {colors::all[23]}},
    block_desc{blocks::terrain::ore_iron[0], "terrain.ore_iron_0", {colors::all[41]}},
    block_desc{blocks::terrain::ore_iron[1], "terrain.ore_iron_1", {colors::all[42]}},
    block_desc{blocks::terrain::ore_iron[2], "terrain.ore_iron_2", {colors::all[43]}},
    block_desc{blocks::terrain::crystal[0], "terrain.crystal_0", {colors::all[3], 0, 160}},
    block_desc{blocks::terrain::crystal[1], "terrain.crystal_1", {colors::all[4], 0, 160}},
    block_desc{blocks::terrain::crystal[2], "terrain.crystal_2", {colors::all[5], 0, 160}},
    block_desc{blocks::terrain::water, "terrain.water", {colors::all[2]}},
    block_desc{blocks::terrain::lava, "terrain.lava", {colors::lava, 15, 255}},
    block_desc{blocks::terrain::magma, "terrain.magma", {colors::all[20], 8, 120}},
    block_desc{blocks::terrain::glowstone, "terrain.glowstone", {colors::all[23], 14, 200}},
    block_desc{blocks::character::skin_light[0], "character.skin_light_0", {colors::all[21]}},
    block_desc{blocks::character::skin_light[1], "character.skin_light_1", {colors::all[22]}},
    block_desc{blocks::character::skin_light[2], "character.skin_light_2", {colors::all[23]}},
    block_desc{blocks::character::skin_tan[0], "character.skin_tan_0", {colors::all[19]}},
    block_desc{blocks::character::skin_tan[1], "character.skin_tan_1", {colors::all[20]}},
    block_desc{blocks::character::skin_tan[2], "character.skin_tan_2", {colors::all[21]}},
    block_desc{blocks::character::skin_dark[0], "character.skin_dark_0", {colors::all[18]}},
    block_desc{blocks::character::skin_dark[1], "character.skin_dark_1", {colors::all[19]}},
    block_desc{blocks::character::skin_dark[2], "character.skin_dark_2", {colors::all[20]}},
    block_desc{blocks::character::hair_black[0], "character.hair_black_0", {colors::all[36]}},
    block_desc{blocks::character::hair_black[1], "character.hair_black_1", {colors::all[37]}},
    block_desc{blocks::character::hair_black[2], "character.hair_black_2", {colors::all[38]}},
    block_desc{blocks::character::hair_brown[0], "character.hair_brown_0", {colors::all[12]}},
    block_desc{blocks::character::hair_brown[1], "character.hair_brown_1", {colors::all[13]}},
    block_desc{blocks::character::hair_brown[2], "character.hair_brown_2", {colors::all[14]}},
    block_desc{blocks::character::hair_blond[0], "character.hair_blond_0", {colors::all[15]}},
    block_desc{blocks::character::hair_blond[1], "character.hair_blond_1", {colors::all[16]}},
    block_desc{blocks::character::hair_blond[2], "character.hair_blond_2", {colors::all[17]}},
    block_desc{blocks::character::hair_red[0], "character.hair_red_0", {colors::all[26]}},
    block_desc{blocks::character::hair_red[1], "character.hair_red_1", {colors::all[27]}},
    block_desc{blocks::character::hair_red[2], "character.hair_red_2", {colors::all[28]}},
    block_desc{blocks::character::hair_gray[0], "character.hair_gray_0", {colors::all[43]}},
    block_desc{blocks::character::hair_gray[1], "character.hair_gray_1", {colors::all[44]}},
    block_desc{blocks::character::hair_gray[2], "character.hair_gray_2", {colors::all[45]}},
    block_desc{blocks::character::eye_pupil, "character.eye_pupil", {colors::all[36]}},
    block_desc{blocks::character::eye_white, "character.eye_white", {colors::all[46]}},
    block_desc{blocks::character::eye_iris, "character.eye_iris", {colors::all[3]}},
    block_desc{blocks::character::cloth_red[0], "character.cloth_red_0", {colors::all[26]}},
    block_desc{blocks::character::cloth_red[1], "character.cloth_red_1", {colors::all[27]}},
    block_desc{blocks::character::cloth_red[2], "character.cloth_red_2", {colors::all[28]}},
    block_desc{blocks::character::cloth_blue[0], "character.cloth_blue_0", {colors::all[0]}},
    block_desc{blocks::character::cloth_blue[1], "character.cloth_blue_1", {colors::all[1]}},
    block_desc{blocks::character::cloth_blue[2], "character.cloth_blue_2", {colors::all[2]}},
    block_desc{blocks::character::cloth_green[0], "character.cloth_green_0", {colors::all[7]}},
    block_desc{blocks::character::cloth_green[1], "character.cloth_green_1", {colors::all[8]}},
    block_desc{blocks::character::cloth_green[2], "character.cloth_green_2", {colors::all[9]}},
    block_desc{blocks::character::cloth_purple[0], "character.cloth_purple_0", {colors::all[31]}},
    block_desc{blocks::character::cloth_purple[1], "character.cloth_purple_1", {colors::all[32]}},
    block_desc{blocks::character::cloth_purple[2], "character.cloth_purple_2", {colors::all[33]}},
    block_desc{blocks::character::cloth_white[0], "character.cloth_white_0", {colors::all[43]}},
    block_desc{blocks::character::cloth_white[1], "character.cloth_white_1", {colors::all[44]}},
    block_desc{blocks::character::cloth_white[2], "character.cloth_white_2", {colors::all[45]}},
    block_desc{blocks::character::cloth_dark[0], "character.cloth_dark_0", {colors::all[37]}},
    block_desc{blocks::character::cloth_dark[1], "character.cloth_dark_1", {colors::all[38]}},
    block_desc{blocks::character::cloth_dark[2], "character.cloth_dark_2", {colors::all[39]}},
    block_desc{blocks::character::leather[0], "character.leather_0", {colors::all[12]}},
    block_desc{blocks::character::leather[1], "character.leather_1", {colors::all[13]}},
    block_desc{blocks::character::leather[2], "character.leather_2", {colors::all[14]}},
    block_desc{blocks::character::metal[0], "character.metal_0", {colors::all[40]}},
    block_desc{blocks::character::metal[1], "character.metal_1", {colors::all[41]}},
    block_desc{blocks::character::metal[2], "character.metal_2", {colors::all[42]}},
    block_desc{blocks::character::metal_bright[0], "character.metal_bright_0", {colors::all[43]}},
    block_desc{blocks::character::metal_bright[1], "character.metal_bright_1", {colors::all[44]}},
    block_desc{blocks::character::metal_bright[2], "character.metal_bright_2", {colors::all[45]}},
    block_desc{blocks::character::gold[0], "character.gold_0", {colors::all[21]}},
    block_desc{blocks::character::gold[1], "character.gold_1", {colors::all[22]}},
    block_desc{blocks::character::gold[2], "character.gold_2", {colors::all[23]}},
    block_desc{blocks::character::wood[0], "character.wood_0", {colors::all[18]}},
    block_desc{blocks::character::wood[1], "character.wood_1", {colors::all[19]}},
    block_desc{blocks::character::wood[2], "character.wood_2", {colors::all[20]}},
    block_desc{blocks::character::bowstring[0], "character.bowstring_0", {colors::all[15]}},
    block_desc{blocks::character::bowstring[1], "character.bowstring_1", {colors::all[16]}},
    block_desc{blocks::character::bowstring[2], "character.bowstring_2", {colors::all[17]}},
    block_desc{blocks::character::ember[0], "character.ember_0", {colors::all[26], 6, 220}},
    block_desc{blocks::character::ember[1], "character.ember_1", {colors::all[27], 6, 220}},
    block_desc{blocks::character::ember[2], "character.ember_2", {colors::all[28], 6, 220}},
    block_desc{blocks::character::magic[0], "character.magic_0", {colors::all[33], 0, 180}},
    block_desc{blocks::character::magic[1], "character.magic_1", {colors::all[34], 0, 180}},
    block_desc{blocks::character::magic[2], "character.magic_2", {colors::all[35], 0, 180}},
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
