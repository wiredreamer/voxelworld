module vw.sculptor;

import std;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr log::log_category fsm_service_lc{"fsm_service"};

}  // namespace

fsm_service::fsm_service(
    engine_type& eng, app_state& state, asset::model_library& library
)
    : engine_(&eng), state_(&state), library_(&library) {}

auto fsm_service::machines() const -> std::vector<asset::asset_ref> {
    const auto it = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (it == state_->scene.name_to_entity.end()) {
        return {};
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::animation_machines_component>(it->second)) {
        return {};
    }

    const auto sources = world.get<ecs::animation_machines_component>(it->second).get_sources();
    return {sources.begin(), sources.end()};
}

auto fsm_service::enter(
    std::size_t layer
) -> bool {
    const auto sources = machines();
    if (layer >= sources.size()) {
        return false;
    }

    const bool same_open = state_->fsm.source == sources[layer] && state_->fsm.has_unsaved_changes;

    if (!same_open) {
        asset::voxf_deserializer deserializer;
        auto result = deserializer.deserialize(library_->path_of(sources[layer]));
        if (!result.has_value()) {
            log::warn(fsm_service_lc, "failed to open machine: {}", sources[layer].str());
            return false;
        }

        state_->fsm.source              = sources[layer];
        state_->fsm.data                = std::move(*result);
        state_->fsm.has_unsaved_changes = false;
        state_->fsm.selected_state      = state_->fsm.data.entry_state;
    }

    state_->ctx.leave_to(0);
    state_->ctx.enter(edit_context::fsm(layer));

    return true;
}

auto fsm_service::save() -> bool {
    if (!state_->fsm.is_open()) {
        return false;
    }

    asset::voxf_serializer serializer{state_->fsm.data};
    if (!serializer.serialize(library_->path_of(state_->fsm.source)).has_value()) {
        log::warn(fsm_service_lc, "failed to save machine: {}", state_->fsm.source.str());
        return false;
    }

    state_->fsm.has_unsaved_changes = false;
    return true;
}

auto fsm_service::leave() -> void {
    if (state_->ctx.in_fsm()) {
        state_->ctx.leave_to(state_->ctx.stack.size() - 1);
    }

    state_->fsm = fsm_document{};
}

auto fsm_service::create(
    std::string_view filename
) -> std::optional<asset::asset_ref> {
    auto name = std::string{filename};
    if (name.empty()) {
        return std::nullopt;
    }
    if (!name.ends_with(".voxf")) {
        name += ".voxf";
    }

    const asset::asset_ref ref{std::format("{}/{}", asset::dirs::fsm, name)};

    asset::voxf_data data;
    data.entry_state = "idle";
    data.states.push_back(asset::voxf_state{.name = "idle"});

    asset::voxf_serializer serializer{data};
    if (!serializer.serialize(library_->path_of(ref)).has_value()) {
        log::warn(fsm_service_lc, "failed to create machine: {}", ref.str());
        return std::nullopt;
    }

    return ref;
}

}  // namespace vw::sculptor
