export module vw.asset:model.edit;

import std;

import vw.core;
import :model.volume;

export namespace vw::asset {

class model_writer;

// Накопленный список правок: собирается отдельно от модели и ложится на неё
// целиком. Полезен там, где список нужен и сам по себе, — операция редактора
// хранит ровно его, чтобы уметь отмену, — а генерации, где правок сотни тысяч,
// дешевле писать через model_writer напрямую.
class voxel_batch {
public:
    auto set(vec3i pos, const voxel& value) -> voxel_batch& {
        edits_.push_back({.at = pos, .value = value, .kind = edit_kind::voxel});
        return *this;
    }

    // Целая страница одной записью. Рельеф ниже поверхности — один и тот же блок
    // на сотни вокселей подряд, и запись по вокселю стоит и цикла, и разреженной
    // страницы, которую пул потом сворачивает обратно.
    auto fill_page(vec3i page, const voxel& value) -> voxel_batch& {
        edits_.push_back({.at = page, .value = value, .kind = edit_kind::page});
        return *this;
    }

    [[nodiscard]] auto empty() const -> bool {
        return edits_.empty();
    }

    [[nodiscard]] auto size() const -> std::size_t {
        return edits_.size();
    }

    auto clear() -> void {
        edits_.clear();
    }

    // Правки ложатся в порядке записи, а поколение модели растёт один раз.
    auto apply_to(model& target) const -> void;

private:
    friend class model_writer;

    enum class edit_kind : uint8 { voxel, page };

    struct edit {
        vec3i at;
        voxel value;
        edit_kind kind;
    };

    std::vector<edit> edits_;
};

// Открытая правка модели: пишет в страницы напрямую, а поколение поднимает один
// раз, на выходе из скоупа.
//
// model::set_voxel берёт мьютекс пула идентичностей на каждый вызов: для одиночной
// правки это нормально, для генерации разорительно — чанк это десятки тысяч
// записей на четырёх воркерах. Свежей модели инвалидация не нужна вовсе, её ещё
// никто не мешил, но лишний инкремент поколения на весь чанк ничего не стоит и
// снимает с вызывающего необходимость об этом думать.
class model_writer {
public:
    explicit model_writer(model& target) : target_{&target} {}

    ~model_writer() {
        if (touched_) {
            target_->invalidate();
        }
    }

    model_writer(const model_writer&)                        = delete;
    auto operator=(const model_writer&) -> model_writer&     = delete;
    model_writer(model_writer&&)                             = delete;
    auto operator=(model_writer&&) -> model_writer&          = delete;

    auto set(int32 x, int32 y, int32 z, const voxel& value) -> model_writer& {
        target_->set_voxel_raw_(x, y, z, value);
        touched_ = true;
        return *this;
    }

    auto set(vec3i pos, const voxel& value) -> model_writer& {
        return set(pos.x, pos.y, pos.z, value);
    }

    auto fill_page(int32 px, int32 py, int32 pz, const voxel& value) -> model_writer& {
        target_->fill_page_raw_(px, py, pz, value);
        touched_ = true;
        return *this;
    }

    auto fill_page(vec3i page, const voxel& value) -> model_writer& {
        return fill_page(page.x, page.y, page.z, value);
    }

    auto apply(const voxel_batch& batch) -> model_writer& {
        for (const auto& item : batch.edits_) {
            if (item.kind == voxel_batch::edit_kind::voxel) {
                set(item.at, item.value);
            } else {
                fill_page(item.at, item.value);
            }
        }
        return *this;
    }

    // Записанная страница остаётся разреженной, даже если все воксели в ней
    // оказались одним блоком; сворачивание таких обратно в однородные и делает
    // глубокий мир подъёмным. Звать один раз, когда объём заполнен.
    auto compact_pages() -> uint32 {
        return target_->compact_pages();
    }

private:
    model* target_;
    bool touched_ = false;
};

}  // namespace vw::asset
