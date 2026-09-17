export module vw.world:grid.chunk;
import :grid.visibility;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :light;

export namespace vw::ecs {

// Куб вокселей, опирающийся на собственную модель и владеющий сущностью, которая
// несёт его в сцене.
//
// Чанку, которому нечего показать, сущность не выдаётся вовсе. Погребённая порода
// — обычный случай: семь чанков из девяти в колонке мира глубиной 512 вокселей, —
// а сущность покупает меш, инстанс на GPU и место в каждом покадровом обходе.
// Воксели при этом на месте: мир можно копать, и копка как раз и выдаёт чанку
// сущность.
class chunk {
public:
    static constexpr int32 size   = 64;
    static constexpr int32 volume = size * size * size;

    chunk(world& w, vec3i coord, std::shared_ptr<asset::chunk_volume> content,
          int32 voxel_scale = 1);
    ~chunk();

    chunk(const chunk&)                    = delete;
    auto operator=(const chunk&) -> chunk& = delete;
    chunk(chunk&& other) noexcept;
    auto operator=(chunk&& other) noexcept -> chunk&;

    [[nodiscard]] auto get_voxel(int32 x, int32 y, int32 z) const -> voxel;
    [[nodiscard]] auto get_voxel(vec3i local) const -> voxel;
    auto set_voxel(int32 x, int32 y, int32 z, voxel v) -> void;
    auto set_voxel(vec3i local, voxel v) -> void;
    [[nodiscard]] auto is_empty(int32 x, int32 y, int32 z) const -> bool;

    [[nodiscard]] auto get_model() const -> std::shared_ptr<asset::model>;
    [[nodiscard]] auto get_volume() const -> std::shared_ptr<asset::chunk_volume>;

    // Недействительна для чанка, которого нет в сцене. Вызывающие, ищущие
    // сущность в таблице, не получают ничего, и это верный ответ, но обход
    // видимости обязан спрашивать is_solid: несмешенный чанк считается там
    // полностью открытым, а сплошная порода — прямая ему противоположность.
    [[nodiscard]] auto get_entity() const -> entity;
    [[nodiscard]] auto is_drawn() const -> bool;
    [[nodiscard]] auto is_solid() const -> bool;

    // О каких из шести соседей чанку сообщили при размещении. Сами плоскости
    // сбрасываются, как только мешер с ними закончил, — или сразу, если чанк
    // мешить не будут, — и модель перестаёт отвечать; а вот были ли они, и
    // говорит, что чанк поставили в полную окрестность.
    [[nodiscard]] auto known_neighbors() const -> uint8;

    // Вводит пропущенный чанк в сцену. Истина, если раньше его там не было.
    auto ensure_entity() -> bool;

    [[nodiscard]] static constexpr auto contains(int32 x, int32 y, int32 z) -> bool {
        return x >= 0 && x < size && y >= 0 && y < size && z >= 0 && z < size;
    }

    auto set_known_neighbors(uint8 mask) -> void;

private:
    auto create_entity_() -> void;

    world* world_;
    vec3i coord_{};
    int32 voxel_scale_{1};
    entity ent_;
    std::shared_ptr<asset::chunk_volume> volume_;
    asset::model_fill fill_ = asset::model_fill::mixed;
    uint8 known_neighbors_  = 0;
};

}  // namespace vw::ecs
