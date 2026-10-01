module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

namespace {
constexpr log::log_category lc_file{"file_service"};

auto make_prefab_ref(std::string_view filename) -> asset::asset_ref {
    return asset::asset_ref{std::format("{}/{}", asset::dirs::prefabs, filename)};
}
}  // namespace

file_service::file_service(
    engine_type& eng, app_state& state, asset::model_library& library,
    operation_manager& op_manager
)
    : engine_(&eng), state_(&state), library_(&library), op_manager_(&op_manager) {}

auto file_service::save() -> bool {
    if (state_->file.filename.empty()) {
        return false;
    }

    return write_(make_prefab_ref(state_->file.filename));
}

auto file_service::save_as(
    std::string_view filename
) -> bool {
    if (filename.empty()) {
        return false;
    }

    const auto prefab_ref = make_prefab_ref(filename);
    const auto moves = plan_model_moves_(make_prefab_ref(state_->file.filename), prefab_ref);

    copy_detached_models_(moves);
    retarget_models_(moves);

    if (!write_(prefab_ref)) {
        model_moves reverted;
        for (const auto& [from, to] : moves) {
            reverted.emplace(to, from);
        }
        retarget_models_(reverted);
        return false;
    }

    for (const auto& from : moves | std::views::keys) {
        library_->forget(from);
    }

    op_manager_->clear();

    state_->file.filename = filename;
    return true;
}

auto file_service::close() -> void {
    auto& world = engine_->get_world();

    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (world.has<ecs::model_component>(ent)) {
            library_->forget(world.get<ecs::model_component>(ent).get_source());
        }

        if (world.has<ecs::variant_slot_component>(ent)) {
            for (const auto& ref : world.get<ecs::variant_slot_component>(ent).get_candidates()) {
                library_->forget(ref);
            }
        }
    }

    state_->reset(world);
    op_manager_->clear();
}

auto file_service::rename_model(
    const asset::asset_ref& ref, std::string_view stem, bool overwrite
) -> std::expected<void, rename_model_error> {
    namespace fs = std::filesystem;

    if (state_->file.filename.empty()) {
        return std::unexpected(rename_model_error::write_failed);
    }

    const auto renamed = asset::renamed_model_ref(ref, stem);
    if (!renamed.has_value()) {
        return std::unexpected(rename_model_error::invalid_name);
    }
    if (*renamed == ref) {
        return {};
    }
    if (is_model_referenced_(*renamed)) {
        return std::unexpected(rename_model_error::name_in_use);
    }

    const auto old_path = library_->path_of(ref);
    const auto new_path = library_->path_of(*renamed);

    std::error_code ec;
    const bool target_existed = fs::exists(new_path, ec);
    const bool is_same_file   = target_existed && fs::equivalent(old_path, new_path, ec);

    if (target_existed && !is_same_file && !overwrite) {
        return std::unexpected(rename_model_error::file_exists);
    }

    if (!write_model_copy_(ref, *renamed)) {
        return std::unexpected(rename_model_error::write_failed);
    }

    library_->forget(*renamed);
    retarget_models_(model_moves{{ref, *renamed}});

    if (!write_(make_prefab_ref(state_->file.filename))) {
        retarget_models_(model_moves{{*renamed, ref}});
        library_->forget(*renamed);

        if (!target_existed) {
            fs::remove(new_path, ec);
        }
        return std::unexpected(rename_model_error::write_failed);
    }

    library_->forget(ref);

    if (is_same_file) {
        fs::rename(old_path, new_path, ec);
    } else {
        fs::remove(old_path, ec);
    }
    if (ec) {
        log::warn(lc_file, "failed to remove model '{}': {}", ref.str(), ec.message());
    }

    op_manager_->clear();
    return {};
}

auto file_service::is_model_referenced_(
    const asset::asset_ref& ref
) const -> bool {
    auto& world = engine_->get_world();

    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (world.has<ecs::model_component>(ent) &&
            world.get<ecs::model_component>(ent).get_source() == ref) {
            return true;
        }

        if (world.has<ecs::variant_slot_component>(ent) &&
            std::ranges::contains(
                world.get<ecs::variant_slot_component>(ent).get_candidates(), ref
            )) {
            return true;
        }
    }

    return false;
}

auto file_service::write_model_copy_(
    const asset::asset_ref& from, const asset::asset_ref& to
) -> bool {
    auto& world = engine_->get_world();

    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (!world.has<ecs::model_component>(ent)) {
            continue;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        if (model_comp.get_source() == from && model_comp.has_model()) {
            return library_->save(to, *model_comp.get_model()).has_value();
        }
    }

    const auto volume = library_->load(from);
    return volume.has_value() && library_->save(to, **volume).has_value();
}

