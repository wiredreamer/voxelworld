export module vw.world:systems.animation_fsm;

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

class animation_fsm_system final {
public:
    static constexpr std::string_view system_name = "anim_fsm";

    explicit animation_fsm_system(world& w);

    auto update(float32 dt) -> void;

    class modifier {
    public:
        auto add_machine(std::size_t index, asset::animation_fsm machine) const -> void;
        auto fire_trigger(std::string_view name) const -> void;

        auto set_parameter(std::string_view name, float32 value) const -> void;

        auto declare_parameters(const asset::voxf_data& data) const -> void;

    private:
        friend class animation_fsm_system;
        explicit modifier(animation_fsm_component* component);

        animation_fsm_component* component_;
    };

    auto modify(entity ent) -> modifier;

    class sources_modifier {
    public:
        auto set(std::vector<asset::asset_ref> refs) const -> void;
        auto add(asset::asset_ref ref) const -> void;
        auto remove(std::size_t index) const -> void;

        auto move(std::size_t from, std::size_t to) const -> void;

    private:
        friend class animation_fsm_system;
        explicit sources_modifier(animation_machines_component* component);

        animation_machines_component* component_;
    };

    auto modify_machines(entity ent) -> sources_modifier;

private:
    auto fill_builtins_(entity ent, asset::fsm_blackboard& board) const -> void;

    world* world_;
};

}  // namespace vw::ecs
