export module vw.asset:model.edit;

import std;

import vw.core;
import :model.volume;

export namespace vw::asset {

class model_writer;

class voxel_batch {
public:
    auto set(vec3i pos, voxel value) -> voxel_batch& {
        edits_.push_back({.at = pos, .value = value, .kind = edit_kind::voxel});
        return *this;
    }

    auto fill_page(vec3i page, voxel value) -> voxel_batch& {
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

    auto set(int32 x, int32 y, int32 z, voxel value) -> model_writer& {
        target_->set_voxel_raw_(x, y, z, value);
        touched_ = true;
        return *this;
    }

    auto set(vec3i pos, voxel value) -> model_writer& {
        return set(pos.x, pos.y, pos.z, value);
    }

    auto fill_page(int32 px, int32 py, int32 pz, voxel value) -> model_writer& {
        target_->fill_page_raw_(px, py, pz, value);
        touched_ = true;
        return *this;
    }

    auto fill_page(vec3i page, voxel value) -> model_writer& {
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

    auto compact_pages() -> uint32 {
        return target_->compact_pages();
    }

private:
    model* target_;
    bool touched_ = false;
};

struct voxel_bounds {
    vec3i min;
    vec3i max;

    [[nodiscard]] auto size() const -> vec3i {
        return vec3i{max.x - min.x + 1, max.y - min.y + 1, max.z - min.z + 1};
    }
};

[[nodiscard]] auto occupied_bounds(const model& source) -> std::optional<voxel_bounds>;

[[nodiscard]] auto trimmed(const model& source, model_registry& registry)
    -> std::shared_ptr<model>;

}  // namespace vw::asset