auto file_service::plan_model_moves_(
    const asset::asset_ref& from_prefab, const asset::asset_ref& to_prefab
) const -> model_moves {
    auto& world = engine_->get_world();

    model_moves moves;
    const auto plan = [&](const asset::asset_ref& ref) {
        const auto rehomed = asset::rehomed_model_ref(ref, from_prefab, to_prefab);
        if (rehomed.has_value() && *rehomed != ref) {
            moves.emplace(ref, *rehomed);
        }
    };

    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (world.has<ecs::model_component>(ent)) {
            plan(world.get<ecs::model_component>(ent).get_source());
        }

        if (world.has<ecs::variant_slot_component>(ent)) {
            for (const auto& ref : world.get<ecs::variant_slot_component>(ent).get_candidates()) {
                plan(ref);
            }
        }
    }

    return moves;
}

auto file_service::copy_detached_models_(
    const model_moves& moves
) -> void {
    auto& world = engine_->get_world();

    std::unordered_set<asset::asset_ref> attached;
    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (!world.has<ecs::model_component>(ent)) {
            continue;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        if (model_comp.has_model()) {
            attached.insert(model_comp.get_source());
        }
    }

    for (const auto& [from, to] : moves) {
        if (attached.contains(from)) {
            continue;
        }

        const auto volume = library_->load(from);
        if (!volume.has_value() || !library_->save(to, **volume)) {
            log::warn(lc_file, "failed to copy model '{}' to '{}'", from.str(), to.str());
        }
    }
}

auto file_service::retarget_models_(
    const model_moves& moves
) -> void {
    if (moves.empty()) {
        return;
    }

    auto& world     = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();
    auto& variants  = world.system<ecs::variant_system>();

    for (const auto ent : state_->scene.name_to_entity | std::views::values) {
        if (world.has<ecs::model_component>(ent)) {
            const auto& model_comp = world.get<ecs::model_component>(ent);

            if (const auto it = moves.find(model_comp.get_source()); it != moves.end()) {
                model_sys.modify(ent).set_source(it->second);

                if (model_comp.has_model()) {
                    library_->adopt(it->second, model_comp.get_model());
                    state_->file.dirty_models.insert(ent);
                }
            }
        }

        if (world.has<ecs::variant_slot_component>(ent)) {
            auto candidates = world.get<ecs::variant_slot_component>(ent).get_candidates();

            bool retargeted = false;
            for (auto& ref : candidates) {
                if (const auto it = moves.find(ref); it != moves.end()) {
                    ref        = it->second;
                    retargeted = true;
                }
            }

            if (retargeted) {
                static_cast<void>(variants.modify(ent).set_candidates(std::move(candidates)));
            }
        }
    }
}

auto file_service::collect_dirty_models() -> void {
    auto& world = engine_->get_world();

    for (const auto ent : world.changed<ecs::model_component>()) {
        state_->file.dirty_models.insert(ent);
    }
    for (const auto ent : world.registry().requested<ecs::model_component>()) {
        state_->file.dirty_models.insert(ent);
    }
}

auto file_service::write_(
    const asset::asset_ref& prefab_ref
) -> bool {
    if (state_->scene.root_name.empty() ||
        !state_->scene.name_to_entity.contains(state_->scene.root_name)) {
        return false;
    }

    collect_dirty_models();
    assign_missing_refs_(prefab_ref);
    write_dirty_models_();

    asset::vox_writer_plain writer;
    ecs::vox_serializer serializer{
        engine_->get_world(),
        writer,
        state_->scene.name_to_entity.at(state_->scene.root_name),
        {.entity_names = state_->scene.entity_to_name}
    };

    if (!serializer.serialize(library_->path_of(prefab_ref))) {
        return false;
    }

    state_->file.has_unsaved_changes = false;
    return true;
}

auto file_service::assign_missing_refs_(
    const asset::asset_ref& prefab_ref
) -> void {
    auto& world     = engine_->get_world();
    auto& model_sys = world.system<ecs::model_system>();

    for (const auto& [name, ent] : state_->scene.name_to_entity) {
        if (!world.has<ecs::model_component>(ent)) {
            continue;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        if (!model_comp.get_source().empty() || !model_comp.has_model()) {
            continue;
        }

        const auto ref = asset::default_model_ref(prefab_ref, name);
        model_sys.modify(ent).set_source(ref);
        library_->adopt(ref, model_comp.get_model());

        state_->file.dirty_models.insert(ent);
    }
}

auto file_service::write_dirty_models_() -> void {
    auto& world = engine_->get_world();

    for (const auto ent : state_->file.dirty_models) {
        if (!world.has<ecs::model_component>(ent)) {
            continue;
        }

        const auto& model_comp = world.get<ecs::model_component>(ent);
        const auto& ref        = model_comp.get_source();
        if (ref.empty() || !model_comp.has_model()) {
            continue;
        }

        if (!library_->save(ref, *model_comp.get_model())) {
            log::warn(lc_file, "failed to write model '{}'", ref.str());
            continue;
        }

        library_->adopt(ref, model_comp.get_model());
    }

    state_->file.dirty_models.clear();
}

}  // namespace vw::sculptor
