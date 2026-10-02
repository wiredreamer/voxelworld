export module vw.game:player.control;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

export namespace vw::game {

class player_system;

struct player_component final {
    [[nodiscard]] auto has_weapon() const -> bool {
        return weapon_.is_valid();
    }

    [[nodiscard]] auto is_attacking() const -> bool {
        return attacking_;
    }

private:
    friend class player_system;

    ecs::entity body_;
    ecs::entity head_;
    ecs::entity hand_right_;
    ecs::entity hand_left_;
    ecs::entity foot_right_;
    ecs::entity foot_left_;
    ecs::entity weapon_;

    vec3f attack_facing_{0.0f, 0.0f, 0.0f};
    int32 jump_counter_ = 0;
    bool jump_pending_  = false;
    bool attacking_     = false;
};

class player_system final {
public:
    static constexpr std::string_view system_name = "player";

    player_system(ecs::world& w, asset::asset_storage& assets);

    auto update(float32 delta_time) -> void;

    [[nodiscard]] auto spawn() -> ecs::entity;

    auto toggle_weapon(ecs::entity player) -> void;

private:
    [[nodiscard]] auto create_body_part_(ecs::entity root, std::string_view part_name) const
        -> ecs::entity;
    auto attach_machines_(ecs::entity root) const -> void;

    ecs::world* world_;
    asset::asset_storage* assets_;
    std::vector<ecs::entity> toggling_;
};

}  // namespace vw::game
