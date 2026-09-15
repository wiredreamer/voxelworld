#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

using asset::fsm_compare;

auto make_fsm() -> asset::animation_fsm {
    asset::animation_fsm fsm;

    fsm.add_state({
        .name = "idle",
        .transitions =
            {
                {
                    .target_state = "walk",
                    .conditions   = {{"speed", fsm_compare::greater, 0.0F}},
                },
            },
    });

    fsm.add_state({.name = "walk"});
    fsm.add_state({.name = "hit"});
    fsm.set_entry_state("idle");

    return fsm;
}

// Слой в покое: ни блендов, ни проигрывания, поэтому wait_* ничему не мешают.
auto stopped_layer() -> asset::animation_layer {
    asset::animation_layer layer;
    layer.state = asset::animation_state::stopped;
    return layer;
}

}  // namespace

TEST_CASE("a condition compares a parameter with a value", "[fsm]") {
    asset::fsm_blackboard board;
    board.set("speed", 2.5F);

    REQUIRE(asset::fsm_condition{"speed", fsm_compare::greater, 0.0F}.holds(board));
    REQUIRE(asset::fsm_condition{"speed", fsm_compare::greater_equal, 2.5F}.holds(board));
    REQUIRE_FALSE(asset::fsm_condition{"speed", fsm_compare::equal, 0.0F}.holds(board));

    // Незаполненный параметр — ноль, а не отказ: условие по нему просто не
    // выполняется, и автомат остаётся там, где стоял.
    REQUIRE(asset::fsm_condition{"grounded", fsm_compare::equal, 0.0F}.holds(board));
    REQUIRE_FALSE(asset::fsm_condition{"grounded", fsm_compare::greater, 0.0F}.holds(board));
}

TEST_CASE("the blackboard keeps one value per name", "[fsm]") {
    asset::fsm_blackboard board;
    board.set("speed", 1.0F);
    board.set("speed", 3.0F);

    REQUIRE(board.entries().size() == 1);
    REQUIRE(board.get("speed") == 3.0F);
}

// Условия правила складываются, а не выбираются: «или» выражается двумя
// переходами в одно состояние.
TEST_CASE("every condition of a rule has to hold", "[fsm]") {
    asset::animation_fsm fsm;
    fsm.add_state({
        .name = "idle",
        .transitions =
            {
                {
                    .target_state = "jump",
                    .conditions =
                        {
                            {"jump_count", fsm_compare::equal, 0.0F},
                            {"grounded", fsm_compare::equal, 0.0F},
                        },
                },
            },
    });
    fsm.add_state({.name = "jump"});
    fsm.set_entry_state("idle");

    asset::animation_fsm::trigger_set triggers;
    asset::fsm_blackboard board;

    board.set("jump_count", 0.0F);
    board.set("grounded", 1.0F);
    REQUIRE_FALSE(fsm.evaluate(stopped_layer(), triggers, board).has_value());

    board.set("grounded", 0.0F);
    REQUIRE(fsm.evaluate(stopped_layer(), triggers, board).has_value());
}

// Переход, объявленный для всех состояний, — это и есть «важнее того, что
// сейчас играет», поэтому он проверяется раньше правил текущего состояния.
TEST_CASE("a transition from any state outruns the current one", "[fsm]") {
    auto fsm = make_fsm();
    fsm.add_any_transition({
        .target_state = "hit",
        .trigger_name = "hit",
    });

    asset::animation_fsm::trigger_set triggers{"hit"};
    asset::fsm_blackboard board;
    board.set("speed", 1.0F);

    const auto result = fsm.evaluate(stopped_layer(), triggers, board);

    REQUIRE(result.has_value());
    REQUIRE(result->target_state == "hit");

    // Сработавший триггер съеден: иначе он сработал бы ещё раз на следующем
    // кадре, уже из нового состояния.
    REQUIRE(triggers.empty());
}

TEST_CASE("a transition from any state does not fire into itself", "[fsm]") {
    auto fsm = make_fsm();
    fsm.add_any_transition({.target_state = "hit", .trigger_name = "hit"});

    fsm.apply_transition({.target_state = "hit"});

    asset::animation_fsm::trigger_set triggers{"hit"};
    asset::fsm_blackboard board;

    REQUIRE_FALSE(fsm.evaluate(stopped_layer(), triggers, board).has_value());
}

// Ни триггера, ни условий — это «когда клип доиграет»: всю работу делает
// wait_until_end, и заводить ради этого параметр «всегда» не за что.
TEST_CASE("a rule without a trigger and conditions waits for the clip", "[fsm]") {
    asset::animation_fsm fsm;
    fsm.add_state({
        .name = "attack",
        .transitions =
            {
                {.target_state = "none", .wait_until_end = true},
            },
    });
    fsm.add_state({.name = "none"});
    fsm.set_entry_state("attack");

    asset::animation_fsm::trigger_set triggers;
    asset::fsm_blackboard board;

    auto playing  = stopped_layer();
    playing.state = asset::animation_state::playing;
    REQUIRE_FALSE(fsm.evaluate(playing, triggers, board).has_value());

    REQUIRE(fsm.evaluate(stopped_layer(), triggers, board).has_value());
}
