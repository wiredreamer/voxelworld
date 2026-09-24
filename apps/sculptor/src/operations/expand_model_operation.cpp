module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

expand_model_operation::expand_model_operation(
    engine_type& eng, app_state& st, const expand_model_params& params
) : engine_(&eng), state_(&st), params_(params) {}

auto expand_model_operation::execute() -> void {
    auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();
    auto& model_sys = world.system<ecs::model_system>();

    const auto& model_comp = world.get<ecs::model_component>(ent);
    const auto model = model_comp.get_model();

    const auto size = model->size();
    const auto new_size = vec3i{
        size.x + std::abs(params_.dir.x),
        size.y + std::abs(params_.dir.y),
        size.z + std::abs(params_.dir.z)
    };
    const auto new_model = model_reg.create_unnamed(new_size);

    const auto zeroed_dir = vec3i{
        params_.dir.x < 0 ? 1 : 0,
        params_.dir.y < 0 ? 1 : 0,
        params_.dir.z < 0 ? 1 : 0
    };

    {
        asset::model_writer writer{*new_model};

        for (int x = 0; x < size.x; ++x) {
            for (int y = 0; y < size.y; ++y) {
                for (int z = 0; z < size.z; ++z) {
                    const auto v = model->get_voxel(x, y, z);
                    const auto new_p = vec3i{
                        x + zeroed_dir.x,
                        y + zeroed_dir.y,
                        z + zeroed_dir.z
                    };
                    writer.set(new_p, v);
                }
            }
        }
    }

    new_model->set_pivot(model->pivot() + vec3f{
        static_cast<float32>(zeroed_dir.x),
        static_cast<float32>(zeroed_dir.y),
        static_cast<float32>(zeroed_dir.z)
    });

    model_sys.modify(ent).set_model(new_model);
    state_->file.has_unsaved_changes = true;
}

auto expand_model_operation::undo() -> void {
    auto ent = state_->scene.name_to_entity[params_.name];

    auto& world        = engine_->get_world();
    auto& model_reg = world.resource<asset::model_registry>();
    auto& model_sys = world.system<ecs::model_system>();

    auto& model_comp = world.get<ecs::model_component>(ent);
    auto model = model_comp.get_model();

    auto size = model->size();
    auto new_size = vec3i{
        size.x - std::abs(params_.dir.x),
        size.y - std::abs(params_.dir.y),
        size.z - std::abs(params_.dir.z)
    };
    auto new_model = model_reg.create_unnamed(new_size);

    const vec3i beg = vec3i{
        params_.dir.x < 0 ? 1 : 0,
        params_.dir.y < 0 ? 1 : 0,
        params_.dir.z < 0 ? 1 : 0
    };
    const vec3i end = vec3i{
        params_.dir.x <= 0 ? size.x : size.x - 1,
        params_.dir.y <= 0 ? size.y : size.y - 1,
        params_.dir.z <= 0 ? size.z : size.z - 1
    };
    const vec3i zeroed_dir = vec3i{
        params_.dir.x < 0 ? 1 : 0,
        params_.dir.y < 0 ? 1 : 0,
        params_.dir.z < 0 ? 1 : 0
    };

    {
        asset::model_writer writer{*new_model};

        for (int x = beg.x; x < end.x; ++x) {
            for (int y = beg.y; y < end.y; ++y) {
                for (int z = beg.z; z < end.z; ++z) {
                    const auto v = model->get_voxel(x, y, z);
                    const auto new_p = vec3i{
                        x - zeroed_dir.x,
                        y - zeroed_dir.y,
                        z - zeroed_dir.z
                    };
                    writer.set(new_p, v);
                }
            }
        }
    }

    new_model->set_pivot(model->pivot() - vec3f{
        static_cast<float32>(zeroed_dir.x),
        static_cast<float32>(zeroed_dir.y),
        static_cast<float32>(zeroed_dir.z)
    });

    model_sys.modify(ent).set_model(new_model);
    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
