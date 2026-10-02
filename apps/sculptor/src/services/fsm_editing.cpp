module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor {

namespace {

constexpr std::string_view machine_extension = ".voxf";
constexpr std::string_view clip_extension    = ".voxa";
constexpr std::size_t problems_shown         = 6;

[[nodiscard]] auto refuse(std::string message) -> std::unexpected<std::string> {
    return std::unexpected(std::move(message));
}

template <typename Range>
[[nodiscard]] auto joined(const Range& names) -> std::string {
    std::string list;
    for (const auto& name : names) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    }
    return list.empty() ? std::string{"none"} : list;
}

[[nodiscard]] auto is_plain_name(std::string_view text) -> bool {
    return !text.empty() && std::ranges::all_of(text, [](char symbol) {
        const bool letter = (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z');
        const bool digit  = symbol >= '0' && symbol <= '9';
        return letter || digit || symbol == '_' || symbol == '-';
    });
}

[[nodiscard]] auto refs_as_text(const std::vector<asset::asset_ref>& refs)
    -> std::vector<std::string> {
    std::vector<std::string> texts;
    for (const asset::asset_ref& ref : refs) {
        texts.push_back(ref.str());
    }
    return texts;
}

}  // namespace

auto fsm_service::machine_ref(std::string_view name) -> asset::asset_ref {
    std::string_view stem = name;
    if (const auto slash = stem.find_last_of("/\\"); slash != std::string_view::npos) {
        stem.remove_prefix(slash + 1);
    }
    if (stem.ends_with(machine_extension)) {
        stem.remove_suffix(machine_extension.size());
    }
    return asset::asset_ref{std::format("{}/{}{}", asset::dirs::fsm, stem, machine_extension)};
}

auto fsm_service::machine_files() const -> std::vector<std::string> {
    namespace fs = std::filesystem;

    std::vector<std::string> refs;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(app_state::fsm_dir(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == machine_extension) {
            refs.push_back(machine_ref(entry.path().filename().string()).str());
        }
    }
    std::ranges::sort(refs);
    return refs;
}

auto fsm_service::layer_of(const asset::asset_ref& machine) const -> std::optional<std::size_t> {
    const auto attached = machines();
    const auto found    = std::ranges::find(attached, machine);
    if (found == attached.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(attached.begin(), found));
}

auto fsm_service::root_() const -> std::expected<ecs::entity, std::string> {
    if (state_->file.filename.empty()) {
        return refuse("no prefab is open; a state machine is edited on an open prefab");
    }

    const auto root = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root == state_->scene.name_to_entity.end()) {
        return refuse("the prefab has no nodes yet; its state machines are kept on the root node");
    }
    if (state_->paste.active() || state_->ctx.in_paste()) {
        return refuse("a paste is being placed; confirm or cancel it in the editor");
    }
    return root->second;
}

auto fsm_service::rig_of_prefab_() const -> std::string {
    const auto root = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root == state_->scene.name_to_entity.end()) {
        return {};
    }

    auto& world = engine_->get_world();
    return world.has<ecs::rig_component>(root->second)
               ? world.get<ecs::rig_component>(root->second).get_name()
               : std::string{};
}

auto fsm_service::read(const asset::asset_ref& machine) const
    -> std::expected<asset::voxf_data, std::string> {
    if (state_->fsm.is_open() && state_->fsm.source == machine) {
        return state_->fsm.data;
    }

    auto parsed = asset::voxf_deserializer{}.deserialize(library_->path_of(machine));
    if (!parsed.has_value()) {
        return refuse(std::format(
            "the state machine '{}' could not be read; the machine files are: {}", machine.str(),
            joined(machine_files())
        ));
    }
    return std::move(*parsed);
}

auto fsm_service::replace(const asset::asset_ref& machine, asset::voxf_data data) -> outcome {
    if (const auto root = root_(); !root) {
        return refuse(root.error());
    }

    const auto layer = layer_of(machine);
    if (!layer) {
        return refuse(std::format(
            "'{}' is not a state machine of this prefab; its machines are: {}; attach it with "
            "prefab_set_machines",
            machine.str(), joined(refs_as_text(machines()))
        ));
    }

    const auto& open = state_->fsm;
    if (open.is_open() && open.source != machine && open.has_unsaved_changes) {
        return refuse(std::format(
            "the state machine '{}' has unsaved changes; save it with fsm_save before editing "
            "another",
            open.source.str()
        ));
    }

    const auto problems = asset::find_problems(data);
    if (!problems.empty()) {
        std::string text = std::format("the state machine has {} problem(s): ", problems.size());
        for (std::size_t index = 0; index < std::min(problems.size(), problems_shown); ++index) {
            text += std::format("{}{}", index == 0 ? "" : "; ", problems[index]);
        }
        return refuse(std::move(text));
    }

    for (const asset::voxf_state& state : data.states) {
        if (state.clip.empty()) {
            continue;
        }

        std::error_code ec;
        const bool is_clip = state.clip.extension() == clip_extension &&
            std::filesystem::is_regular_file(library_->path_of(state.clip), ec);
        if (!is_clip) {
            return refuse(std::format(
                "state '{}': there is no clip file '{}'; assets_list names the clips there are",
                state.name, state.clip.str()
            ));
        }
    }

    const std::string prefab_rig = rig_of_prefab_();
    if (!data.rig.empty() && !prefab_rig.empty() && data.rig != prefab_rig) {
        return refuse(std::format(
            "the state machine is for the rig '{}' and the prefab has the rig '{}'", data.rig,
            prefab_rig
        ));
    }

    leave_edit_contexts(*state_, *clips_);
    if (!enter(*layer)) {
        return refuse(std::format("the state machine '{}' could not be opened", machine.str()));
    }

    if (state_->fsm.data == data) {
        return {};
    }

    op_manager_->execute(std::make_unique<set_fsm_operation>(
        *state_,
        set_fsm_params{.before = state_->fsm.data, .after = std::move(data), .machine = machine}
    ));
    return {};
}

