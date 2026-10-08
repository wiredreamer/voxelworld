export module vw.game:combat.projectile;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;

// см. docs/ENGINE.md#снаряды
export namespace vw::game {

class projectile_system;

struct projectile_tuning final {
    float32 gravity_scale  = 0.4f;
    float32 flight_seconds = 6.0f;
    float32 stuck_seconds  = 20.0f;
    float32 sink_units     = 4.0f;
    uint32 stuck_limit     = 32;
};

struct projectile_launch final {
    std::shared_ptr<asset::model> model;
    vec3f position{0.0f, 0.0f, 0.0f};
    vec3f velocity{0.0f, 0.0f, 1.0f};
    ecs::entity owner;
};

struct projectile_hit final {
    ecs::entity projectile;
    ecs::entity owner;
    ecs::entity target;
    ecs::entity part;
    vec3f point{0.0f, 0.0f, 0.0f};
    vec3f velocity{0.0f, 0.0f, 0.0f};
};

struct projectile_component final {
    [[nodiscard]] auto get_owner() const -> ecs::entity {
        return owner_;
    }

    [[nodiscard]] auto get_velocity() const -> const vec3f& {
        return velocity_;
    }

    [[nodiscard]] auto is_stuck() const -> bool {
        return stuck_;
    }

    [[nodiscard]] auto get_stuck_in() const -> ecs::entity {
        return stuck_in_;
    }

    [[nodiscard]] auto get_age_seconds() const -> float32 {
        return age_seconds_;
    }

private:
    friend class projectile_system;

    ecs::entity owner_;
    ecs::entity stuck_in_;
    vec3f velocity_{0.0f, 0.0f, 0.0f};
    vec3f held_at_{0.0f, 0.0f, 0.0f};
    quat held_turn_{0.0f, 0.0f, 0.0f, 1.0f};
    float32 age_seconds_ = 0.0f;
    bool stuck_          = false;
};

class projectile_system final {
public:
    static constexpr std::string_view system_name = "projectile";

    explicit projectile_system(ecs::world& w);

    auto update(float32 delta_time) -> void;

    auto launch(const projectile_launch& shot) -> ecs::entity;

    [[nodiscard]] auto hits() const -> std::span<const projectile_hit> {
        return hits_;
    }

    [[nodiscard]] auto get_launched_count() const -> uint32 {
        return launched_count_;
    }

    [[nodiscard]] auto get_entity_hit_count() const -> uint32 {
        return entity_hit_count_;
    }

    [[nodiscard]] auto get_stuck_count() const -> std::size_t {
        return stuck_.size();
    }

    [[nodiscard]] auto get_flying_count() const -> uint32 {
        return flying_count_;
    }

    [[nodiscard]] auto tuning() -> projectile_tuning& {
        return tuning_;
    }

    [[nodiscard]] auto first_hit(const vec3f& from, const vec3f& to, ecs::entity ignored_owner)
        -> std::optional<projectile_hit>;

private:
    [[nodiscard]] auto root_of_(ecs::entity ent) const -> ecs::entity;
    auto fly_(ecs::entity ent, projectile_component& shot, float32 delta_time) -> void;
    auto hold_(ecs::entity ent, projectile_component& shot, float32 delta_time) -> void;
    auto stick_(ecs::entity ent, projectile_component& shot, const projectile_hit& hit) -> void;
    auto drop_(ecs::entity ent) -> void;

    ecs::world* world_;
    projectile_tuning tuning_;
    std::vector<ecs::entity> candidates_;
    std::vector<ecs::entity> dropped_;
    std::vector<projectile_hit> hits_;
    std::deque<ecs::entity> stuck_;
    uint32 launched_count_   = 0;
    uint32 entity_hit_count_ = 0;
    uint32 flying_count_     = 0;
};

}  // namespace vw::game
