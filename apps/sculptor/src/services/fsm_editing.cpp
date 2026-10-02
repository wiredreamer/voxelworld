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

}  // namespace vw::sculptor
