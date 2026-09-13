export module vw.asset:model.volume;

import std;

import vw.core;
import :model.identity;
import :model.occupancy;
import :model.links;

export namespace vw::asset {

// Страничный воксельный объём. Страницы бывают пустыми, однородными и
// разреженными, поэтому почти сплошная или почти пустая модель стоит одной записи
// на страницу вместо массива вокселей.
//
// Знает только про свои воксели. Всё, что добавляет им мир, — плоскости соседей
// по швам и запечённый свет — живёт в chunk_volume, потому что бывает только у
// чанков: над моделью в редакторе ни соседей, ни неба нет.
//
// Набор блоков у модели ровно один и задаётся при создании. Поэтому страница
// хранит только номер в наборе — байт, а не два, — а полный идентификатор
// собирается при чтении. Смешение наборов в одном объёме этим и запрещено: то,
// что состоит из разных наборов, — это разные модели в одной мировой сетке, и у
// них разный масштаб вокселя чаще, чем одинаковый.
class model {
public:
    static constexpr int32 page_size   = 8;
    static constexpr int32 page_volume = page_size * page_size * page_size;
    using page_type                    = std::array<block_index, page_volume>;

    model(
        model_identity_pool& identity_pool,
        page_pool& pool,
        block_category category,
        int32 width,
        int32 height,
        int32 depth,
        int32 voxel_scale = 1
    );
    ~model();

    model(const model&)                    = delete;
    auto operator=(const model&) -> model& = delete;

    model(model&& other) noexcept;
    auto operator=(model&& other) noexcept -> model&;

    auto set_voxel(int32 x, int32 y, int32 z, const voxel& v) -> void;

    auto set_voxel(
        vec3i pos, const voxel& v
    ) -> void {
        set_voxel(pos.x, pos.y, pos.z, v);
    }

    [[nodiscard]] auto get_voxel(
        int32 x, int32 y, int32 z
    ) const -> voxel {
        return voxel{to_id(get_index(x, y, z))};
    }

    // Номер в наборе, без сборки идентификатора. Тем, кто идёт по вокселям одной
    // модели, набор известен и так, и собирать его на каждый воксель незачем.
    [[nodiscard]] auto get_index(
        int32 x, int32 y, int32 z
    ) const -> block_index {
        const auto& entry = pages_[page_index(x / page_size, y / page_size, z / page_size)];

        switch (entry.mode()) {
            case page_mode::empty:
                return block_index{};
            case page_mode::uniform:
                return entry.fill_index();
            case page_mode::sparse:
                return pool_ptr_->get(
                    entry.pool_index()
                )[local_index(x % page_size, y % page_size, z % page_size)];
        }
        return block_index{};
    }

    [[nodiscard]] auto to_id(
        block_index index
    ) const -> block_id {
        return index.is_empty() ? blocks::air : block_id{category_, index.value};
    }

