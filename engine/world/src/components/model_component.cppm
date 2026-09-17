export module vw.world:components.model;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :spatial;
import :components.transform;

export namespace vw::ecs {

class animation_fsm_system;
class animation_system;
class character_controller_system;
class hierarchy_system;
class light_system;
class model_system;
class physics_system;
class socket_system;
class spatial_system;
class transform_system;
class world_grid_system;

struct model_component final {
    [[nodiscard]] auto has_model() const -> bool {
        return model_ != nullptr;
    }

    [[nodiscard]] auto get_model() const -> std::shared_ptr<asset::model> {
        return model_;
    }

    // Чанк, которым эта модель стоит в мире, — со швами и запечённым светом; у
    // модели, которая чанком не является (всё в редакторе), его нет вовсе, и
    // мешер строит её как отдельно стоящую.
    [[nodiscard]] auto get_chunk() const -> const std::shared_ptr<asset::chunk_volume>& {
        return chunk_;
    }

    [[nodiscard]] auto get_voxel(int32 x, int32 y, int32 z) const -> voxel {
        return model_->get_voxel(x, y, z);
    }

    [[nodiscard]] auto get_voxel(vec3i pos) const -> voxel {
        return model_->get_voxel(pos);
    }

    [[nodiscard]] auto is_empty(int32 x, int32 y, int32 z) const -> bool {
        return model_->is_empty(x, y, z);
    }

    [[nodiscard]] auto is_empty(vec3i pos) const -> bool {
        return model_->is_empty(pos);
    }

    [[nodiscard]] auto width() const -> int32 {
        return model_->width();
    }

    [[nodiscard]] auto height() const -> int32 {
        return model_->height();
    }

    [[nodiscard]] auto depth() const -> int32 {
        return model_->depth();
    }

    [[nodiscard]] auto size() const -> vec3i {
        return model_->size();
    }

    [[nodiscard]] auto get_identity() const -> asset::model_identity {
        return model_->get_identity();
    }

    // Точка объёма, которой он садится на начало координат узла. У сущности без
    // модели её попросту нет, и объём тогда не рисуется.
    [[nodiscard]] auto get_pivot() const -> vec3f {
        return model_ ? model_->pivot() : vec3f{};
    }

    // Из какого .voxm пришёл объём. Пустая ссылка законна: у чанка мира и у
    // только что созданного узла файла ещё нет, имя ему даёт первая запись.
    [[nodiscard]] auto get_source() const -> const asset::asset_ref& {
        return source_;
    }

    // Скрытый объём не рисуется ни в одном проходе, включая тени, и луч выбора
    // проходит сквозь него. Геометрия при этом остаётся в буферах, а в файл флаг
    // не пишется: это вопрос того, на что сейчас смотрят, а не того, из чего
    // сцена состоит.
    [[nodiscard]] auto is_visible() const -> bool {
        return visible_;
    }

private:
    friend class model_system;

    std::shared_ptr<asset::model> model_;
    std::shared_ptr<asset::chunk_volume> chunk_;
    asset::asset_ref source_;
    bool visible_ = true;
};

// Матрица, в которой лежит сам воксельный объём: матрица узла плюс сдвиг на
// точку вращения модели. Двух компонентов она требует не случайно — объём без
// узла негде поставить, а узел без объёма её и не спрашивает. Дети наследуют
// матрицу узла и точку модели не видят, иначе замена варианта таскала бы за
// собой всё поддерево.
[[nodiscard]] inline auto model_matrix(
    const transform_component& transform_comp, const model_component& model_comp
) -> mat4f {
    return transform_comp.get_world_matrix() *
        math::translation_matrix(-model_comp.get_pivot());
}

}  // namespace vw::ecs
