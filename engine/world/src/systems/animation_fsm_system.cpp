module vw.world;

import std;
import vw.core;
import vw.asset;

namespace vw::ecs {

animation_fsm_system::animation_fsm_system(world& w)
    : world_(&w) {}

auto animation_fsm_system::update(float32) -> void {
    auto view =
        world_->registry()
            .view<animation_fsm_component, animation_player_component>();

    for (auto [ent, fsm_comp, player_comp] : view) {
        fill_builtins_(ent, fsm_comp.board_);
        for (const auto& [name, value] : fsm_comp.pinned_) {
            fsm_comp.board_.set(name, value);
        }

        auto& triggers = fsm_comp.triggers_;
        for (std::size_t i = 0; i < fsm_comp.machine_count(); ++i) {
            auto pm = world_->system<animation_system>().modify_player(ent);
            if (!player_comp.has_layer(i)) {
                pm.add_layer(i);
                pm.rebuild_target_map();
            }

            auto& machine     = fsm_comp.machines_[i];
            const auto& layer = player_comp.get_layer(i);

            auto lm = pm.layer(i);

            if (layer.state == asset::animation_state::stopped && !layer.clip) {
                const auto* state = machine.get_current_state_node();
                if (!state) {
                    continue;
                }

                if (state->clip) {
                    lm.blend_to(state->clip);
                    lm.set_loop_mode(state->loop_mode);
                    lm.set_playback_speed(state->playback_speed);
                    lm.set_fade_out(state->layer_blend_out);

                    lm.play(state->layer_blend_in);
                    continue;
                }
            }

            auto result = machine.evaluate(layer, triggers, fsm_comp.board_);
            if (!result) {
                continue;
            }

            if (result->clip) {
                lm.blend_to(result->clip, result->blend);
                lm.set_loop_mode(result->loop_mode);
                lm.set_playback_speed(result->playback_speed);
                lm.set_fade_out(result->layer_blend_out);

                lm.play(result->layer_blend_in);
            } else if (layer.state == asset::animation_state::playing && !layer.fade_is_out) {
                lm.stop(result->blend);
            }

            machine.apply_transition(*result);
        }
        triggers.clear();
    }
}

animation_fsm_system::modifier::modifier(
    animation_fsm_component* component
)
    : component_(component) {}

auto animation_fsm_system::modifier::add_machine(
    std::size_t index, asset::animation_fsm machine
) const -> void {
    if (index >= component_->machines_.size()) {
        component_->machines_.resize(index + 1);
    }
    component_->machines_[index] = std::move(machine);
}

auto animation_fsm_system::fill_builtins_(
    entity ent, asset::fsm_blackboard& board
) const -> void {
    auto& registry = world_->registry();

    if (registry.has<movement_intent_component>(ent)) {
        const auto& wish = registry.get<movement_intent_component>(ent).get_wish_velocity();
        board.set("speed", math::length(vec3f{wish.x, 0.0F, wish.z}));
    }

    if (registry.has<rigid_body_component>(ent)) {
        board.set(
            "grounded", registry.get<rigid_body_component>(ent).is_grounded() ? 1.0F : 0.0F
        );
    }
}

auto animation_fsm_system::modifier::fire_trigger(
    std::string_view name
) const -> void {
    component_->triggers_.emplace(name);
}

auto animation_fsm_system::modifier::set_parameter(
    std::string_view name, float32 value
) const -> void {
    component_->board_.set(name, value);
}

auto animation_fsm_system::modifier::declare_parameters(
    const asset::voxf_data& data
) const -> void {
    asset::apply_defaults(data, component_->board_);

    auto& declared = component_->declared_;
    for (const auto& param : data.params) {
        if (!std::ranges::contains(declared, param.name, &asset::voxf_param::name)) {
            declared.push_back(param);
        }
    }
}

auto animation_fsm_system::modifier::pin_parameter(
    std::string_view name, float32 value
) const -> void {
    component_->board_.set(name, value);

    auto& pinned    = component_->pinned_;
    const auto slot = std::ranges::find(pinned, name, &animation_fsm_component::pinned_parameter::first);
    if (slot != pinned.end()) {
        slot->second = value;
        return;
    }
    pinned.emplace_back(std::string{name}, value);
}

auto animation_fsm_system::modifier::unpin_parameter(
    std::string_view name
) const -> void {
    std::erase_if(component_->pinned_, [name](const auto& entry) { return entry.first == name; });
}

auto animation_fsm_system::modifier::unpin_all_parameters() const -> void {
    component_->pinned_.clear();
}

animation_fsm_system::sources_modifier::sources_modifier(
    animation_machines_component* component
)
    : component_(component) {}

auto animation_fsm_system::sources_modifier::set(
    std::vector<asset::asset_ref> refs
) const -> void {
    component_->sources_ = std::move(refs);
}

auto animation_fsm_system::sources_modifier::add(
    asset::asset_ref ref
) const -> void {
    component_->sources_.push_back(std::move(ref));
}

auto animation_fsm_system::sources_modifier::remove(
    std::size_t index
) const -> void {
    if (index < component_->sources_.size()) {
        component_->sources_.erase(component_->sources_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

auto animation_fsm_system::sources_modifier::move(
    std::size_t from, std::size_t to
) const -> void {
    auto& sources = component_->sources_;
    if (from >= sources.size() || to >= sources.size() || from == to) {
        return;
    }

    auto ref = std::move(sources[from]);
    sources.erase(sources.begin() + static_cast<std::ptrdiff_t>(from));
    sources.insert(sources.begin() + static_cast<std::ptrdiff_t>(to), std::move(ref));
}

auto animation_fsm_system::modify_machines(
    entity ent
) -> sources_modifier {
    auto& comp = world_->registry().get<animation_machines_component>(ent);
    return sources_modifier(&comp);
}

auto animation_fsm_system::modify(
    entity ent
) -> modifier {
    auto& comp = world_->registry().get<animation_fsm_component>(ent);
    return modifier(&comp);
}

}  // namespace vw::ecs