    [[nodiscard]] auto get_voxel(
        vec3i pos
    ) const -> voxel {
        return get_voxel(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto get_index(
        vec3i pos
    ) const -> block_index {
        return get_index(pos.x, pos.y, pos.z);
    }

    [[nodiscard]] auto category() const -> block_category {
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

    [[nodiscard]] auto voxel_scale() const -> int32 {
        return voxel_scale_;
    }

    // Записанная страница остаётся разреженной, даже если все воксели в ней
    // оказались одним блоком, — поэтому сплошная порода стоит 512 байт на
    // страницу вместо нуля. Сворачивание таких обратно в однородные и делает
    // глубокий мир подъёмным; звать один раз, когда объём заполнен.
    auto compact_pages() -> uint32;

    // Собственная плоскость модели со стороны face_direction, прямо из таблицы
    // страниц: однородная страница — это восемь строк по восемь выставленных бит,
    // и вокселей она не касается. Ложь для всего, что не 64-куб: соседей, которым
    // нужна плоскость, имеют только чанки мира.
    [[nodiscard]] auto extract_face(int32 face_direction, face_occupancy& out) const -> bool;

    // Идёт по записям страниц, а не по вокселям: чанк сплошной породы — это 512
    // однородных записей, и ответ выходит из первых двух различающихся. Звать
    // после compact_pages: разреженная страница, где случайно один блок, читается
    // как смешанная.
    //
    // Кэшируется, потому что важный вызывающий — сосед, спрашивающий через шов, а
    // к тому моменту таблица страниц холодная: два килобайта промахов кэша ради
    // одного байта. Стоит прогреть на потоке, сгенерировавшем чанк.
    [[nodiscard]] auto scan_fill() const -> model_fill;

    // Заполняет битовый объём из таблицы страниц, а не воксель за вокселем: пустая
    // страница не даёт ничего, однородная даёт восемь выставленных бит на строку
    // разом. Ложь, если модель не 64-куб.
    [[nodiscard]] auto build_occupancy(chunk_occupancy& out) const -> bool;

    // Только строки вдоль X, не трогая zrows, и только на заданном диапазоне
    // страниц: x в [px0 * 8, px1 * 8) и z в [pz0 * 8, pz1 * 8). Строки вне
    // диапазона по z обнуляются, а не заполняются.
    //
    // Диапазон и делает небесный свет подъёмным. Построить все девять колонок
    // окрестности целиком стоит около четырнадцати миллисекунд, впятеро дороже
    // заливки, которую это кормит, и почти всё — цена касания каждого вокселя
    // восьми чанков. Но юбка — пятнадцать вокселей, то есть две страницы по
    // восемь: боковой сосед читается на две страничные колонки, угловой на две на
    // две, и вся окрестность выходит в 2,25 колонки работы вместо девяти.
    [[nodiscard]] auto build_x_rows(
        chunk_occupancy& out, int32 px0, int32 px1, int32 pz0, int32 pz1
    ) const -> bool;

    [[nodiscard]] auto build_x_rows(
        chunk_occupancy& out
    ) const -> bool {
        return build_x_rows(out, 0, pages_x_, 0, pages_z_);
    }

    // Поднять поколение, не трогая вокселей: меш есть функция и света тоже, а
    // ставит его chunk_volume. Правку вокселей объявляет model_writer, на выходе
    // из своего скоупа.
    auto invalidate() -> void;

    auto fill(const voxel& v) -> void;

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
    ) const -> block_index {
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
    // Правка идёт через model_writer: он пишет прямо в страницы, а поколение
    // поднимает один раз на весь скоуп.
    friend class model_writer;

    [[nodiscard]] auto to_index_(const voxel& v) const -> block_index;

    auto set_voxel_raw_(int32 x, int32 y, int32 z, const voxel& v) -> void;
    auto fill_page_raw_(int32 px, int32 py, int32 pz, const voxel& v) -> void;

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
    block_category category_;
    int32 width_{0}, height_{0}, depth_{0};
    int32 voxel_scale_{1};
    int32 pages_x_{0}, pages_y_{0}, pages_z_{0};
    std::vector<page_entry> pages_;
    std::vector<uint32> owned_pages_;
    model_identity identity_;
    mutable model_fill fill_ = model_fill::mixed;
    mutable bool fill_known_ = false;
};

// Владеет именованными моделями мира вместе с пулами, из которых берутся их
// страницы и идентичности.
class model_registry {
public:
    [[nodiscard]] auto has(std::string_view name) const -> bool;
    [[nodiscard]] auto get(std::string_view name) const -> std::shared_ptr<model>;

    [[nodiscard]] auto create(
        std::string_view name, block_category category, int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create(std::string_view name, block_category category, vec3i size)
        -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(
        block_category category, int32 width, int32 height, int32 depth
    ) -> std::shared_ptr<model>;
    [[nodiscard]] auto create_unnamed(block_category category, vec3i size)
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