auto fsm_service::save_open() -> outcome {
    if (!state_->fsm.is_open()) {
        return refuse("no state machine is open; fsm_set opens the one it edits");
    }
    if (!save()) {
        return refuse(std::format(
            "the state machine '{}' could not be written", state_->fsm.source.str()
        ));
    }
    return {};
}

auto fsm_service::create_machine(std::string_view name, bool attach)
    -> std::expected<asset::asset_ref, std::string> {
    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const asset::asset_ref machine = machine_ref(name);
    const std::string stem{machine.stem()};
    if (!is_plain_name(stem)) {
        return refuse(std::format(
            "'{}' cannot name a state machine: use letters, digits, '_' and '-'", name
        ));
    }

    std::error_code ec;
    if (std::filesystem::exists(library_->path_of(machine), ec)) {
        return refuse(std::format(
            "a state machine file '{}' already exists; attach it with prefab_set_machines and "
            "edit it with fsm_set",
            machine.str()
        ));
    }

    asset::voxf_data data;
    data.rig         = rig_of_prefab_();
    data.entry_state = "idle";
    data.states.push_back(asset::voxf_state{.name = "idle"});

    if (!asset::voxf_serializer{data}.serialize(library_->path_of(machine)).has_value()) {
        return refuse(std::format("the state machine '{}' could not be written", machine.str()));
    }

    if (attach) {
        auto attached = machines();
        attached.push_back(machine);
        if (auto set = set_machines(std::move(attached)); !set) {
            return refuse(set.error());
        }
    }
    return machine;
}

auto fsm_service::set_machines(std::vector<asset::asset_ref> wanted) -> outcome {
    if (const auto root = root_(); !root) {
        return refuse(root.error());
    }

    for (std::size_t index = 0; index < wanted.size(); ++index) {
        const asset::asset_ref& machine = wanted[index];

        std::error_code ec;
        const bool is_machine = machine.extension() == machine_extension &&
            std::filesystem::is_regular_file(library_->path_of(machine), ec);
        if (!is_machine) {
            return refuse(std::format(
                "machines[{}]: there is no state machine file '{}'; the machine files are: {}",
                index, machine.str(), joined(machine_files())
            ));
        }
        if (std::ranges::count(wanted, machine) > 1) {
            return refuse(std::format(
                "machines: '{}' is given twice; a machine drives one layer", machine.str()
            ));
        }
    }

    if (wanted == machines()) {
        return {};
    }

    const auto& open = state_->fsm;
    if (open.is_open() && open.has_unsaved_changes && !std::ranges::contains(wanted, open.source)) {
        return refuse(std::format(
            "the state machine '{}' has unsaved changes and would be detached; save it with "
            "fsm_save first",
            open.source.str()
        ));
    }

    leave_edit_contexts(*state_, *clips_);

    op_manager_->execute(std::make_unique<set_machines_operation>(
        *engine_, *state_, set_machines_params{.machines = std::move(wanted)}
    ));
    return {};
}

