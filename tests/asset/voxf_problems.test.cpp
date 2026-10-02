#include <catch2/catch_test_macros.hpp>

import std;

import vw.core;
import vw.asset;

using namespace vw;

namespace {

using rule = asset::animation_fsm::transition_rule;

auto sound_machine() -> asset::voxf_data {
    asset::voxf_data data;
    data.rig         = "humanoid";
    data.entry_state = "idle";
    data.params      = {
        {.name = "speed", .type = asset::voxf_param_type::real, .value = 0.F},
        {.name = "attack", .type = asset::voxf_param_type::trigger, .value = 0.F},
    };

    asset::voxf_state idle{.name = "idle"};
    idle.transitions.push_back(rule{
        .target_state = "walk",
        .conditions   = {{.parameter = "speed", .compare = asset::fsm_compare::greater, .value = 0.F}},
    });
    idle.transitions.push_back(rule{.target_state = "walk", .trigger_name = "attack"});

    asset::voxf_state walk{.name = "walk"};
    walk.transitions.push_back(rule{.target_state = "idle", .wait_until_end = true});

    data.states = {idle, walk};
    data.any_transitions.push_back(rule{.target_state = "idle", .trigger_name = "attack"});
    return data;
}

auto mentions(const std::vector<std::string>& problems, std::string_view text) -> bool {
    return std::ranges::any_of(problems, [text](const std::string& problem) {
        return problem.contains(text);
    });
}

}  // namespace

TEST_CASE("a sound machine has no problems", "[voxf_problems]") {
    CHECK(asset::find_problems(sound_machine()).empty());
}

TEST_CASE("a machine without states is a problem", "[voxf_problems]") {
    asset::voxf_data data;
    data.entry_state = "idle";

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 1);
    CHECK(problems.front() == "the machine has no states");
}

TEST_CASE("an entry that is not a state is named with the states there are", "[voxf_problems]") {
    auto data        = sound_machine();
    data.entry_state = "run";

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 1);
    CHECK(problems.front() == "the entry 'run' is not a state; the states are: idle, walk");
}

TEST_CASE("a state declared twice is reported once", "[voxf_problems]") {
    auto data = sound_machine();
    data.states.push_back(asset::voxf_state{.name = "walk"});

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 1);
    CHECK(problems.front() == "the state 'walk' is declared 2 times");
}

TEST_CASE("a parameter declared twice is reported once", "[voxf_problems]") {
    auto data = sound_machine();
    data.params.push_back({.name = "speed", .type = asset::voxf_param_type::integer, .value = 0.F});

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 1);
    CHECK(problems.front() == "the parameter 'speed' is declared 2 times");
}

TEST_CASE("names of states and parameters are one word", "[voxf_problems]") {
    auto data = sound_machine();
    data.states.push_back(asset::voxf_state{.name = "long jump"});
    data.params.push_back({.name = "", .type = asset::voxf_param_type::real, .value = 0.F});
    data.rig = "two words";

    const auto problems = asset::find_problems(data);

    CHECK(mentions(problems, "the name 'long jump' must be one word"));
    CHECK(mentions(problems, "parameter 2: the name '' must be one word"));
    CHECK(mentions(problems, "the rig 'two words' must be one word"));
}

TEST_CASE("a transition to a state that is not there says where it is", "[voxf_problems]") {
    auto data                                    = sound_machine();
    data.states[1].transitions[0].target_state   = "idel";
    data.any_transitions[0].target_state         = "nowhere";

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 2);
    CHECK(problems[0] ==
          "state 'walk', transition 0: the target 'idel' is not a state; the states are: idle, walk");
    CHECK(mentions(problems, "any transition 0: the target 'nowhere' is not a state"));
}

TEST_CASE("a trigger must be a trigger parameter", "[voxf_problems]") {
    auto data                                  = sound_machine();
    data.states[0].transitions[1].trigger_name = "speed";

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 1);
    CHECK(mentions(problems, "'speed' is not a trigger parameter; the triggers are: attack"));
}

TEST_CASE("a condition must read a value parameter", "[voxf_problems]") {
    auto data = sound_machine();
    data.states[0].transitions[0].conditions[0].parameter = "attack";
    data.states[1].transitions[0].conditions.push_back(
        {.parameter = "stamina", .compare = asset::fsm_compare::less, .value = 1.F}
    );

    const auto problems = asset::find_problems(data);

    REQUIRE(problems.size() == 2);
    CHECK(mentions(problems, "the condition reads 'attack', which is not a value parameter"));
    CHECK(mentions(problems, "the condition reads 'stamina'"));
    CHECK(mentions(problems, "the value parameters are: speed"));
}

TEST_CASE("rates and times must make sense", "[voxf_problems]") {
    auto data                                    = sound_machine();
    data.states[0].rate                          = 0.F;
    data.states[1].fade_out.duration             = -0.5F;
    data.states[1].transitions[0].blend.duration = -1.F;

    const auto problems = asset::find_problems(data);

    CHECK(problems.size() == 3);
    CHECK(mentions(problems, "state 'idle': the rate must be above zero"));
    CHECK(mentions(problems, "state 'walk': a fade cannot last a negative time"));
    CHECK(mentions(problems, "state 'walk', transition 0: a blend cannot last a negative time"));
}

TEST_CASE("the machines shipped with the game have no problems", "[voxf_problems]") {
    const std::filesystem::path fsm_dir = std::filesystem::path{VW_ASSET_DIR} / "fsm";
    REQUIRE(std::filesystem::is_directory(fsm_dir));

    std::size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator{fsm_dir}) {
        if (entry.path().extension() != ".voxf") {
            continue;
        }

        INFO(entry.path().filename().string());

        const auto data = asset::voxf_deserializer{}.deserialize(entry.path());
        REQUIRE(data.has_value());
        CHECK(asset::find_problems(*data).empty());
        ++checked;
    }
    CHECK(checked >= 2);
}
