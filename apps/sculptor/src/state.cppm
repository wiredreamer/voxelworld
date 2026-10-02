export module vw.sculptor:state;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

export namespace vw::sculptor {

using world_type    = ecs::world;
using keyframe_value = std::variant<asset::keyframe_vec3f, asset::keyframe_quat>;

enum class tools : uint8 {
    invalid,
    select_entity,
    add_voxel,
    remove_voxel,
    paint_voxel,
    color_picker,
    move_pivot,
    select_box,
    place_paste,

    pose,
};

enum class gizmo_mode : uint8 { translate, rotate, scale };

enum class panels : uint8 {
    tools,
    palette,
    gizmo,
    properties,
    entity_tree,
    sockets,
    timeline,
    keyframe,
    fsm,
    paste,
    selection,
};

struct ui_state {
    float32 left_offset   = 0.f;
    float32 bottom_offset = 0.f;
    float32 right_offset  = 0.f;

    bool need_startup_modal   = true;
    bool startup_modal_open   = false;
    bool need_new_file_modal  = false;
    bool need_open_file_modal = false;
    bool need_save_as_modal   = false;
    bool need_close_file      = false;
    bool need_shortcuts_modal = false;

    float32 bottom_panel_height = 400.f;
    bool show_timeline          = false;
    bool show_sockets           = true;

    bool need_enter_animation   = false;
    std::string need_add_model_for;
    std::string need_rename_model_for;

    std::string need_add_candidate_for;

    bool need_add_machine_modal = false;

    std::optional<std::size_t> need_enter_machine;

    bool need_create_clip_modal = false;
    bool need_save_clip         = false;
    bool need_load_clip_modal   = false;
    bool need_close_clip        = false;
};

struct file_state {
    std::string filename;
    bool has_unsaved_changes = false;

    std::unordered_set<ecs::entity> dirty_models;
};

enum class edit_kind : uint8 { prefab, model, clip, fsm, paste };

struct edit_context {
    edit_kind kind = edit_kind::model;
    std::string node_name;

    std::size_t layer = 0;

    [[nodiscard]] static auto model(std::string node_name) -> edit_context {
        return edit_context{.kind = edit_kind::model, .node_name = std::move(node_name)};
    }

    [[nodiscard]] static auto paste(std::string node_name) -> edit_context {
        return edit_context{.kind = edit_kind::paste, .node_name = std::move(node_name)};
    }

    [[nodiscard]] static auto clip() -> edit_context {
        return edit_context{.kind = edit_kind::clip};
    }

    [[nodiscard]] static auto fsm(std::size_t layer) -> edit_context {
        return edit_context{.kind = edit_kind::fsm, .layer = layer};
    }
};

struct context_state {
    std::vector<edit_context> stack;

    [[nodiscard]] auto kind() const -> edit_kind {
        return stack.empty() ? edit_kind::prefab : stack.back().kind;
    }

    [[nodiscard]] auto in_prefab() const -> bool {
        return stack.empty();
    }

    [[nodiscard]] auto in_clip() const -> bool {
        return kind() == edit_kind::clip;
    }

    [[nodiscard]] auto in_fsm() const -> bool {
        return kind() == edit_kind::fsm;
    }

    [[nodiscard]] auto layer() const -> std::size_t {
        return stack.empty() ? 0 : stack.back().layer;
    }

    [[nodiscard]] auto allows_node_select() const -> bool {
        return kind() == edit_kind::prefab || kind() == edit_kind::clip;
    }

    [[nodiscard]] auto in_paste() const -> bool {
        return kind() == edit_kind::paste;
    }

    [[nodiscard]] auto allows_volume_edit() const -> bool {
        return kind() == edit_kind::model;
    }

    [[nodiscard]] auto shows_volume() const -> bool {
        return kind() == edit_kind::model || kind() == edit_kind::paste;
    }

    [[nodiscard]] auto node_name() const -> std::string_view {
        return stack.empty() ? std::string_view{} : std::string_view{stack.back().node_name};
    }

    [[nodiscard]] auto allows_tool(tools tool) const -> bool {
        switch (tool) {
            case tools::select_entity: return in_prefab();
            case tools::pose: return in_clip();
            case tools::add_voxel:
            case tools::remove_voxel:
            case tools::paint_voxel:
            case tools::color_picker:
            case tools::move_pivot:
            case tools::select_box: return kind() == edit_kind::model;
            case tools::place_paste: return in_paste();
            case tools::invalid: break;
        }
        return false;
    }

