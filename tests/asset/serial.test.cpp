#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

auto parse_vox(std::string_view text) {
    std::istringstream input{std::string{text}};
    asset::vox_parser_plain parser;
    return parser.parse(input);
}

auto parse_voxa(std::string_view text) {
    std::istringstream input{std::string{text}};
    asset::voxa_deserializer deserializer;
    return deserializer.deserialize(input);
}

}  // namespace

// Узлы взяты из assets/prefabs/m_human.vox дословно, вплоть до «-0» и
// табуляций. Разборщик не знает ни одного из этих тегов: он видит имя, остаток
// строки и глубину отступа, а смысл придаёт читатель.
TEST_CASE("the vox parser reads a prefab out of a stream", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "root root\n"
        "entity root\n"
        "\ttransform 0 0 0\t-0 -0 -0\t1 1 1\n"
        "entity hand_right\n"
        "\tparent root\n"
        "\ttransform 10 2 0\t1.5707963 -0 -0\t1 1 1\n"
        "\tmodel models/m_human/hand_right.voxm\n"
        "\tanim_target hand_right\n"
        "\tsocket hand_right\n"
        "\t\tpos 0 0 0\n"
        "\t\trot -1.570796 -0 -0\n"
        "\t\tscale 0.99 0.99 0.99\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->root_name == "root");
    REQUIRE(prefab->rig == "humanoid");
    REQUIRE(prefab->entities.size() == 2);

    const auto& root = prefab->entities.front();
    REQUIRE(root.value_of("transform") == "0 0 0\t-0 -0 -0\t1 1 1");
    REQUIRE(root.find("model") == nullptr);

    const auto& hand = prefab->entities.back();
    REQUIRE(hand.parent_name == "root");
    REQUIRE(hand.value_of("transform") == "10 2 0\t1.5707963 -0 -0\t1 1 1");
    REQUIRE(hand.value_of("model") == "models/m_human/hand_right.voxm");
    REQUIRE(hand.value_of("anim_target") == "hand_right");

    const auto* socket = hand.find("socket");
    REQUIRE(socket != nullptr);
    REQUIRE(socket->value == "hand_right");
    REQUIRE(socket->prop("pos") == "0 0 0");
    REQUIRE(socket->prop("scale") == "0.99 0.99 0.99");
}

// Тег, которого не знает никто, обязан дойти до записи целым: иначе открыть
// префаб сборкой без нужного компонента и сохранить — значит потерять его.
TEST_CASE("an unknown tag is carried through untouched", "[serial]") {
    const auto first = parse_vox(
        "root body\n"
        "entity body\n"
        "\tlight point\n"
        "\t\tcolor 255 200 120\n"
        "\t\trange 12\n"
    );

    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);

    const auto* light = second->entities.front().find("light");
    REQUIRE(light != nullptr);
    REQUIRE(light->value == "point");
    REQUIRE(light->prop("range") == "12");
}

// Повтор тега — это список, и порядок в нём значим: сокеты обязаны вернуться
// теми же и в том же порядке.
TEST_CASE("a repeated tag is a list", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\tsocket hand_right\n"
        "\t\tpos 1 0 0\n"
        "\tsocket hand_left\n"
        "\t\tpos -1 0 0\n"
    );

    REQUIRE(prefab.has_value());

    const auto& tags = prefab->entities.front().tags;
    REQUIRE(tags.size() == 2);
    REQUIRE(tags.front().value == "hand_right");
    REQUIRE(tags.front().prop("pos") == "1 0 0");
    REQUIRE(tags.back().value == "hand_left");
    REQUIRE(tags.back().prop("pos") == "-1 0 0");
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
// Ошибкой разбора осталась только поломанная структура: свойство без тега
// повисает в воздухе, и догадываться, к чему оно относится, разборщик не станет.
TEST_CASE("a property without a tag is a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\t\tcolor 1 2 3\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::parse_error);
}

// Усечённый трансформ разборщика больше не касается: сколько чисел полагается
// тегу, знает читатель. Строка доезжает до него как есть, и это его дело —
// понять, что чисел мало.
TEST_CASE("a truncated transform is not a parse error", "[serial]") {
    const auto prefab = parse_vox(
        "root body\n"
        "entity body\n"
        "\ttransform 1 2\n"
    );

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.front().value_of("transform") == "1 2");

    std::array<float32, 9> values{};
    REQUIRE_FALSE(asset::parse_floats("1 2", values));
}

