export module vw.world:systems.world_grid;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;
import :grid;
import :spatial;
import :light;
import :terrain;

export namespace vw::ecs {

class world;

struct world_grid_system_stats {
    float32 integrate_ms       = 0.0F;
    float32 stage_ms           = 0.0F;
    float32 boundary_from_ms   = 0.0F;
    float32 chunk_create_ms    = 0.0F;
    float32 request_columns_ms = 0.0F;
    float32 rebuild_active_ms  = 0.0F;
    float32 unload_ms          = 0.0F;
    uint32 active_count        = 0;
    uint32 pending_count       = 0;
    uint32 loaded_count        = 0;

    // Из загруженных те, за которыми стоит сущность. Сплошной породе и открытому
    // воздуху рисовать нечего, и они не получают ни сущности, ни меша.
    uint32 drawn_count = 0;

    // Сгенерировано и придержано, пока не появятся все восемь соседей.
    uint32 staged_count = 0;

    // Из придержанных те, у которых задача на свет в полёте.
    uint32 lighting_count = 0;

    // Присоединение готового света к его моделям и возврат изменённых колонок
    // пекарю. Заливка идёт на воркере; это та часть, что попадает в кадр.
    float32 light_apply_ms = 0.0F;

    // Размещённые колонки, ждущие повторного освещения после правки, и счёт тех,
    // что были отправлены с момента загрузки мира.
    uint32 relight_backlog = 0;
    uint64 relit_columns   = 0;

    // Из чанков, вернувшихся с перезаливки, те, чей свет действительно изменился и
    // потому потребовал нового меша. Копка в темноте не меняет ничего и стоить
    // ничего не должна.
    uint64 relit_chunks = 0;
};

class world_grid_system {
public:
    static constexpr std::string_view system_name = "world_grid";

    explicit world_grid_system(world& w);
    ~world_grid_system();

    world_grid_system(const world_grid_system&)                    = delete;
    auto operator=(const world_grid_system&) -> world_grid_system& = delete;
    world_grid_system(world_grid_system&&) noexcept;
    auto operator=(world_grid_system&&) noexcept -> world_grid_system&;

    auto set_grid(std::unique_ptr<world_grid> grid) -> void;
    auto set_loader(std::unique_ptr<chunk_loader> loader) -> void;

    [[nodiscard]] auto grid() -> world_grid*;
    [[nodiscard]] auto grid() const -> const world_grid*;
    [[nodiscard]] auto loader() -> chunk_loader*;
    [[nodiscard]] auto loader() const -> const chunk_loader*;
    [[nodiscard]] auto has_grid() const -> bool;
    [[nodiscard]] auto has_loader() const -> bool;

    auto update(float32 dt) -> void;
    auto shutdown() -> void;

    [[nodiscard]] auto get_stats() const -> const world_grid_system_stats&;
    [[nodiscard]] auto get_loader_stats() const -> column_gen_stats;
    [[nodiscard]] auto get_light_stats() const -> light_stats;

    class view_modifier {
    public:
        auto set_view_distance(uint32 distance) -> view_modifier&;

    private:
        friend class world_grid_system;
        view_modifier(world_grid_system* system, entity ent);

        world_grid_system* system_;
        entity entity_;
    };

    auto modify_view(entity ent) -> view_modifier;

private:
    // Колонка ждёт в накопителе, пока не сгенерированы все восемь её горизонтальных
    // соседей, а затем — пока не залит её небесный свет. Только после этого её
    // чанки попадают в сетку, уже зная все границы и все уровни, — поэтому каждый
    // чанк мешится один раз, а не по разу на каждого подоспевшего соседа.
    //
    // Для границ хватало четырёх соседей. Свету нужны и диагонали: устье пещеры на
    // углу освещается через неё.
    //
    // Поэтому активное множество на колонку шире дальности видимости: внешнее
    // кольцо существует ради того, чтобы дополнить кольцо внутри, и не размещается
    // никогда.
    static constexpr int32 apron_columns = 1;

    auto process_dirty_entity_(entity ent) -> bool;
    auto process_dirty_entities_() -> bool;
    auto stage_completed_columns_() -> void;
    auto collect_lit_columns_() -> void;
    auto relight_dirty_columns_() -> void;
    auto apply_relit_column_(light_result& result) -> void;
    auto integrate_completed_columns_() -> void;
    auto dispatch_light_(vec2i coord) -> bool;
    [[nodiscard]] auto column_stack_(vec2i coord, int32 bottom)
        -> std::vector<std::shared_ptr<asset::model>>;
    [[nodiscard]] auto column_bottom_(vec2i coord) -> std::optional<int32>;
    [[nodiscard]] static auto already_lit_(gen_column& col) -> bool;
    [[nodiscard]] auto column_available_(vec2i coord) const -> bool;
    [[nodiscard]] auto column_ready_(vec2i coord) const -> bool;
    [[nodiscard]] auto within_draw_(vec2i coord) const -> bool;
    [[nodiscard]] auto model_at_(vec3i chunk_coord) const -> asset::model*;
    auto queue_if_ready_(vec2i coord) -> void;
    auto demote_column_(vec2i coord) -> void;
    auto dispatch_column_requests_() -> void;
    auto update_grid_stats_() -> void;
    auto rebuild_active_set_() -> vec2i;
    auto unload_inactive_columns_() -> void;
    auto rebuild_pending_requests_(vec2i camera_column) -> void;
    auto clear_grid_transient_state_() -> void;
    auto clear_loader_transient_state_() -> void;

    world* world_;
    std::unique_ptr<world_grid> grid_;
    std::unique_ptr<chunk_loader> loader_;
    std::unique_ptr<light_baker> baker_;
    std::unordered_set<vec2i> active_columns_;
    std::unordered_set<vec2i> pending_active_columns_;
    std::vector<vec2i> pending_requests_;
    std::unordered_map<vec2i, std::unique_ptr<gen_column>> staged_columns_;
    std::vector<vec2i> ready_columns_;

    // Размещённые колонки, которые правка сделала неверными. Колонка, уже
    // освещаемая, остаётся здесь до прихода той задачи: задача, в которой она
    // сейчас, стартовала от вокселей в состоянии до правки.
    std::unordered_set<vec2i> light_dirty_;

    // Юбка генерируется, но никогда не рисуется, поэтому она не должна держать
    // живыми отрисованные колонки позади камеры: накопитель достаёт на колонку
    // дальше этой границы, а размещённые колонки отпускаются на ней.
    vec2i camera_column_{};
    int32 draw_distance_ = 0;

    world_grid_system_stats stats_;
};
}  // namespace vw::ecs
