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
namespace {

constexpr float32 label_column = 70.f;
constexpr float32 field_width  = 220.f;

auto row_label(std::string_view label) -> void {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(label_column);
}

}  // namespace

create_entity_modal::create_entity_modal(
    engine_type& eng, app_state& state, operation_manager& op_manager,
    asset::model_library& library
)
    : engine_(&eng), state_(&state), op_manager_(&op_manager), library_(&library) {}

auto create_entity_modal::open() -> void {
    need_open_ = true;
    error_.clear();

    const auto& scene = state_->scene;
    name_             = std::format("new entity {}", scene.name_to_entity.size());
    parent_name_ =
        scene.name_to_entity.contains(scene.selected_name) ? scene.selected_name : scene.root_name;
    category_ = selected_model_category(*engine_, *state_);

    model_files_ = collect_asset_refs(app_state::model_dir(), ".voxm");
    if (!std::ranges::contains(model_files_, model_file_)) {
        model_file_ = {};
        model_file_info_.clear();
    }
}

auto create_entity_modal::render(
    float
) -> void {
    if (need_open_) {
        ImGui::OpenPopup("Create Entity");
        need_open_ = false;
    }

    constexpr ImGuiWindowFlags flags =  //
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

    if (!ImGui::BeginPopupModal("Create Entity", nullptr, flags)) {
        return;
    }

    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "%s", error_.c_str());
        ImGui::Spacing();
    }

    ImGui::PushItemWidth(field_width);

    imgui_input_text_string("Name", name_, label_column);
    render_parent_();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    render_role_();

    switch (role_) {
        case entity_role::model: render_model_fields_(); break;
        case entity_role::point: render_point_fields_(); break;
        case entity_role::socket:
        case entity_role::empty: break;
    }

    ImGui::Spacing();
    render_extras_();

    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Create") && create_entity_()) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

auto create_entity_modal::render_parent_() -> void {
    row_label("Parent");

    const auto& scene = state_->scene;
    if (scene.root_name.empty()) {
        ImGui::TextDisabled("none, becomes the root");
        return;
    }

    if (!ImGui::BeginCombo("##parent", parent_name_.c_str())) {
        return;
    }

    const auto root = scene.name_to_entity.find(scene.root_name);
    if (root != scene.name_to_entity.end()) {
        render_parent_option_(root->second, 0);
    }

    ImGui::EndCombo();
}

auto create_entity_modal::render_parent_option_(
    ecs::entity ent, std::size_t depth
) -> void {
    const auto& scene = state_->scene;

    const auto found = scene.entity_to_name.find(ent);
    if (found == scene.entity_to_name.end()) {
        return;
    }

    const auto& name  = found->second;
    const auto label  = std::format("{}{}", std::string(depth * 2, ' '), name);
    const bool chosen = name == parent_name_;

    if (ImGui::Selectable(label.c_str(), chosen)) {
        parent_name_ = name;
    }
    if (chosen) {
        ImGui::SetItemDefaultFocus();
    }

    const auto& world = engine_->get_world();
    if (!world.has<ecs::hierarchy_component>(ent)) {
        return;
    }

    for (const auto child : world.get<ecs::hierarchy_component>(ent).get_children()) {
        render_parent_option_(child, depth + 1);
    }
}

auto create_entity_modal::render_role_() -> void {
    row_label("Role");

    const auto option = [this](const char* label, entity_role value) {
        if (ImGui::RadioButton(label, role_ == value)) {
            role_ = value;
        }
    };

    option("Model", entity_role::model);
    ImGui::SameLine();
    option("Socket", entity_role::socket);
    ImGui::SameLine();
    option("Point", entity_role::point);
    ImGui::SameLine();
    option("Empty", entity_role::empty);
}

auto create_entity_modal::render_model_fields_() -> void {
    row_label("Source");
    if (ImGui::RadioButton("New", source_ == volume_source::blank)) {
        source_ = volume_source::blank;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("File", source_ == volume_source::file)) {
        source_ = volume_source::file;
    }

    if (source_ == volume_source::blank) {
        imgui_voxel_set_combo("Voxels", engine_->get_voxel_registry(), category_, label_column);

        row_label("Size");
        std::array<int32, 3> size{size_.x, size_.y, size_.z};
        if (ImGui::InputInt3("##size", size.data())) {
            size_ = vec3i{size[0], size[1], size[2]};
        }
        return;
    }

    row_label("Volume");
    const auto& preview = model_file_.empty() ? std::string{"choose a file"} : model_file_.str();
    if (ImGui::BeginCombo("##volume", preview.c_str())) {
        if (model_files_.empty()) {
            ImGui::TextDisabled("no saved volumes");
        }
        for (const auto& ref : model_files_) {
            if (ImGui::Selectable(ref.str().c_str(), ref == model_file_)) {
                pick_model_file_(ref);
            }
        }
        ImGui::EndCombo();
    }

    if (!model_file_info_.empty()) {
        row_label("");
        ImGui::TextDisabled("%s", model_file_info_.c_str());
    }
}

