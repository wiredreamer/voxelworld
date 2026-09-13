#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

auto parse_vox(std::string_view text) {
    static const block_registry registry;

    std::istringstream input{std::string{text}};
    asset::vox_parser_plain parser{registry};
    return parser.parse(input);
}

auto parse_voxa(std::string_view text) {
    std::istringstream input{std::string{text}};
    asset::voxa_deserializer deserializer;
    return deserializer.deserialize(input);
}

}  // namespace

TEST_CASE("the vox parser reads a prefab out of a stream", "[serial]") {
    const auto prefab = parse_vox(
        "# comment\n"
        "root body\n"
        "entity body\n"
        "\tt 1 2 3\t0 0 0\t1 1 1\t0 0 0\n"
        "\tm 2 2 2\n"
        "\t\tv 0 0 0 1:1\n"
        "\t\tv 1 1 1 1:13\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->root_name == "body");
    REQUIRE(prefab->entities.size() == 1);

    const auto& entity = prefab->entities.front();
    REQUIRE(entity.name == "body");
    REQUIRE(entity.has_transform);
    REQUIRE(entity.position == vec3f{1.0F, 2.0F, 3.0F});

    REQUIRE(entity.model.has_value());
    REQUIRE(entity.model->size == vec3i{2, 2, 2});
    REQUIRE(entity.model->voxels.size() == 2);
    REQUIRE(entity.model->voxels[0].second.id == blocks::terrain::grass[0]);
    REQUIRE(entity.model->voxels[1].second.id == blocks::terrain::dirt[0]);
    REQUIRE(entity.model->category == blocks::terrain::category);
}

// В 1.0 на месте блока стояло число, которое было то цветом, то индексом. Читать
// его как «категория:номер» нельзя ни при каком отображении, поэтому ответ —
// ошибка разбора, а не догадка.
TEST_CASE("a vox 1.0 voxel is a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tm 2 2 2\n"
        "\t\tv 0 0 0 0x13\n"
    );

    REQUIRE_FALSE(prefab.has_value());
}

// Модель несёт ровно один набор: страница хранит номер в наборе, а сам набор —
// у модели. Файл из двух наборов не представим, и молча взять первый значило бы
// перекрасить половину модели.
TEST_CASE("a vox model may not mix block sets", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tm 2 2 2\n"
        "\t\tv 0 0 0 1:1\n"
        "\t\tv 1 1 1 2:1\n"
    );

    REQUIRE_FALSE(prefab.has_value());
}

// Блок вне каталога рвать разбор не должен: он нарисуется заглушкой, и это
// видно сразу, а половина модели из-за одного вокселя пропасть не может.
TEST_CASE("a vox block outside the catalog still parses", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tm 2 2 2\n"
        "\t\tv 0 0 0 200:7\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.front().model->voxels.size() == 1);
}

// Разбор идёт построчно, и незнакомая команда — не повод бросать файл: так
// формат остаётся расширяемым, а старый разборщик читает новый файл.
TEST_CASE("an unknown vox command is skipped, not fatal", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tqqq 1 2 3\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

// Обрезанная строка — то, что приносит и оборванная запись, и правка руками.
// Ответ обязан быть ошибкой разбора, а не догадкой о недостающих числах.
TEST_CASE("a truncated vox transform is a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tt 1 2\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::parse_error);
}

TEST_CASE("an empty vox stream yields an empty prefab", "[serial]") {
    const auto prefab = parse_vox("");

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.empty());
}

TEST_CASE("the voxa parser reads a clip out of a stream", "[serial]") {
    const auto clip = parse_voxa(
        "# comment\n"
        "clip walk\n"
        "track body 60\n"
        "  channel position\n"
        "    k 0 0 0 0 linear 0 1\n"
        "    k 1.5 0 1 0 linear 0 1\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE(*clip != nullptr);
    REQUIRE((*clip)->get_name() == "walk");
    REQUIRE((*clip)->has_track("body"));
    REQUIRE((*clip)->get_tracks().size() == 1);
}

TEST_CASE("a truncated voxa keyframe is a parse error", "[serial]") {
    const auto clip = parse_voxa(
        "clip walk\n"
        "track body 60\n"
        "  channel position\n"
        "    k 0 0\n"
    );

    REQUIRE_FALSE(clip.has_value());
    REQUIRE(clip.error() == asset::voxa_deserializer::error_type::parse_error);
}

// То, что перебирает фаззер, но в виде, который читается глазами: разборщик
// обязан дойти до конца любого ввода и вернуть либо клип, либо ошибку.
TEST_CASE("the parsers survive rubbish", "[serial]") {
    const std::array<std::string_view, 6> rubbish{
        "\0\0\0", "clip", "track", "k k k", "v v v", "root\n\n\nentity\n",
    };

    for (const auto& text : rubbish) {
        static_cast<void>(parse_vox(text));
        static_cast<void>(parse_voxa(text));
    }

    SUCCEED("neither parser crashed");
}

// Версия в шапке — единственная защита от чтения будущего формата как мусора:
// команды у него будут другие, каждая уедет в «неизвестную», и файл откроется
// пустым вместо внятного отказа.
TEST_CASE("a vox file of an unsupported major version is rejected", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 99.0\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::unsupported_version);
}

TEST_CASE("a vox file of the current version parses", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 2.0\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

// Младший номер поднимают при добавлении команд, а незнакомую команду разборщик
// и так переживает: ронять из-за неё файл значило бы запретить формату расти.
TEST_CASE("a vox file of a newer minor version parses", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 2.7\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
}

// Без версии писались и файлы первых дней, и тексты в тестах, и буфер из
// фаззера. Отказывать им — значит требовать шапку там, где её никогда не было.
TEST_CASE("a vox file without a version header parses", "[serial]") {
    const auto prefab = parse_vox(
        "# just a comment\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.size() == 1);
}

TEST_CASE("a voxa file of an unsupported major version is rejected", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 99.0\n"
        "clip walk 60\n"
    );

    REQUIRE_FALSE(clip.has_value());
    REQUIRE(clip.error() == asset::voxa_deserializer::error_type::unsupported_version);
}

TEST_CASE("a voxa file of the current version parses", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 1.0\n"
        "clip walk 60\n"
    );

    REQUIRE(clip.has_value());
}
