export module vw.asset:model.chunk;

import std;

import vw.core;
import :model.occupancy;
import :model.light_field;
import :model.volume;

export namespace vw::asset {

// Модель, поставленная в мир чанком: к самим вокселям добавляется то, чего у
// модели быть не может, — плоскости соседей по швам и запечённый свет.
//
// Всё это существует только у чанков мира: над моделью в редакторе нет неба, нет
// ламп и нет соседей, к которым её прижимают. Поэтому оно и живёт здесь, а не в
// model: та отвечает за воксели и страницы, и ничего про мир вокруг себя не знает.
class chunk_volume {
public:
    explicit chunk_volume(std::shared_ptr<model> voxels) : voxels_{std::move(voxels)} {}

    [[nodiscard]] auto voxels() -> model& {
        return *voxels_;
    }

    [[nodiscard]] auto voxels() const -> const model& {
        return *voxels_;
    }

    [[nodiscard]] auto shared_voxels() const -> const std::shared_ptr<model>& {
        return voxels_;
    }

    // Плоскость соседа, лежащего по face_direction: его обращённая сюда сторона в
    // виде битов занятости. Однородный сосед отвечает целиком, не читая вокселей.
    auto set_boundary_slice(int32 face_direction, const model& neighbor) -> void;

    // Верно только пока has_boundary_slice это подтверждает.
    [[nodiscard]] auto get_boundary_face(int32 face_direction) const -> const face_occupancy& {
        return boundary_->faces[face_direction];
    }

    [[nodiscard]] auto has_boundary_slice(int32 face_direction) const -> bool {
        return boundary_ != nullptr && (boundary_->valid & (1U << face_direction)) != 0;
    }

    [[nodiscard]] auto is_boundary_solid(int32 face_direction, int32 x, int32 y, int32 z) const
        -> bool;

    // Все соседние плоскости известны и в каждой выставлены все биты, то есть
    // снаружи этого чанка не видно ничего. Отсутствующий срез — открытое небо, и
    // считается не сплошным.
    [[nodiscard]] auto boundaries_are_solid() const -> bool;

    // Зовётся, когда меш построен. Всё, ради чего плоскости были нужны, к этому
    // моменту уже случилось, а держать их стоит трёх килобайт на чанк всё время,
    // пока чанк загружен.
    auto release_boundary() -> void {
        boundary_.reset();
    }

    // Небесный свет чанка либо ничего, если он ещё не освещён. Три килобайта,
    // когда он есть.
    //
    // Правка оставляет его на месте, а не сбрасывает. Устаревший свет на кадр, за
    // который идёт перезаливка, не виден вовсе; отсутствие света выглядит так,
    // будто чанк почернел.
    auto set_sky_light(light_field light) -> void;

    [[nodiscard]] auto get_sky_light() const -> const light_field* {
        return sky_.get();
    }

    [[nodiscard]] auto has_sky_light() const -> bool {
        return sky_ != nullptr;
    }

    // Другой канал, хранится так же. Держится отдельно от первого и никогда с ним
    // не суммируется: небесный свет — это видимость, которую умножает время суток,
    // а свет блоков — свет, которого оно трогать не должно. В сумме лампа гасла бы
    // к вечеру.
    auto set_block_light(light_field light) -> void;

    [[nodiscard]] auto get_block_light() const -> const light_field* {
        return block_.get();
    }

    [[nodiscard]] auto has_block_light() const -> bool {
        return block_ != nullptr;
    }

private:
    std::shared_ptr<model> voxels_;
    std::unique_ptr<model_boundary> boundary_;
    std::unique_ptr<light_field> sky_;
    std::unique_ptr<light_field> block_;
};

}  // namespace vw::asset