TEST_CASE("an empty vox stream yields an empty prefab", "[serial]") {
    const auto prefab = parse_vox("");

    REQUIRE(prefab.has_value());
    REQUIRE(prefab->entities.empty());
}

// Разбор и запись — две половины одного формата, и держатся они только тем, что
// лежат рядом. Поле, которое читается, но не пишется, не даёт ни ошибки, ни
// лога: оно просто исчезает при следующем сохранении.
TEST_CASE("a prefab survives a write and a read", "[serial]") {
    constexpr std::string_view source =
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "root root\n"
        "entity root\n"
        "\ttransform 0 0 0\t-0 -0 -0\t1 1 1\n"
        "entity hand_right\n"
        "\tparent root\n"
        "\ttransform 10 2 0\t1.5707963 -0 -0\t1 1 1\n"
        "\tmodel models/m_human/hand_right.voxm\n"
        "\tanim_target hand_right\n"
        "\tsocket hand_right\n"
        "\t\tpos 0 0 0\n"
        "\t\trot -1.570796 -0 -0\n"
        "\t\tscale 0.99 0.99 0.99\n";

    const auto first = parse_vox(source);
    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);
}

// Порядок ссылок на автоматы — это номера слоёв, поэтому повтор тега здесь не
// просто список, а список упорядоченный, и запись обязана его сохранить.
TEST_CASE("a prefab names its state machines in layer order", "[serial]") {
    constexpr std::string_view source =
        "# Vox File Version 4.0\n"
        "rig humanoid\n"
        "fsm fsm/humanoid_locomotion.voxf\n"
        "fsm fsm/humanoid_action.voxf\n"
        "root root\n"
        "entity root\n";

    const auto first = parse_vox(source);
    REQUIRE(first.has_value());
    REQUIRE(first->fsm_refs.size() == 2);
    REQUIRE(first->fsm_refs[0] == asset::asset_ref{"fsm/humanoid_locomotion.voxf"});
    REQUIRE(first->fsm_refs[1] == asset::asset_ref{"fsm/humanoid_action.voxf"});

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(second->fsm_refs == first->fsm_refs);
}

// Узел без единого тега — тоже узел: пустые поля не должны превращаться в
// строки, которые разбор потом прочтёт как настоящие.
TEST_CASE("a bare node survives a write and a read", "[serial]") {
    const auto first = parse_vox(
        "# Vox File Version 4.0\n"
        "root root\n"
        "entity root\n"
    );
    REQUIRE(first.has_value());

    std::ostringstream written;
    asset::vox_writer_plain writer;
    REQUIRE(writer.write(written, *first).has_value());

    const auto second = parse_vox(written.str());
    REQUIRE(second.has_value());
    REQUIRE(*second == *first);
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

// Файл 3.1 нёс сокеты плоской строкой с позиционными числами. Читать его больше
// нечем, и молчать об этом нельзя: без проверки версии сокет ушёл бы в тег с
// девятью числами в значении, а префаб открылся бы наполовину.
TEST_CASE("a vox 3.1 file is rejected by version", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 3.1\n"
        "root body\n"
        "entity body\n"
    );

    REQUIRE_FALSE(prefab.has_value());
    REQUIRE(prefab.error() == asset::vox_parser::error_type::unsupported_version);
}