auto create_entity_modal::pick_model_file_(
    const asset::asset_ref& ref
) -> void {
    model_file_ = ref;

    const auto loaded = library_->load(ref);
    if (!loaded.has_value()) {
        model_file_info_ = "failed to load";
        return;
    }

    const auto& volume   = **loaded;
    const auto size      = volume.size();
    const voxel_set* set = engine_->get_voxel_registry().set_of(volume.category());
    const std::string_view set_name = set != nullptr ? set->name : "unknown";

    model_file_info_ = std::format("{}x{}x{}, {}", size.x, size.y, size.z, set_name);
}

auto create_entity_modal::render_point_fields_() -> void {
    row_label("Kind");
    if (ImGui::RadioButton("Furniture", point_kind_ == point_kind::furniture)) {
        point_kind_ = point_kind::furniture;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Connection", point_kind_ == point_kind::connection)) {
        point_kind_ = point_kind::connection;
    }

    const auto label = point_kind_ == point_kind::furniture ? "Category" : "Profile";
    imgui_input_text_string(label, point_tag_, label_column);
}

auto create_entity_modal::render_extras_() -> void {
    std::string chosen;
    for (const auto& drawer : default_drawers().all()) {
        if (drawer.make_add && extras_.contains(drawer.tag) &&
            !covered_by_role_(drawer.component)) {
            chosen += chosen.empty() ? drawer.title : std::format(", {}", drawer.title);
        }
    }

    const auto header = chosen.empty() ? std::string{"More components"}
                                       : std::format("More components: {}", chosen);

    if (!ImGui::TreeNodeEx("##extras", ImGuiTreeNodeFlags_None, "%s", header.c_str())) {
        return;
    }

    for (const auto& drawer : default_drawers().all()) {
        if (!drawer.make_add || covered_by_role_(drawer.component)) {
            continue;
        }

        bool checked = extras_.contains(drawer.tag);
        if (ImGui::Checkbox(drawer.title.c_str(), &checked)) {
            if (checked) {
                extras_.insert(drawer.tag);
            } else {
                extras_.erase(drawer.tag);
            }
        }
    }

    ImGui::TreePop();
}

auto create_entity_modal::covered_by_role_(
    uint32 component
) const -> bool {
    switch (role_) {
        case entity_role::socket: return component == ecs::component_id_of<ecs::socket_component>();
        case entity_role::point:
            return component == ecs::component_id_of<ecs::furniture_point_component>() ||
                component == ecs::component_id_of<ecs::connection_point_component>();
        case entity_role::model:
        case entity_role::empty: break;
    }
    return false;
}

auto create_entity_modal::create_entity_() -> bool {
    error_.clear();

    const auto& scene = state_->scene;

    if (name_.empty()) {
        error_ = "Name cannot be empty.";
        return false;
    }
    if (scene.name_to_entity.contains(name_)) {
        error_ = "An entity with this name already exists.";
        return false;
    }

    const bool becomes_root = scene.root_name.empty();
    if (!becomes_root && !scene.name_to_entity.contains(parent_name_)) {
        error_ = "Choose a parent.";
        return false;
    }

    std::vector<std::unique_ptr<base_operation>> parts;
    parts.push_back(
        std::make_unique<create_entity_operation>(
            *engine_, *state_,
            create_entity_params{
                .name = name_, .parent_name = becomes_root ? std::string{} : parent_name_
            }
        )
    );

    switch (role_) {
        case entity_role::model:
            if (source_ == volume_source::blank) {
                if (size_.x <= 0 || size_.y <= 0 || size_.z <= 0) {
                    error_ = "Size dimensions must be greater than zero.";
                    return false;
                }
                parts.push_back(
                    std::make_unique<add_model_component_operation>(
                        *engine_, *state_,
                        add_model_component_params{
                            .name = name_, .size = size_, .category = category_
                        }
                    )
                );
            } else {
                if (model_file_.empty() || !library_->load(model_file_).has_value()) {
                    error_ = "Choose a volume file that loads.";
                    return false;
                }
                parts.push_back(
                    std::make_unique<attach_model_operation>(
                        *engine_, *state_, *library_,
                        attach_model_params{.name = name_, .source = model_file_}
                    )
                );
            }
            break;

        case entity_role::socket:
            parts.push_back(
                std::make_unique<add_socket_component_operation>(
                    *engine_, *state_, add_socket_component_params{.name = name_}
                )
            );
            break;

        case entity_role::point:
            parts.push_back(
                std::make_unique<set_point_operation>(
                    *engine_, *state_,
                    set_point_params{.name = name_, .kind = point_kind_, .tag = point_tag_}
                )
            );
            break;

        case entity_role::empty: break;
    }

    for (const auto& drawer : default_drawers().all()) {
        if (drawer.make_add && extras_.contains(drawer.tag) &&
            !covered_by_role_(drawer.component)) {
            parts.push_back(drawer.make_add(*engine_, *state_, name_));
        }
    }

    op_manager_->execute(std::make_unique<composite_operation>(std::move(parts)));
    return true;
}

}  // namespace vw::sculptor