auto fsm_service::run() -> outcome {
    if (const auto root = root_(); !root) {
        return refuse(root.error());
    }

    const std::vector<asset::asset_ref> attached = machines();
    if (attached.empty()) {
        return refuse(
            "the prefab runs no state machines; attach one with prefab_set_machines or fsm_create"
        );
    }

    std::vector<machine_to_run> built;
    std::vector<asset::voxf_param> declared;

    for (const asset::asset_ref& machine : attached) {
        auto data = read(machine);
        if (!data) {
            return refuse(data.error());
        }
        if (const auto problems = asset::find_problems(*data); !problems.empty()) {
            return refuse(std::format(
                "the state machine '{}' cannot run: {}", machine.stem(), problems.front()
            ));
        }

        for (const asset::voxf_state& state : data->states) {
            if (!state.clip.empty() && clips_->clip_for_machine(state.clip) == nullptr) {
                return refuse(std::format(
                    "the state '{}' of '{}' plays '{}', which does not load", state.name,
                    machine.stem(), state.clip.str()
                ));
            }
        }

        for (const asset::voxf_param& param : data->params) {
            if (!std::ranges::contains(declared, param.name, &asset::voxf_param::name)) {
                declared.push_back(param);
            }
        }

        asset::animation_fsm runnable = asset::build_fsm(
            *data, [this](const asset::asset_ref& clip) { return clips_->clip_for_machine(clip); }
        );
        built.push_back(machine_to_run{.machine = std::move(runnable), .data = std::move(*data)});
    }

    if (!state_->ctx.in_fsm()) {
        const std::size_t layer =
            state_->fsm.is_open() ? layer_of(state_->fsm.source).value_or(0) : 0;

        leave_edit_contexts(*state_, *clips_);
        if (!enter(layer)) {
            return refuse(std::format(
                "the state machine '{}' could not be opened", attached[layer].stem()
            ));
        }
    }

    if (auto started = clips_->run_machines(std::move(built)); !started) {
        return started;
    }

    running_refs_   = attached;
    running_params_ = std::move(declared);
    return {};
}

auto fsm_service::stop() -> void {
    clips_->stop_machines();
}

auto fsm_service::drive(const machine_input& input) -> outcome {
    if (!state_->anim.machines_running) {
        return refuse("no state machine is running; start them with fsm_run");
    }
    const auto root = root_();
    if (!root) {
        return refuse(root.error());
    }

    const auto declared = [this](std::string_view name) -> const asset::voxf_param* {
        const auto found = std::ranges::find(running_params_, name, &asset::voxf_param::name);
        return found == running_params_.end() ? nullptr : &*found;
    };
    const auto listed = [this](bool triggers) {
        std::vector<std::string> names;
        for (const asset::voxf_param& param : running_params_) {
            if ((param.type == asset::voxf_param_type::trigger) == triggers) {
                names.push_back(param.name);
            }
        }
        return joined(names);
    };

    for (const auto& [name, value] : input.values) {
        const asset::voxf_param* param = declared(name);
        if (param == nullptr || param->type == asset::voxf_param_type::trigger) {
            return refuse(std::format(
                "set: '{}' is not a value parameter of the running machines; they are: {}", name,
                listed(false)
            ));
        }
        if (param->type == asset::voxf_param_type::boolean && value != 0.0F && value != 1.0F) {
            return refuse(std::format("set: '{}' is a bool, give true or false", name));
        }
        if (param->type == asset::voxf_param_type::integer && value != std::round(value)) {
            return refuse(std::format("set: '{}' is an int, got {}", name, value));
        }
    }
    for (const std::string& name : input.triggers) {
        const asset::voxf_param* param = declared(name);
        if (param == nullptr || param->type != asset::voxf_param_type::trigger) {
            return refuse(std::format(
                "fire: '{}' is not a trigger of the running machines; they are: {}", name,
                listed(true)
            ));
        }
    }

    const auto runner = engine_->get_world().system<ecs::animation_fsm_system>().modify(*root);
    for (const auto& [name, value] : input.values) {
        runner.set_parameter(name, value);
    }
    for (const std::string& name : input.triggers) {
        runner.fire_trigger(name);
    }
    return {};
}

auto fsm_service::run_status() const -> machine_run_status {
    machine_run_status status{
        .running    = state_->anim.machines_running,
        .layers     = {},
        .parameters = {},
        .triggers   = {},
    };
    if (!status.running) {
        return status;
    }

    const auto root = state_->scene.name_to_entity.find(state_->scene.root_name);
    if (root == state_->scene.name_to_entity.end()) {
        return status;
    }

    auto& world = engine_->get_world();
    if (!world.has<ecs::animation_fsm_component>(root->second) ||
        !world.has<ecs::animation_player_component>(root->second)) {
        return status;
    }

    const auto& running = world.get<ecs::animation_fsm_component>(root->second);
    const auto& player  = world.get<ecs::animation_player_component>(root->second);

    for (std::size_t layer = 0; layer < running.machine_count(); ++layer) {
        machine_layer_status described{
            .machine  = layer < running_refs_.size() ? running_refs_[layer] : asset::asset_ref{},
            .state    = running.get_machine(layer).get_current_state(),
            .clip     = {},
            .playback = asset::animation_state::stopped,
            .time     = 0.0F,
        };
        if (player.has_layer(layer)) {
            const asset::animation_layer& played = player.get_layer(layer);
            described.clip     = played.clip ? played.clip->get_name() : std::string{};
            described.playback = played.state;
            described.time     = played.time;
        }
        status.layers.push_back(std::move(described));
    }

    for (const asset::voxf_param& param : running_params_) {
        if (param.type == asset::voxf_param_type::trigger) {
            status.triggers.push_back(param.name);
            continue;
        }
        status.parameters.push_back(machine_parameter{
            .name = param.name, .type = param.type, .value = running.get_board().get(param.name)
        });
    }
    return status;
}

}  // namespace vw::sculptor
