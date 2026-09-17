export module vw.world:systems.structure;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;

export namespace vw::ecs {

class world;

class structure_system final {
public:
    static constexpr std::string_view system_name = "structure";

    explicit structure_system(world& w);

    auto update(float32) -> void {}

    class modifier {
    public:
        auto set_type(std::string_view type) const -> void;
        auto set_races(std::vector<std::string> races) const -> void;
        auto set_tier(uint8 tier) const -> void;
        auto set_size(structure_size size) const -> void;

    private:
        friend class structure_system;
        explicit modifier(structure_component* component);

        structure_component* component_;
    };

    auto modify(entity ent) -> modifier;

    auto set_furniture_category(entity ent, std::string_view category) -> void;
    auto set_connection_profile(entity ent, std::string_view profile) -> void;

private:
    world* world_;
};

}  // namespace vw::ecs
