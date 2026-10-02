export module vw.game:input.system;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import :input.mapper;

// см. docs/ENGINE.md#удержание
export namespace vw::game {

class input_system;

struct player_input_component final {
    [[nodiscard]] auto get_frame() const -> const input_frame& {
        return frame_;
    }

    [[nodiscard]] auto hold_seconds(input_action action) const -> float32 {
        return hold_seconds_[std::to_underlying(action)];
    }

private:
    friend class input_system;

    input_frame frame_;
    input_frame incoming_;
    bool has_incoming_ = false;

    std::array<float32, input_action_count> hold_seconds_{};
};

class input_system final {
public:
    static constexpr std::string_view system_name = "input";

    explicit input_system(ecs::world& w);

    auto update(float32 delta_time) -> void;

    [[nodiscard]] auto mapper() -> input_mapper&;

    auto control_locally(ecs::entity ent) -> void;
    [[nodiscard]] auto locally_controlled() const -> ecs::entity;

    auto submit(ecs::entity ent, const input_frame& frame) -> void;

private:
    ecs::world* world_;
    input_mapper mapper_;
    ecs::entity local_ = ecs::invalid_entity;
};

}  // namespace vw::game
