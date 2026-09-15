module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

select_variant_operation::select_variant_operation(
    engine_type& eng, app_state& st, asset::model_library& library,
    const select_variant_params& params
)
    : engine_(&eng), state_(&st), library_(&library), params_(params) {}

auto select_variant_operation::execute() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(ent)) {
        return;
    }

    previous_ = world.get<ecs::variant_slot_component>(ent).get_selected();

    // Через разборщик, а не через систему: кандидатом бывает и поддерево, и
    // читать его — работа того, у кого есть разборщик. Объём он передаст системе
    // сам.
    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};

    // Отказ не молчит: вариант, который не встал, обязан быть виден — иначе
    // список в инспекторе покажет одно, а в сцене останется другое.
    const auto result = deserializer.put_variant(ent, params_.index);
    if (!result.has_value()) {
        log::warn(
            "failed to put candidate {} into slot '{}'", params_.index,
            world.get<ecs::variant_slot_component>(ent).get_name()
        );
        return;
    }

    state_->file.has_unsaved_changes = true;
}

auto select_variant_operation::undo() -> void {
    const auto ent = state_->scene.name_to_entity[params_.name];

    auto& world = engine_->get_world();
    if (!world.has<ecs::variant_slot_component>(ent)) {
        return;
    }

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{world, parser, *library_};
    static_cast<void>(deserializer.put_variant(ent, previous_));

    state_->file.has_unsaved_changes = true;
}

}  // namespace vw::sculptor