TEST_CASE("a vox file of the current version parses", "[serial]") {
    const auto prefab = parse_vox(
        "# Vox File Version 4.0\n"
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
        "# Vox File Version 4.7\n"
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
        "# Voxa File Version 1.1\n"
        "clip walk 60\n"
        "rig humanoid\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE((*clip)->get_rig() == "humanoid");
}

// Риг появился в 1.1 и остался необязательным, поэтому клип 1.0 читается как
// «риг не указан», а не отвергается: пять записанных до этого клипов никуда не
// делись.
TEST_CASE("a voxa file without a rig parses as one without a rig", "[serial]") {
    const auto clip = parse_voxa(
        "# Voxa File Version 1.0\n"
        "clip walk 60\n"
    );

    REQUIRE(clip.has_value());
    REQUIRE((*clip)->get_rig().empty());
}

namespace {

auto parse_voxm(asset::model_registry& registry, std::string_view text) {
    static const block_registry blocks;

    std::istringstream input{std::string{text}};
    asset::voxm_deserializer deserializer{registry, blocks};
    return deserializer.deserialize(input);
}

auto write_voxm(const asset::model& model) -> std::string {
    std::ostringstream output;
    asset::voxm_serializer{model}.serialize(output);
    return output.str();
}

}  // namespace

TEST_CASE("a voxm volume survives a round trip", "[serial]") {
    asset::model_registry registry;

    const auto source = registry.create_unnamed(blocks::creature::category, vec3i{6, 4, 3});
    source->set_pivot(vec3f{2.5F, 1.5F, 0.5F});
    source->set_voxel(vec3i{0, 0, 0}, voxel{blocks::creature::cloth_white[0]});
    source->set_voxel(vec3i{1, 0, 0}, voxel{blocks::creature::cloth_white[0]});
    source->set_voxel(vec3i{2, 0, 0}, voxel{blocks::creature::cloth_white[1]});
    source->set_voxel(vec3i{5, 3, 2}, voxel{blocks::creature::cloth_white[2]});

    const auto restored = parse_voxm(registry, write_voxm(*source));

    REQUIRE(restored.has_value());

    const auto& model = **restored;
    REQUIRE(model.size() == source->size());
    REQUIRE(model.category() == source->category());
    REQUIRE(model.pivot() == source->pivot());

    for (int32 z = 0; z < model.depth(); ++z) {
        for (int32 y = 0; y < model.height(); ++y) {
            for (int32 x = 0; x < model.width(); ++x) {
                REQUIRE(model.get_voxel(x, y, z).id == source->get_voxel(x, y, z).id);
            }
        }
    }
}

// Пробег — единственная форма записи вокселя, и строка из одного блока обязана
// стать одной строкой файла, иначе разделение форматов не окупается.
TEST_CASE("a voxm run collapses a row of one block", "[serial]") {
    asset::model_registry registry;

    const auto source = registry.create_unnamed(blocks::terrain::category, vec3i{8, 1, 1});
    for (int32 x = 0; x < 8; ++x) {
        source->set_voxel(vec3i{x, 0, 0}, voxel{blocks::terrain::grass[0]});
    }

    const auto text = write_voxm(*source);

    std::istringstream lines{text};
    std::string line;
    int32 runs = 0;
    while (std::getline(lines, line)) {
        if (line.starts_with("r ")) {
            ++runs;
        }
    }

    REQUIRE(runs == 1);
    REQUIRE(text.contains("r 0 0 0 8 "));
}

TEST_CASE("an empty voxm volume is a valid file", "[serial]") {
    asset::model_registry registry;

    const auto source   = registry.create_unnamed(blocks::creature::category, vec3i{4, 4, 4});
    const auto restored = parse_voxm(registry, write_voxm(*source));

    REQUIRE(restored.has_value());
    REQUIRE((*restored)->size() == vec3i{4, 4, 4});
}

TEST_CASE("a voxm file of an unsupported major version is rejected", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "# Voxm File Version 99.0\n"
        "category 2\n"
        "size 2 2 2\n"
    );

    REQUIRE_FALSE(restored.has_value());
    REQUIRE(restored.error() == asset::voxm_deserializer::error_type::unsupported_version);
}

// Пробег за границей объёма — это испорченный файл, а не повод писать мимо
// страниц: молча обрезать его значило бы тихо потерять часть модели.
TEST_CASE("a voxm run outside the volume is a parse error", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "category 2\n"
        "size 2 2 2\n"
        "r 0 0 0 5 51\n"
    );

    REQUIRE_FALSE(restored.has_value());
}

TEST_CASE("a voxm file without a size is a parse error", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(registry, "category 2\n");

    REQUIRE_FALSE(restored.has_value());
}

// Блок вне каталога рвать чтение не должен: он нарисуется заглушкой, и это
// видно сразу, а половина модели из-за одного номера пропасть не может.
TEST_CASE("a voxm block outside the catalog still parses", "[serial]") {
    asset::model_registry registry;

    const auto restored = parse_voxm(
        registry,
        "category 200\n"
        "size 2 2 2\n"
        "r 0 0 0 1 7\n"
    );

    REQUIRE(restored.has_value());
    REQUIRE((*restored)->get_voxel(0, 0, 0).id == block_id{block_category{200}, 7});
}
