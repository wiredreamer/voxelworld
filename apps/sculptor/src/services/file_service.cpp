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

// Ссылка отсчитывается от корня ассетов, а рабочий каталог редактора к нему
// отношения не имеет: на диск её переводит model_library.
auto make_prefab_ref(std::string_view filename) -> asset::asset_ref {
    return asset::asset_ref{std::format("{}/{}", asset::dirs::prefabs, filename)};
}
}  // namespace

file_service::file_service(
    engine_type& eng, app_state& state, asset::model_library& library
)
    : engine_(&eng), state_(&state), library_(&library) {}

auto file_service::save() -> bool {
    // Имя нужно раньше записи: по нему называются и префаб, и объёмы узлов.
    // Безымянный документ сохраняется только через «Save As».
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

    if (!write_(make_prefab_ref(filename))) {
        return false;
    }

    state_->file.filename = filename;
    return true;
}

auto file_service::write_(
    const asset::asset_ref& prefab_ref
) -> bool {
    if (state_->scene.root_name.empty() ||
        !state_->scene.name_to_entity.contains(state_->scene.root_name)) {
        return false;
    }

    assign_missing_refs_(prefab_ref);
    write_dirty_models_();

    asset::vox_writer_plain writer;
    ecs::vox_serializer serializer{
        engine_->get_world(),
        writer,
        state_->scene.name_to_entity.at(state_->scene.root_name),
        {.entity_names = state_->scene.entity_to_name, .kind = state_->file.kind}
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

        // Файла у такого объёма ещё нет, поэтому он грязный по определению —
        // иначе первая запись префаба сошлётся в пустоту.
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

        // Записанный объём и есть тот, что теперь лежит по ссылке. Без этого
        // кеш библиотеки останется с прежним: расширение модели заводит новый
        // объём, а ссылка у узла та же.
        library_->adopt(ref, model_comp.get_model());
    }

    state_->file.dirty_models.clear();
}

}  // namespace vw::sculptor
