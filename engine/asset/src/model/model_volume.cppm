export module vw.asset:model.volume;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;
import :model.links;

export namespace vw::asset {

class model {
public:
    static constexpr int32 page_size   = voxel_page_size;
    static constexpr int32 page_volume = voxel_page_volume;
    using page_type                    = page_pool::page_type;

    model(
        model_identity_pool& identity_pool,
        page_pool& pool,
        int32 width,
        int32 height,
        int32 depth,
        int32 world_units_per_voxel = 1
    );
    ~model();

    model(const model&)                    = delete;
    auto operator=(const model&) -> model& = delete;

    model(model&& other) noexcept;
    auto operator=(model&& other) noexcept -> model&;

    auto set_voxel(int32 x, int32 y, int32 z, voxel v) -> void;

    auto set_voxel(
        vec3i pos, voxel v
    ) -> void {
        set_voxel(pos.x, pos.y, pos.z, v);
    }

    [[nodiscard]] auto get_voxel(
        int32 x, int32 y, int32 z
    ) const -> voxel {
        const auto& entry = pages_[page_index(x / page_size, y / page_size, z / page_size)];

        switch (entry.mode()) {
            case page_mode::empty:
                return voxel{};
            case page_mode::uniform:
                return entry.fill_voxel();
            default:
                return view_of(entry).voxel_at(
                    x % page_size, y % page_size, z % page_size
                );
        }
    }

    [[nodiscard]] auto get_voxel(
        vec3i pos
    ) const -> voxel {
        return get_voxel(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto is_empty(
        int32 x, int32 y, int32 z
    ) const -> bool {
        const auto& entry = pages_[page_index(x / page_size, y / page_size, z / page_size)];

        switch (entry.mode()) {
            case page_mode::empty:
                return true;
            case page_mode::uniform:
                return false;
            default:
                return view_of(entry)
                    .voxel_at(x % page_size, y % page_size, z % page_size)
                    .is_empty();
        }
    }

    [[nodiscard]] auto is_empty(
        vec3i pos
    ) const -> bool {
        return is_empty(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto width() const -> int32 {
        return width_;
    }

    [[nodiscard]] auto height() const -> int32 {
        return height_;
    }

    [[nodiscard]] auto depth() const -> int32 {
        return depth_;
    }

    [[nodiscard]] auto size() const -> vec3i {
        return vec3i{width_, height_, depth_};
    }

    [[nodiscard]] auto world_units_per_voxel() const -> int32 {
        return world_units_per_voxel_;
    }

    [[nodiscard]] auto pivot() const -> const vec3f& {
        return pivot_;
    }

    auto set_pivot(const vec3f& pivot) -> void {
        pivot_ = pivot;
    }

    auto compact_pages() -> uint32;

    [[nodiscard]] auto extract_face(face_direction face, face_occupancy& out) const -> bool;

    [[nodiscard]] auto scan_fill() const -> model_fill;

    [[nodiscard]] auto build_occupancy(chunk_occupancy& out) const -> bool;

    [[nodiscard]] auto page_may_hold(int32 px, int32 py, int32 pz, const voxel_set& wanted) const -> bool;

    // см. docs/rendering.md#качание-листвы
    [[nodiscard]] auto build_rows_of(chunk_occupancy& out, const voxel_set& wanted) const -> bool;

    [[nodiscard]] auto extract_face(face_direction face, face_occupancy& out, const voxel_set& wanted,
                                    face_occupancy& wanted_out) const -> bool;

    [[nodiscard]] auto build_x_rows(
        chunk_occupancy& out, int32 px0, int32 px1, int32 pz0, int32 pz1
    ) const -> bool;

    [[nodiscard]] auto build_x_rows(
        chunk_occupancy& out
    ) const -> bool {
        return build_x_rows(out, 0, pages_x_, 0, pages_z_);
    }

    auto invalidate() -> void;

    auto fill(voxel v) -> void;

    [[nodiscard]] auto get_identity() const -> model_identity {
        return identity_;
    }

    auto clone_pages_from(const model& source) -> void;

    [[nodiscard]] auto get_page_mode(
        int32 px, int32 py, int32 pz
    ) const -> page_mode {
        return pages_[page_index(px, py, pz)].mode();
    }

    [[nodiscard]] auto get_page_fill(
        int32 px, int32 py, int32 pz
    ) const -> voxel {
        return pages_[page_index(px, py, pz)].fill_voxel();
    }

    [[nodiscard]] auto get_page(
        int32 px, int32 py, int32 pz
    ) const -> page_view {
        return view_of(pages_[page_index(px, py, pz)]);
    }

    [[nodiscard]] auto pages_x() const -> int32 {
        return pages_x_;
    }

    [[nodiscard]] auto pages_y() const -> int32 {
        return pages_y_;
    }

    [[nodiscard]] auto pages_z() const -> int32 {
        return pages_z_;
    }

private:
    friend class model_writer;

    auto set_voxel_raw_(int32 x, int32 y, int32 z, voxel v) -> void;
    auto fill_page_raw_(int32 px, int32 py, int32 pz, voxel v) -> void;

    [[nodiscard]] auto page_index(
        int32 px, int32 py, int32 pz
    ) const -> int32 {
        return px + (py * pages_x_) + (pz * pages_x_ * pages_y_);
    }

    [[nodiscard]] auto view_of(
        const page_entry& entry
    ) const -> page_view {
        switch (entry.mode()) {
            case page_mode::dense:
                return page_view{pool_ptr_->get_dense(entry.dense_slot())};
            case page_mode::binary:
                return page_view{
                    pool_ptr_->get_binary(entry.binary_slot()), entry.fill_voxel()
                };
            case page_mode::palette:
                return page_view{pool_ptr_->get_palette(entry.palette_slot())};
            default:
                return page_view{};
        }
    }

    auto release_page_(page_entry entry) -> void;
    auto release_all_pages_() -> void;
    auto make_binary_(page_entry& entry, voxel fill, bool solid) -> binary_page&;
    auto promote_to_dense(int32 px, int32 py, int32 pz) -> page_type&;

    auto increment_generation_() -> void;

    model_identity_pool* identity_pool_;
    page_pool* pool_ptr_;
    int32 width_{0}, height_{0}, depth_{0};
    int32 world_units_per_voxel_{1};
    vec3f pivot_{0.0F, 0.0F, 0.0F};
    int32 pages_x_{0}, pages_y_{0}, pages_z_{0};
    std::vector<page_entry> pages_;
    std::vector<uint32> owned_dense_;
    std::vector<uint32> owned_binary_;
    std::vector<uint32> owned_palette_;
    model_identity identity_;
    mutable model_fill fill_ = model_fill::mixed;
    mutable bool fill_known_ = false;
};

class model_registry {
public:
    [[nodiscard]] auto has(std::string_view name) const -> bool;
    [[nodiscard]] auto get(std::string_view name) const -> std::shared_ptr<model>;

    [[nodiscard]] auto create(
        std::string_view name, int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create(std::string_view name, vec3i size)
        -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(
        int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(vec3i size)
        -> std::shared_ptr<model>;
    [[nodiscard]] auto create_clone(std::string_view name) -> std::shared_ptr<model>;

    auto erase(std::string_view name) -> void;

    [[nodiscard]] auto get_identity_pool() -> model_identity_pool& {
        return identity_pool_;
    }

    [[nodiscard]] auto get_page_pool() -> page_pool& {
        return page_pool_;
    }

private:
    model_identity_pool identity_pool_;
    page_pool page_pool_;
    string_map<std::shared_ptr<model>> models_;
};

}  // namespace vw::asset
