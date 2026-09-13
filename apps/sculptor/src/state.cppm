export module vw.sculptor:state;

import std;

import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import vw.gfx;

// ---- from src/app/app_state.h
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
};

struct ui_state {
    float left_top_voffset    = 0.f;
    float left_bottom_voffset = 0.f;
    float right_top_voffset   = 0.f;

    bool need_startup_modal   = true;
    bool need_new_file_modal  = false;
    bool need_open_file_modal = false;
    bool need_save_as_modal   = false;

    float bottom_panel_height   = 400.f;
    bool show_timeline          = false;
    bool show_clip_manager      = false;
    bool show_sockets           = true;
    bool need_create_clip_modal = false;
    bool need_save_clip         = false;
    bool need_load_clip_modal   = false;
    bool need_close_clip        = false;
};

struct file_state {
    std::string filename;
    bool has_unsaved_changes = false;
};

struct scene_state {
    std::string selected_name;
    std::string root_name;
    std::unordered_map<std::string, ecs::entity> name_to_entity;
    std::unordered_map<ecs::entity, std::string> entity_to_name;
    std::vector<ecs::entity> entities;

    auto clear_entities(world_type& world) -> void;
};

struct tool_state {
    tools selected_tool     = tools::add_voxel;
    block_id selected_block = blocks::creature::cloth_white[2];

    // Кисть помнится на набор: модель несёт ровно один набор, и переход к
    // модели другого не должен стоить заново выбранного цвета. Плоский массив
    // по значению категории — полкилобайта и ни одной аллокации.
    std::array<block_id, 256> brush_of_set{};

    // Чем красить в этом наборе: запомненным блоком, а если такого ещё не было —
    // первым блоком набора.
    [[nodiscard]] auto brush_for(block_category category, const block_registry& registry) const
        -> block_id;
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
    bool animation_mode = false;

    bool need_toggle_playback = false;
    bool need_stop_playback   = false;
    bool need_add_keyframe    = false;
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

struct app_state {
    static constexpr std::string_view asset_dir_name = "models";

    ui_state ui;
    file_state file;
    scene_state scene;
    tool_state tool;
    animation_state anim;
    socket_state sockets;

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
