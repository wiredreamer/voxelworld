export module vw.world:systems.model;

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

class model_system {
public:
    static constexpr std::string_view system_name = "model";

    explicit model_system(world& w);

    class model_modifier {
    public:
        explicit model_modifier(model_system& system, model_component* component, entity e);

        [[nodiscard]] auto get_model() const -> std::shared_ptr<asset::model>;

        auto set_model(std::shared_ptr<asset::model> model_ptr) -> void;
        auto set_model(std::shared_ptr<asset::model> model_ptr, const asset::asset_ref& source)
            -> void;
        auto set_pivot(const vec3f& pivot) -> void;
        auto set_source(const asset::asset_ref& source) -> void;

        // Ставит и чанк, и его модель разом: у сущности чанка они всегда пара.
        auto set_chunk(std::shared_ptr<asset::chunk_volume> volume) -> void;
        auto set_voxel(int32 x, int32 y, int32 z, const voxel& v) -> void;
        auto set_voxel(vec3i pos, const voxel& v) -> void;
        auto fill(const voxel& v) -> void;

    private:
        model_system* system_;
        model_component* component_;
        entity entity_;
    };

    auto modify(entity e) -> model_modifier;
    auto update(float32 dt) -> void;

    template <typename C>
        requires std::same_as<C, model_component>
    auto on_add(entity e) -> void;

private:
    world* world_;
};

}  // namespace vw::ecs
