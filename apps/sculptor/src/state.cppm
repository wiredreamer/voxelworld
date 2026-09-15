export module vw.sculptor:state;

import std;

import vw.core;
import vw.asset;
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
    move_pivot,
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
    bool show_rig               = true;
    bool need_create_clip_modal = false;
    bool need_save_clip         = false;
    bool need_load_clip_modal   = false;
    bool need_close_clip        = false;
};

struct file_state {
    std::string filename;
    bool has_unsaved_changes = false;

    // Узлы, чьи объёмы правились с последней записи. Флага на весь документ
    // мало: объёмы лежат отдельными файлами, и правка одного вокселя не должна
    // переписывать все .voxm префаба и шуметь в git.
    std::unordered_set<ecs::entity> dirty_models;
};

// Что правится прямо сейчас. Не флаг «режим анимации», а тип документа: из
// .voxf надо уметь провалиться в клип состояния, из префаба — в объём узла, и
// одним булевым полем такая вложенность не описывается.
enum class edit_kind : uint8 { prefab, model, clip, fsm };

// Под-ассет, в который провалились. Имени файла здесь нет намеренно: у объёма
// его знает узел, у клипа — anim.selected_clip_name, и обе ссылки меняются под
// рукой (переименование префаба, «Save As»). Контекст говорит, что правится, а
// как оно называется сейчас, крошка спрашивает у мира.
struct edit_context {
    edit_kind kind = edit_kind::model;
    std::string node_name;

    [[nodiscard]] static auto model(std::string node_name) -> edit_context {
        return edit_context{.kind = edit_kind::model, .node_name = std::move(node_name)};
    }

    [[nodiscard]] static auto clip() -> edit_context {
        return edit_context{.kind = edit_kind::clip};
    }
};

struct context_state {
    // Стек под-ассетов поверх документа. Пусто — правится сам префаб; его имя в
    // крошках берётся из file.filename, чтобы «Save As» не оставил там старое.
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

    // Узел выбирают и в префабе, и в клипе: в клипе выбор говорит, чью дорожку
    // правят и кого двигает гизмо. Внутри объёма выбирать нечего — узел уже
    // назван контекстом, а в автомате узлов нет вовсе.
    [[nodiscard]] auto allows_node_select() const -> bool {
        return kind() == edit_kind::prefab || kind() == edit_kind::clip;
    }

    // Сам объём правят и из префаба, и провалившись в него: точка вращения и
    // обрезка нужны там же, где воксели. В клипе не правится ничего — позу
    // ведёт дорожка.
    [[nodiscard]] auto allows_volume_edit() const -> bool {
        return kind() == edit_kind::prefab || kind() == edit_kind::model;
    }

    // Узел, чей под-ассет открыт. Пусто — либо префаб, либо контекст без узла.
    [[nodiscard]] auto node_name() const -> std::string_view {
        return stack.empty() ? std::string_view{} : std::string_view{stack.back().node_name};
    }

    // Воксельные инструменты работают только внутри объёма, выбор узла — только
    // в самом префабе. Инструмент, которому в этом контексте нечего трогать, не
    // прячется «серым», а не показывается вовсе: серая кнопка обещает, что её
    // когда-нибудь можно нажать здесь.
    [[nodiscard]] auto allows_tool(tools tool) const -> bool {
        switch (tool) {
            case tools::select_entity: return allows_node_select();
            case tools::add_voxel:
            case tools::remove_voxel:
            case tools::paint_voxel:
            case tools::color_picker:
            case tools::move_pivot: return kind() == edit_kind::model;
            case tools::invalid: break;
        }
        return false;
    }

    [[nodiscard]] auto default_tool() const -> tools {
        return kind() == edit_kind::model ? tools::add_voxel : tools::select_entity;
    }

    auto enter(edit_context ctx) -> void {
        stack.push_back(std::move(ctx));
    }

    // Выход по крошке: глубина 0 — сам документ.
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

    auto clear_entities(world_type& world) -> void;
};

// Занятый объём модели, о которой сейчас идёт речь. Считается один раз на смену
// модели и читается и панелью, и рамкой в сцене: обход 64-куба — четверть
// миллиона проверок, и каждый кадр их делать не за что.
struct volume_state {
    asset::model_identity source = asset::invalid_model_identity;
    std::optional<asset::voxel_bounds> occupied;
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
    // Корень ассетов задаётся на конфигурации (VW_SCULPTOR_ASSET_ROOT) и по
    // умолчанию указывает на assets/ репозитория: редактор правит те же файлы,
    // которые читают arena и git. Копия в каталоге сборки этого не давала бы —
    // правка оставалась бы там и погибала при следующей сборке.
    static constexpr std::string_view asset_root_name = VW_SCULPTOR_ASSET_ROOT;

    // Имена каталогов лежат в vw.asset (asset::dirs): по ним же движок строит
    // ссылку на безымянный объём. Здесь они только превращаются в путь на
    // диске — ссылка отсчитывается от корня, а не от рабочего каталога.
    [[nodiscard]] static auto prefab_dir() -> std::filesystem::path;
    [[nodiscard]] static auto model_dir() -> std::filesystem::path;
    [[nodiscard]] static auto clip_dir() -> std::filesystem::path;
    [[nodiscard]] static auto fsm_dir() -> std::filesystem::path;

    ui_state ui;
    file_state file;
    context_state ctx;
    scene_state scene;
    volume_state volume;
    tool_state tool;
    animation_state anim;
    socket_state sockets;

    // Узел, о котором идёт речь: его называет контекст, а если контекст узла не
    // называет — префаб и клип не называют — то выделение. Undo умеет вернуть
    // контекст чужого объёма, и править тогда надо тот узел, чьё имя в крошках.
    [[nodiscard]] auto edited_node() const -> const std::string& {
        if (!ctx.stack.empty() && !ctx.stack.back().node_name.empty()) {
            return ctx.stack.back().node_name;
        }
        return scene.selected_name;
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