    [[nodiscard]] auto default_tool() const -> tools {
        switch (kind()) {
            case edit_kind::model: return tools::add_voxel;
            case edit_kind::clip: return tools::pose;
            case edit_kind::paste: return tools::place_paste;
            case edit_kind::prefab:
            case edit_kind::fsm: break;
        }
        return tools::select_entity;
    }

    [[nodiscard]] auto shows(panels panel) const -> bool {
        switch (panel) {
            case panels::tools:
            case panels::palette: return kind() == edit_kind::model;

            case panels::gizmo: return allows_node_select();

            case panels::properties:
                return kind() == edit_kind::prefab || kind() == edit_kind::model;

            case panels::entity_tree:
                return kind() == edit_kind::prefab || kind() == edit_kind::model;

            case panels::sockets: return in_prefab();

            case panels::timeline:
            case panels::keyframe: return in_clip();

            case panels::fsm: return in_fsm();

            case panels::paste: return in_paste();

            case panels::selection: return kind() == edit_kind::model;
        }
        return false;
    }

    auto enter(edit_context ctx) -> void {
        stack.push_back(std::move(ctx));
    }

    auto leave_to(std::size_t depth) -> void {
        if (depth < stack.size()) {
            stack.resize(depth);
        }
    }
};

struct scene_state {
    std::string selected_name;
    std::string root_name;
    std::unordered_map<std::string, ecs::entity> name_to_entity;
    std::unordered_map<ecs::entity, std::string> entity_to_name;
    std::vector<ecs::entity> entities;

    std::unordered_set<std::string> hidden_nodes;

    auto clear_entities(world_type& world) -> void;
};

struct volume_selection {
    asset::voxel_bounds box;
    std::string node_name;
    vec3i volume_size;
};

struct volume_state {
    asset::model_identity source = asset::invalid_model_identity;
    std::optional<asset::voxel_bounds> occupied;
    std::optional<volume_selection> selection;
};

struct clipboard_state {
    asset::voxel_clip clip;
};

struct paste_state {
    asset::voxel_clip clip;
    vec3i origin;
    asset::paste_mode mode = asset::paste_mode::keep_air;

    std::string node_name;
    std::shared_ptr<asset::model> base;
    bool preview_stale = false;

    [[nodiscard]] auto active() const -> bool {
        return base != nullptr;
    }

    [[nodiscard]] auto base_matrix(const mat4f& node_world_matrix) const -> mat4f {
        return node_world_matrix * math::translation_matrix(-base->pivot());
    }

    [[nodiscard]] auto centre() const -> vec3f {
        return vec3f{
            static_cast<float32>(origin.x) + (static_cast<float32>(clip.size.x) * 0.5F),
            static_cast<float32>(origin.y) + (static_cast<float32>(clip.size.y) * 0.5F),
            static_cast<float32>(origin.z) + (static_cast<float32>(clip.size.z) * 0.5F),
        };
    }
};

struct tool_state {
    tools selected_tool     = tools::add_voxel;
    tools tool_before_paste = tools::add_voxel;
    gizmo_mode gizmo        = gizmo_mode::translate;
    voxel selected_voxel = voxels::gray[9];
};

struct clip_settings {
    float32 playback_speed             = 1.0f;
    asset::animation_loop_mode loop_mode = asset::animation_loop_mode::once;
    asset::transition blend_transition   = {};
    asset::transition fade_in            = {};
    asset::transition fade_out           = {};
};

struct animation_state {
    std::string selected_clip_name;
    std::string selected_track_name;
    asset::animation_property selected_property = asset::animation_property::position;

    uint32 selected_keyframe_id = asset::invalid_keyframe_id;
    float32 timeline_cursor     = 0.f;
    std::unordered_set<std::string> expanded_tracks;

    bool need_toggle_playback = false;
    bool need_stop_playback   = false;
    bool need_record_key     = false;
    bool need_record_key_all = false;
    bool need_delete_keyframe = false;
    bool need_step_forward    = false;
    bool need_step_backward   = false;
    bool need_apply_pose      = false;

