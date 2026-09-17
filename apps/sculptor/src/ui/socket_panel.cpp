module;

#include <imgui.h>

module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

namespace vw::sculptor {

socket_panel::socket_panel(
    engine_type& eng, app_state& st, operation_manager& op_manager,
    asset::model_library& library
)
    : engine_(&eng), state_(&st), op_manager_(&op_manager), library_(&library) {}

auto socket_panel::render(
    float /*delta_time*/
) -> void {
    if (state_->scene.selected_name.empty()) {
        return;
    }

    auto& world    = engine_->get_world();
    const auto ent = state_->scene.name_to_entity[state_->scene.selected_name];

    if (!world.has<ecs::socket_component>(ent)) {
        return;
    }

    bool still_open = true;
    begin_panel(*state_, panel_slot::right, "Sockets", &still_open);
    if (!still_open) {
        state_->ui.show_sockets = false;
    }

    render_add_socket_();

    const auto& socket_comp = world.get<ecs::socket_component>(ent);
    const auto& sockets     = socket_comp.get_sockets();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (sockets.empty()) {
        ImGui::TextDisabled("No sockets");
    } else {
        std::string socket_to_remove;
        for (const auto& sp : sockets) {
            const auto socket_id = std::format("##socket_{}", sp.name);
            ImGui::PushID(socket_id.c_str());
            render_socket_(sp, socket_to_remove);
            ImGui::PopID();
        }

        if (!socket_to_remove.empty()) {
            pending_remove_socket_ = socket_to_remove;
            ImGui::OpenPopup("Remove Socket?");
        }
    }

    constexpr ImGuiWindowFlags popup_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::BeginPopupModal("Remove Socket?", nullptr, popup_flags)) {
        ImGui::Text("Remove socket \"%s\"?", pending_remove_socket_.c_str());
        ImGui::Spacing();

        if (ImGui::Button("Remove")) {
            remove_socket_params params = {
                .entity_name = state_->scene.selected_name,
                .socket_name = pending_remove_socket_,
            };
            auto op = std::make_unique<remove_socket_operation>(*engine_, *state_, params);
            op_manager_->execute(std::move(op));
            const auto pkey = socket_state::socket_preview_key(
                state_->scene.selected_name, pending_remove_socket_
            );
            unload_preview_(pkey);
            pending_remove_socket_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_remove_socket_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Dummy({220.0f, 0.0f});

    render_add_socket_modal_();

    end_panel(*state_, panel_slot::right);

    render_preview_file_list_();
}

auto socket_panel::render_socket_(
    const ecs::socket_point& sp, std::string& socket_to_remove
) -> void {
    constexpr ImGuiTreeNodeFlags flags =  //
        ImGuiTreeNodeFlags_OpenOnArrow |  //
        ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (ImGui::TreeNodeEx(sp.name.c_str(), flags)) {
        vec3f position       = sp.position;
        vec3f rotation_euler = math::quat_to_euler(sp.rotation);
        vec3f rotation_deg   = {
            math::degrees(rotation_euler.x),
            math::degrees(rotation_euler.y),
            math::degrees(rotation_euler.z),
        };
        vec3f scale = sp.scale;

        bool changed = false;

        ImGui::PushItemWidth(80.0f);

        ImGui::Text("Pos");
        ImGui::SameLine(80.f);
        changed |= ImGui::DragFloat("##PX", &position.x, 0.1f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##PY", &position.y, 0.1f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##PZ", &position.z, 0.1f, 0, 0, "%.4f");

        ImGui::Text("Rot");
        ImGui::SameLine(80.f);
        changed |= ImGui::DragFloat("##RX", &rotation_deg.x, 0.5f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##RY", &rotation_deg.y, 0.5f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##RZ", &rotation_deg.z, 0.5f, 0, 0, "%.4f");

        ImGui::Text("Scale");
        ImGui::SameLine(80.f);
        changed |= ImGui::DragFloat("##SX", &scale.x, 0.01f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##SY", &scale.y, 0.01f, 0, 0, "%.4f");
        ImGui::SameLine();
        changed |= ImGui::DragFloat("##SZ", &scale.z, 0.01f, 0, 0, "%.4f");

        ImGui::PopItemWidth();

        if (changed) {
            const auto new_rotation = math::euler_to_quat(
                vec3f{
                    math::radians(rotation_deg.x),
                    math::radians(rotation_deg.y),
                    math::radians(rotation_deg.z),
                }
            );

            set_socket_transform_params params = {
                .entity_name = state_->scene.selected_name,
                .socket_name = sp.name,
                .position    = position,
                .rotation    = new_rotation,
                .scale       = scale,
            };
            auto op = std::make_unique<set_socket_transform_operation>(*engine_, *state_, params);
            op_manager_->execute(std::move(op));

            const auto pkey =
                socket_state::socket_preview_key(state_->scene.selected_name, sp.name);
            update_preview_transform_(pkey, position, new_rotation, scale);
        }

        const auto pkey = socket_state::socket_preview_key(state_->scene.selected_name, sp.name);
        const bool has_preview = state_->sockets.socket_previews.contains(pkey);
        if (has_preview) {
            const auto& preview = state_->sockets.socket_previews[pkey];
            ImGui::Text("Preview: %s", preview.filename.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Unload")) {
                unload_preview_(pkey);
            }
        } else {
            ImGui::TextDisabled("Preview: Empty");
            ImGui::SameLine();
            if (ImGui::SmallButton("Load")) {
                need_preview_modal_   = true;
                preview_modal_socket_ = sp.name;
            }
        }

        ImGui::Spacing();

        if (ImGui::SmallButton("Remove")) {
            socket_to_remove = sp.name;
        }

        ImGui::TreePop();
    }
}

auto socket_panel::render_add_socket_() -> void {
    if (ImGui::Button("Add")) {
        need_add_socket_modal_ = true;
        new_socket_name_.clear();
        add_socket_error_.clear();
    }
}

auto socket_panel::render_add_socket_modal_() -> void {
    if (need_add_socket_modal_) {
        ImGui::OpenPopup("Add Socket");
        need_add_socket_modal_ = false;
    }

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Add Socket", nullptr, flags)) {
        if (!add_socket_error_.empty()) {
            ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", add_socket_error_.c_str());
        }

        imgui_input_text_string("Name", new_socket_name_);

        if (ImGui::Button("Create")) {
            if (new_socket_name_.empty()) {
                add_socket_error_ = "Name cannot be empty.";
            } else {
                auto ent = state_->scene.name_to_entity[state_->scene.selected_name];
                const auto& sc =
                    engine_->get_world().get<ecs::socket_component>(ent);
                if (sc.find(new_socket_name_) != nullptr) {
                    add_socket_error_ = "A socket with this name already exists.";
                } else {
                    add_socket_params params = {
                        .entity_name = state_->scene.selected_name,
                        .socket_name = new_socket_name_,
                    };
                    auto op = std::make_unique<add_socket_operation>(*engine_, *state_, params);
                    op_manager_->execute(std::move(op));
                    new_socket_name_.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            new_socket_name_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto socket_panel::render_preview_file_list_() -> void {
    if (need_preview_modal_) {
        ImGui::OpenPopup("Load Preview");
        need_preview_modal_ = false;

        vox_filenames_.clear();

        namespace fs = std::filesystem;
        const fs::path prefab_dir = app_state::prefab_dir();
        if (fs::exists(prefab_dir)) {
            for (const auto& entry : fs::directory_iterator(prefab_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".vox") {
                    vox_filenames_.emplace_back(entry.path().filename().string());
                }
            }
        }
    }

    constexpr ImGuiWindowFlags dialog_flags =  //
        ImGuiWindowFlags_AlwaysAutoResize |    //
        ImGuiWindowFlags_NoMove;
    if (ImGui::BeginPopupModal("Load Preview", nullptr, dialog_flags)) {
        ImGui::Text("Select VOX file:");
        ImGui::Spacing();

        const float list_height = ImGui::GetTextLineHeightWithSpacing() * 7.5f;

        constexpr ImGuiChildFlags child_flags =       //
            ImGuiChildFlags_AlwaysUseWindowPadding |  //
            ImGuiChildFlags_Borders;

        if (ImGui::BeginChild("##preview_file_list", ImVec2(300.f, list_height), child_flags)) {
            for (const auto& f : vox_filenames_) {
                const bool is_selected = preview_selected_file_ == f;
                if (ImGui::Selectable(f.c_str(), is_selected)) {
                    preview_selected_file_ = f;
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();

        const bool is_preview_empty = preview_selected_file_.empty();
        if (is_preview_empty) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Load")) {
            load_preview_(preview_modal_socket_, preview_selected_file_);
            preview_selected_file_.clear();
            ImGui::CloseCurrentPopup();
        }
        if (is_preview_empty) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            preview_selected_file_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

auto socket_panel::load_preview_(
    const std::string& socket_name, const std::string& filename
) const -> void {
    const auto pkey = socket_state::socket_preview_key(state_->scene.selected_name, socket_name);
    unload_preview_(pkey);

    namespace fs = std::filesystem;

    asset::vox_parser_plain parser;
    ecs::vox_deserializer deserializer{engine_->get_world(), parser, *library_};
    const fs::path filepath = app_state::prefab_dir() / fs::path{filename};

    const ecs::vox_deserializer::options opts{
        .skip_tags = {"socket", "anim_target"},
    };

    auto result = deserializer.deserialize(filepath, opts);
    if (!result.has_value()) {
        return;
    }

    const auto parent_ent   = state_->scene.name_to_entity[state_->scene.selected_name];
    auto& world             = engine_->get_world();
    const auto& socket_comp = world.get<ecs::socket_component>(parent_ent);
    const auto* sp          = socket_comp.find(socket_name);
    if (!sp) {
        return;
    }

    // Превью — содержимое по ссылке, как и кандидат слота: в запись оно не
    // идёт, и решает это метка на сущностях, а не список исключений у писателя.
    auto& variants = world.system<ecs::variant_system>();
    for (const auto ent : result->entities) {
        variants.mark_content(ent, parent_ent);
    }

    socket_state::socket_preview preview;
    preview.filename          = filename;
    preview.preview_root_name = result->root_name;
    preview.entities          = std::move(result->entities);

    if (result->name_to_entity.contains(result->root_name)) {
        const auto preview_root = result->name_to_entity[result->root_name];
        auto& transform_sys = world.system<ecs::transform_system>();
        transform_sys.modify(preview_root)
            .set_position(sp->position)
            .set_rotation(sp->rotation)
            .set_scale(sp->scale);

        auto& hierarchy_sys = world.system<ecs::hierarchy_system>();
        hierarchy_sys.modify(preview_root).set_parent(parent_ent);
    }

    state_->sockets.socket_previews[pkey] = std::move(preview);
}

auto socket_panel::unload_preview_(
    const std::string& key
) const -> void {
    state_->sockets.erase_preview(key, engine_->get_world());
}

auto socket_panel::update_preview_transform_(
    const std::string& key, const vec3f& position, const quat& rotation, const vec3f& scale
) const -> void {
    const auto it = state_->sockets.socket_previews.find(key);
    if (it == state_->sockets.socket_previews.end()) {
        return;
    }

    const auto& preview = it->second;
    if (preview.entities.empty()) {
        return;
    }

    const auto preview_root = preview.entities[0];
    auto& transform_sys = engine_->get_world().system<ecs::transform_system>();
    transform_sys.modify(preview_root)
        .set_position(position)
        .set_rotation(rotation)
        .set_scale(scale);
}

}  // namespace vw::sculptor
