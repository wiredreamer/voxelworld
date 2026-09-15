#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

using asset::fsm_compare;

auto parse(std::string_view text) -> asset::voxf_data {
    std::istringstream input{std::string{text}};
    auto result = asset::voxf_deserializer{}.deserialize(input);

    REQUIRE(result.has_value());
    return std::move(*result);
}

auto round_trip(const asset::voxf_data& data) -> asset::voxf_data {
    std::ostringstream output;
    REQUIRE(asset::voxf_serializer{data}.serialize(output).has_value());

    return parse(output.str());
}

constexpr std::string_view locomotion = R"(# Voxf File Version 1.0
rig humanoid
entry idle
param speed float
param grounded bool true
param jump_count int
param hit trigger
state idle
	clip animations/a_idle.voxa
	playback loop
	to walk
		when speed > 0
		blend 0.15
	to jump
		when jump_count == 0
		when grounded == false
		blend 0.15
		wait blend
state walk
	clip animations/a_walk.voxa
	playback loop
	rate 1.25
	to idle
		when speed == 0
		blend 0.15
state jump
	clip animations/a_jump.voxa
	playback once
	fade_in 0.25
	fade_out 0.35 ease_in_out
	to idle
		when grounded > 0
		blend 0.25
		wait end
any
	to hit_reaction
		on hit
		blend 0.15
state hit_reaction
	clip animations/a_hit.voxa
	playback once
	to idle
		wait end
)";

}  // namespace

TEST_CASE("a voxf file reads into states, transitions and parameters", "[voxf]") {
    const auto data = parse(locomotion);

    REQUIRE(data.rig == "humanoid");
    REQUIRE(data.entry_state == "idle");
    REQUIRE(data.states.size() == 4);

    REQUIRE(data.params.size() == 4);
    REQUIRE(data.params[1].name == "grounded");
    REQUIRE(data.params[1].type == asset::voxf_param_type::boolean);
    REQUIRE(data.params[3].type == asset::voxf_param_type::trigger);

    const auto& idle = data.states[0];
    REQUIRE(idle.name == "idle");
    REQUIRE(idle.clip == asset::asset_ref{"animations/a_idle.voxa"});
    REQUIRE(idle.loop_mode == asset::animation_loop_mode::loop);
    REQUIRE(idle.transitions.size() == 2);

    // Условия правила складываются: у прыжка их два, и оба стоят отдельными
    // строками — повтор тега это список.
    const auto& to_jump = idle.transitions[1];
    REQUIRE(to_jump.conditions.size() == 2);
    REQUIRE(to_jump.conditions[0] == asset::fsm_condition{"jump_count", fsm_compare::equal, 0.0F});
    REQUIRE(to_jump.conditions[1] == asset::fsm_condition{"grounded", fsm_compare::equal, 0.0F});
    REQUIRE(to_jump.wait_until_blend);
    REQUIRE_FALSE(to_jump.wait_until_end);

    REQUIRE(data.states[1].rate == 1.25F);
    REQUIRE(data.states[2].loop_mode == asset::animation_loop_mode::once);
    REQUIRE(data.states[2].fade_in.duration == 0.25F);
    REQUIRE(data.states[2].fade_out.interp == math::interpolation_type::ease_in_out);
}

// Блок any стоит на верхнем уровне, а не внутри состояния: правило из него
// относится ко всем, и приписывать его одному состоянию значило бы соврать.
TEST_CASE("a voxf file keeps transitions from any state apart", "[voxf]") {
    const auto data = parse(locomotion);

    REQUIRE(data.any_transitions.size() == 1);
    REQUIRE(data.any_transitions[0].target_state == "hit_reaction");
    REQUIRE(data.any_transitions[0].trigger_name == "hit");

    // Блок any не съел состояние, идущее за ним.
    REQUIRE(data.states[3].name == "hit_reaction");
    REQUIRE(data.states[3].transitions.size() == 1);
}

TEST_CASE("a voxf file survives a write and a read back", "[voxf]") {
    const auto data = parse(locomotion);

    REQUIRE(round_trip(data) == data);
}

// Побайтового совпадения от записи не требуется, но объявленный bool должен
// вернуться словом: иначе файл после сохранения читался бы хуже, чем до него.
TEST_CASE("a voxf writer spells a declared bool with a word", "[voxf]") {
    const auto data = parse(locomotion);

    std::ostringstream output;
    REQUIRE(asset::voxf_serializer{data}.serialize(output).has_value());

    const auto text = output.str();
    REQUIRE(text.contains("when grounded == false"));
    REQUIRE(text.contains("when jump_count == 0"));
}

TEST_CASE("a voxf file of a future major version is refused", "[voxf]") {
    std::istringstream input{"# Voxf File Version 2.0\nstate idle\n"};

    const auto result = asset::voxf_deserializer{}.deserialize(input);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == asset::voxf_deserializer::error_type::unsupported_version);
}

// Словарь автомата закрыт, поэтому кривое условие — ошибка, а не тег, который
// проносят нетронутым: иначе опечатка стала бы переходом, который не случается.
TEST_CASE("a malformed condition is a parse error", "[voxf]") {
    std::istringstream input{"state idle\n\tto walk\n\t\twhen speed => 1\n"};

    const auto result = asset::voxf_deserializer{}.deserialize(input);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == asset::voxf_deserializer::error_type::parse_error);
}

TEST_CASE("a voxf file becomes a running state machine", "[voxf]") {
    const auto data = parse(locomotion);

    auto fsm = asset::build_fsm(data, {});
    REQUIRE(fsm.get_current_state() == "idle");

    asset::fsm_blackboard board;
    asset::apply_defaults(data, board);

    // Объявленные параметры лежат на доске с первого кадра, а триггер — нет: он
    // живёт в наборе сработавших.
    REQUIRE(board.entries().size() == 3);

    asset::animation_layer layer;
    layer.state = asset::animation_state::stopped;
    asset::animation_fsm::trigger_set triggers;

    REQUIRE_FALSE(fsm.evaluate(layer, triggers, board).has_value());

    board.set("speed", 1.0F);
    const auto walking = fsm.evaluate(layer, triggers, board);
    REQUIRE(walking.has_value());
    REQUIRE(walking->target_state == "walk");
    REQUIRE(walking->playback_speed == 1.25F);
    REQUIRE(walking->blend.duration == 0.15F);

    fsm.apply_transition(*walking);

    // Переход из блока any обгоняет правила состояния, в котором мы стоим.
    triggers.insert("hit");
    const auto hit = fsm.evaluate(layer, triggers, board);
    REQUIRE(hit.has_value());
    REQUIRE(hit->target_state == "hit_reaction");
}
