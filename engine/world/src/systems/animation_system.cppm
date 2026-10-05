export module vw.world:systems.animation;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;
import :grid;
import :spatial;
import :light;
import :terrain;

export namespace vw::ecs {

class world;

struct rig_report {
    std::string rig;
    std::string clip_rig;

    std::vector<std::string> unknown_targets;

    [[nodiscard]] auto rig_matches() const -> bool {
        return rig.empty() || clip_rig.empty() || rig == clip_rig;
    }

    [[nodiscard]] auto ok() const -> bool {
        return rig_matches() && unknown_targets.empty();
    }
};

class animation_system final {
public:
    static constexpr std::string_view system_name = "animation";

    explicit animation_system(world& w);

    auto update(float32 delta_time) -> void;

    [[nodiscard]] auto get_target_fps() const -> float32;
    auto set_target_fps(float32 fps) -> void;

    class layer_modifier {
    public:
        auto play() const -> void;
        auto play(const asset::transition& fade_in) const -> void;
        auto pause() const -> void;
        auto stop() const -> void;
        auto stop(const asset::transition& fade_out) const -> void;
        auto clear() const -> void;
        auto resume() const -> void;
        auto set_time(float32 time) const -> void;
        auto set_playback_speed(float32 speed) const -> void;
        auto set_loop_mode(asset::animation_loop_mode mode) const -> void;
        auto set_fade_in(const asset::transition& t) const -> void;
        auto set_fade_out(const asset::transition& t) const -> void;
        auto blend_to(std::shared_ptr<asset::animation_clip> clip,
                      std::optional<asset::transition> t = std::nullopt) const -> void;
        auto blend_to_by_name(std::string_view name,
                              std::optional<asset::transition> t = std::nullopt) -> void;

    private:
        friend class animation_system;
        layer_modifier(animation_system* system, entity ent, asset::animation_layer* layer);

        animation_system* system_;
        entity entity_;
        asset::animation_layer* layer_;
    };

    class player_modifier {
    public:
        auto add_layer(std::size_t index) const -> void;
        auto layer(std::size_t index) -> layer_modifier;
        auto apply_pose() const -> void;
        auto rebuild_target_map() const -> void;

    private:
        friend class animation_system;
        player_modifier(animation_system* system, entity ent,
                        animation_player_component* component);

        animation_system* system_;
        entity entity_;
        animation_player_component* component_;
    };

    class target_modifier {
    public:
        auto set_target_name(std::string name) const -> void;
        auto set_rest_transform(const transform& rest) const -> void;

    private:
        friend class animation_system;
        target_modifier(entity ent, animation_target_component* component);

        entity entity_;
        animation_target_component* component_;
    };

    class rig_modifier {
    public:
        auto set_name(std::string name) const -> void;

    private:
        friend class animation_system;
        rig_modifier(entity ent, rig_component* component);

        entity entity_;
        rig_component* component_;
    };

    class adjustment_modifier {
    public:
        auto set_rotation(const quat& rotation) const -> const adjustment_modifier&;
        auto set_translation(const vec3f& translation) const -> const adjustment_modifier&;

    private:
        friend class animation_system;
        explicit adjustment_modifier(pose_adjustment_component* component);

        pose_adjustment_component* component_;
    };

    auto modify_player(entity ent) -> player_modifier;
    auto modify_target(entity ent) -> target_modifier;
    auto modify_adjustment(entity ent) -> adjustment_modifier;
    auto modify_rig(entity ent) -> rig_modifier;

    [[nodiscard]] auto collect_targets(entity root_ent) const -> std::vector<std::string>;

    [[nodiscard]] auto check_clip(entity root_ent, const asset::animation_clip& clip) const
        -> rig_report;

private:
    auto add_active_entity(entity root_ent) -> void;
    auto remove_active_entity(entity root_ent) -> void;
    auto build_and_cache_target_map(entity root_ent) -> void;

    [[nodiscard]] auto collect_target_list_(entity root_ent) const
        -> std::vector<std::pair<std::string, entity>>;

    [[nodiscard]] auto collect_target_map_(entity root_ent) const
        -> std::unordered_map<std::string, entity>;

    auto warn_unknown_targets_(entity root_ent) -> void;

    [[nodiscard]] auto get_cached_target_map(entity root_ent) const
        -> const std::unordered_map<std::string, entity>*;

    auto process_animation(entity ent, animation_player_component& anim_comp, float32 delta_time) -> void;

    auto update_layer_time(asset::animation_layer& layer, float32 delta_time) -> void;

    auto process_layer(asset::animation_layer& layer, float32 delta_time, bool is_base) -> void;
    auto clear_fired_events_() -> void;
    auto collect_fired_events_(
        entity ent, animation_player_component& anim_comp, std::size_t layer_index
    ) -> void;
    auto apply_animation(entity root_ent, const animation_player_component& anim_comp) -> void;

    [[nodiscard]] auto compute_layer_transform(const asset::animation_layer& layer,
                                               const std::string& target_name,
                                               const transform& rest) const
        -> std::optional<transform>;

    [[nodiscard]] auto compute_track_transform(const asset::animation_layer& layer,
                                               const asset::animation_track& track,
                                               const std::string& target_name,
                                               const transform& rest) const
        -> std::optional<transform>;

    static auto merge_with_rest(const transform& anim, const asset::animation_track& track,
                                const transform& rest) -> transform;

    std::unordered_set<entity> active_entities_;
    std::unordered_map<entity, std::unordered_map<std::string, entity>> target_maps_;
    std::unordered_map<entity, std::unordered_set<std::string>> warned_targets_;
    std::vector<entity> to_remove_;
    std::vector<std::pair<entity, transform>> final_transforms_;
    std::vector<uint32> slot_by_entity_;
    std::vector<const asset::animation_event*> crossed_events_;
    std::vector<entity> entities_with_fired_events_;
    static constexpr uint32 max_steps_per_update_ = 8;

    float32 accumulated_delta_time_ = 0.0F;
    float32 target_frame_time_      = 1.0F / 120.0F;

    world* world_;
};

}  // namespace vw::ecs
