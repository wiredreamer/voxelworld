export module vw.world:grid.world_grid;
import :grid.visibility;
import :grid.chunk;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

// Загруженная часть воксельного мира: чанки по чанковой координате плюс учёт
// колонок, который ведёт загрузчик.
class world_grid {
public:
    explicit world_grid(world& w, int32 voxel_scale = 8);
    ~world_grid() = default;

    world_grid(const world_grid&)                    = delete;
    auto operator=(const world_grid&) -> world_grid& = delete;
    world_grid(world_grid&&)                         = delete;
    auto operator=(world_grid&&) -> world_grid&      = delete;

    [[nodiscard]] auto get_voxel(vec3i world_pos) const -> voxel;
    auto set_voxel(vec3i world_pos, const voxel& v) -> void;

    [[nodiscard]] auto has_chunk(vec3i chunk_coord) const -> bool;
    [[nodiscard]] auto get_chunk(vec3i chunk_coord) -> chunk*;

    // Самый верхний сплошной воксель над точкой, в вокселях, а не в мировых
    // единицах — и аргумент, и ответ. Всё остальное в этом классе принимает
    // мировые единицы, так что умножай на voxel_scale(), чтобы вернуться к ним.
    [[nodiscard]] auto get_surface_y(int32 vx, int32 vz) const -> std::optional<int32>;
    [[nodiscard]] auto has_column(vec2i coord) const -> bool;

    // Какие уровни чанков есть у загруженной колонки. Пусто для незагруженной;
    // уровни отсортированы.
    [[nodiscard]] auto column_levels(vec2i coord) const -> std::span<const int32>;
    [[nodiscard]] auto column_count() const -> uint32;
    [[nodiscard]] auto chunk_count() const -> uint32;

    // Из них те, что попали в сцену. Остальные — сплошная порода или открытый
    // воздух: загружены и проходимы, но рисовать в них нечего.
    [[nodiscard]] auto drawn_chunk_count() const -> uint32;

    auto place_chunk(vec3i chunk_coord, std::shared_ptr<asset::chunk_volume> volume)
        -> chunk*;
    auto register_column(vec2i coord, std::vector<int32> y_levels) -> void;
    auto unload_column(vec2i coord) -> void;

    // Снова выдаёт чанку шесть соседних плоскостей и ставит его на мешинг.
    // Плоскости — кэш над вокселями соседей и сбрасываются, когда мешер закончил,
    // поэтому правка выводит их заново. Правке это и нужно: копия, сохранённая с
    // момента размещения, была бы теперь устаревшей стороной шва.
    auto refresh_chunk(vec3i chunk_coord) -> void;

    // То же для чанка, уже стоящего в сцене, и только для него: перезаливка света
    // трогает каждый чанк колонки, а у погребённой породы нет граней, на которые
    // свет мог бы лечь. refresh_chunk выдал бы ей сущность и меш впустую.
    auto remesh_drawn_chunk(vec3i chunk_coord) -> void;

    // Колонки, чей небесный свет больше не соответствует вокселям; забираются и
    // очищаются. Сетка не знает, что такое свет: она знает, какие воксели
    // изменились и как далеко изменение может дойти, — поэтому называет колонки, а
    // перезаливку оставляет владельцу пекаря.
    [[nodiscard]] auto take_light_dirty() -> std::vector<vec2i>;

    [[nodiscard]] auto voxel_scale() const -> int32;

    // f(vec3i coord, const chunk&). Нужен рендереру: он обязан каждый кадр
    // что-то сказать о каждом загруженном чанке, видимом или нет.
    template <typename F>
    auto for_each_chunk(F&& f) const -> void {
        for (const auto& [coord, ptr] : chunks_) {
            f(coord, *ptr);
        }
    }

    [[nodiscard]] auto world_to_chunk_coord(vec3i world_pos) const -> vec3i;
    [[nodiscard]] auto world_to_local_coord(vec3i world_pos) const -> vec3i;
    [[nodiscard]] auto chunk_to_world_coord(vec3i chunk_coord) const -> vec3i;

private:
    auto mark_light_dirty_(vec3i chunk_coord, vec3i local) -> void;

    world* world_;
    int32 voxel_scale_{1};
    std::unordered_map<vec3i, std::unique_ptr<chunk>> chunks_;
    std::unordered_map<vec2i, std::vector<int32>> column_chunks_;
    std::unordered_set<vec2i> light_dirty_;
    uint32 drawn_chunks_ = 0;
};
}  // namespace vw::ecs
