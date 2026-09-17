export module vw.asset:model.volume;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;
import :model.links;

export namespace vw::asset {

class model {
public:
    static constexpr int32 page_size   = 8;
    static constexpr int32 page_volume = page_size * page_size * page_size;
    using page_type                    = std::array<voxel_index, page_volume>;

    model(
        model_identity_pool& identity_pool,
        page_pool& pool,
        voxel_category category,
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
        return to_id(get_index(x, y, z));
    }

    [[nodiscard]] auto get_index(
        int32 x, int32 y, int32 z
    ) const -> voxel_index {
        const auto& entry = pages_[page_index(x / page_size, y / page_size, z / page_size)];

        switch (entry.mode()) {
            case page_mode::empty:
                return voxel_index{};
            case page_mode::uniform:
                return entry.fill_index();
            case page_mode::sparse:
                return pool_ptr_->get(
                    entry.pool_index()
                )[local_index(x % page_size, y % page_size, z % page_size)];
        }
        return voxel_index{};
    }

    [[nodiscard]] auto to_id(
        voxel_index index
    ) const -> voxel {
        return index.is_empty() ? voxels::air : voxel{category_, index.value};
    }

    [[nodiscard]] auto get_voxel(
        vec3i pos
    ) const -> voxel {
        return get_voxel(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto get_index(
        vec3i pos
    ) const -> voxel_index {
        return get_index(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto category() const -> voxel_category {
        return category_;
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
            case page_mode::sparse:
                return pool_ptr_
                    ->get(
                        entry.pool_index()
                    )[local_index(x % page_size, y % page_size, z % page_size)]
                    .is_empty();
        }
        return true;
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

    [[nodiscard]] auto get_page_fill_index(
        int32 px, int32 py, int32 pz
    ) const -> voxel_index {
        return pages_[page_index(px, py, pz)].fill_index();
    }

    [[nodiscard]] auto get_page(
        int32 px, int32 py, int32 pz
    ) const -> const page_type* {
        const auto& entry = pages_[page_index(px, py, pz)];
        return entry.mode() == page_mode::sparse ? &pool_ptr_->get(entry.pool_index()) : nullptr;
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

    [[nodiscard]] auto to_index_(voxel v) const -> voxel_index;

    auto set_voxel_raw_(int32 x, int32 y, int32 z, voxel v) -> void;
    auto fill_page_raw_(int32 px, int32 py, int32 pz, voxel v) -> void;

    [[nodiscard]] auto page_index(
        int32 px, int32 py, int32 pz
    ) const -> int32 {
        return px + (py * pages_x_) + (pz * pages_x_ * pages_y_);
    }

    [[nodiscard]] static auto local_index(
        int32 lx, int32 ly, int32 lz
    ) -> int32 {
        return lx + (ly * page_size) + (lz * page_size * page_size);
    }

    auto alloc_sparse_page() -> uint32;
    auto free_sparse_page(uint32 index) -> void;
    auto promote_to_sparse(int32 px, int32 py, int32 pz) -> page_type&;

    auto increment_generation_() -> void;

    model_identity_pool* identity_pool_;
    page_pool* pool_ptr_;
    voxel_category category_;
    int32 width_{0}, height_{0}, depth_{0};
    int32 world_units_per_voxel_{1};
    vec3f pivot_{0.0F, 0.0F, 0.0F};
    int32 pages_x_{0}, pages_y_{0}, pages_z_{0};
    std::vector<page_entry> pages_;
    std::vector<uint32> owned_pages_;
    model_identity identity_;
    mutable model_fill fill_ = model_fill::mixed;
    mutable bool fill_known_ = false;
};

class model_registry {
public:
    [[nodiscard]] auto has(std::string_view name) const -> bool;
    [[nodiscard]] auto get(std::string_view name) const -> std::shared_ptr<model>;

    [[nodiscard]] auto create(
        std::string_view name, voxel_category category, int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create(std::string_view name, voxel_category category, vec3i size)
        -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(
        voxel_category category, int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(voxel_category category, vec3i size)
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
    std::unordered_map<std::string, std::shared_ptr<model>> models_;
};

}  // namespace vw::asset