    std::unordered_map<std::string, bool> unsaved_clips;
    std::unordered_map<std::string, std::size_t> clip_to_layer;
    std::unordered_map<std::string, clip_settings> clip_settings_map;
    std::unordered_map<std::string, transform> saved_transforms;
    bool has_saved_transforms = false;

    [[nodiscard]] auto has_unsaved_clip(const std::string& name) const -> bool;
    [[nodiscard]] auto has_any_unsaved_clip() const -> bool;
    [[nodiscard]] auto get_layer_for_clip(const std::string& name) const -> std::size_t;
    [[nodiscard]] auto get_clip_settings(const std::string& name) const -> clip_settings;
    [[nodiscard]] auto get_clip_settings_mut(const std::string& name) -> clip_settings&;
};

struct socket_state {
    struct socket_preview {
        std::string filename;
        std::string preview_root_name;
        std::vector<ecs::entity> entities;

        auto destroy_entities(world_type& world) -> void;
    };

    std::unordered_map<std::string, socket_preview> socket_previews;

    [[nodiscard]] static auto socket_preview_key(
        const std::string& entity_name, const std::string& socket_name
    ) -> std::string {
        return std::format("{}:{}", entity_name, socket_name);
    }

    [[nodiscard]] auto get_preview_entities() const -> std::unordered_set<ecs::entity> {
        std::unordered_set<ecs::entity> result;
        for (const auto& preview : socket_previews | std::views::values) {
            for (const auto ent : preview.entities) {
                result.insert(ent);
            }
        }
        return result;
    }

    auto erase_preview(const std::string& key, world_type& world) -> void;
    auto erase_previews_for(const std::string& entity_name, world_type& world) -> void;
    auto clear_all(world_type& world) -> void;
};

struct fsm_document {
    asset::asset_ref source;
    asset::voxf_data data;
    bool has_unsaved_changes = false;

    std::string selected_state;

    [[nodiscard]] auto is_open() const -> bool {
        return !source.empty();
    }

    [[nodiscard]] auto incoming(std::string_view state_name) const -> std::vector<std::string> {
        std::vector<std::string> sources;

        for (const auto& state : data.states) {
            const auto leads = std::ranges::any_of(
                state.transitions,
                [state_name](const asset::animation_fsm::transition_rule& rule) {
                    return rule.target_state == state_name;
                }
            );
            if (leads) {
                sources.push_back(state.name);
            }
        }

        const auto from_any = std::ranges::any_of(
            data.any_transitions,
            [state_name](const asset::animation_fsm::transition_rule& rule) {
                return rule.target_state == state_name;
            }
        );
        if (from_any) {
            sources.emplace_back("any");
        }

        return sources;
    }
};

struct mcp_status {
    bool enabled    = false;
    bool listening  = false;
    uint16 port     = 0;
    uint64 requests = 0;
    std::string last_tool;
    std::string failure;
};

struct app_state {
    static constexpr std::string_view asset_root_name = VW_SCULPTOR_ASSET_ROOT;

    [[nodiscard]] static auto prefab_dir() -> std::filesystem::path;
    [[nodiscard]] static auto model_dir() -> std::filesystem::path;
    [[nodiscard]] static auto clip_dir() -> std::filesystem::path;
    [[nodiscard]] static auto fsm_dir() -> std::filesystem::path;

    ui_state ui;
    file_state file;
    context_state ctx;
    fsm_document fsm;
    scene_state scene;
    volume_state volume;
    clipboard_state clipboard;
    paste_state paste;
    tool_state tool;
    animation_state anim;
    socket_state sockets;
    mcp_status mcp;

    [[nodiscard]] auto edited_node() const -> const std::string& {
        if (!ctx.stack.empty() && !ctx.stack.back().node_name.empty()) {
            return ctx.stack.back().node_name;
        }
        return scene.selected_name;
    }

    [[nodiscard]] auto has_unsaved_changes() const -> bool {
        return file.has_unsaved_changes || fsm.has_unsaved_changes ||
            anim.has_any_unsaved_clip();
    }

    auto reset(world_type& world) -> void;
};

}  // namespace vw::sculptor

template <>
struct std::hash<vw::sculptor::tools> {
    auto operator()(
        vw::sculptor::tools t
    ) const noexcept -> std::size_t {
        return std::hash<vw::uint32>()(static_cast<vw::uint32>(t));
    }
};
