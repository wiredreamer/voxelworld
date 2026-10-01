module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

auto replace_volume(
    gfx::engine& engine, std::shared_ptr<asset::model> current, std::shared_ptr<asset::model> next
) -> void {
    if (!current) {
        return;
    }

    auto& world = engine.get_world();

    std::vector<ecs::entity> holders;
    world.registry().for_each<ecs::model_component>(
        [&holders, &current](ecs::entity ent, const ecs::model_component& model_comp) -> void {
            if (model_comp.get_model() == current) {
                holders.push_back(ent);
            }
        }
    );

    auto& model_sys = world.system<ecs::model_system>();
    for (const auto ent : holders) {
        model_sys.modify(ent).set_model(next);
    }
}

}  // namespace vw::sculptor
