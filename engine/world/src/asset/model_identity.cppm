export module vw.world:model.identity;

import std;

import vw.core;

export namespace vw::asset {

struct model_identity {
    static constexpr uint32 invalid_index = std::numeric_limits<uint32>::max();

    uint32 index      = invalid_index;
    uint32 generation = 0;

    [[nodiscard]] auto operator==(const model_identity& other) const -> bool {
        return index == other.index && generation == other.generation;
    }

    [[nodiscard]] auto operator!=(const model_identity& other) const -> bool {
        return !(*this == other);
    }

    [[nodiscard]] auto is_valid() const -> bool {
        return index != invalid_index;
    }
};

inline constexpr auto invalid_model_identity = model_identity{};

// Выдаёт дескрипторы моделей с меткой поколения. Общий для потоков загрузчика и
// главного потока — отсюда и замок.
class model_identity_pool final {
public:
    static constexpr std::size_t default_capacity = 1024;

    explicit model_identity_pool(std::size_t capacity = default_capacity);

    [[nodiscard]] auto create() -> model_identity;
    [[nodiscard]] auto next_generation(model_identity id) -> model_identity;
    [[nodiscard]] auto has(model_identity id) const -> bool;

    auto destroy(model_identity id) -> void;

private:
    [[nodiscard]] auto has_unlocked_(model_identity id) const -> bool {
        return id.index != model_identity::invalid_index && id.index < generations_.size() &&
               generations_[id.index] == id.generation;
    }

    mutable std::mutex mutex_;
    std::vector<uint32> generations_;
    std::vector<uint32> free_indices_;
};

// Слабовый аллокатор разреженных воксельных страниц: модели держат индексы, а
// сами страницы лежат в блоках, которые никогда не переезжают.
class page_pool final {
public:
    using page_type                    = std::array<block_index, 512>;
    static constexpr uint32 block_size = 4096;
    static constexpr uint32 max_blocks = 256;

    page_pool();

    [[nodiscard]] auto alloc() -> uint32;
    auto free(uint32 index) -> void;

    [[nodiscard]] auto alloc_batch(uint32 count) -> std::vector<uint32>;
    auto free_batch(std::span<const uint32> indices) -> void;

    [[nodiscard]] auto get(uint32 index) -> page_type& {
        return (*blocks_[index / block_size])[index % block_size];
    }

    [[nodiscard]] auto get(uint32 index) const -> const page_type& {
        return (*blocks_[index / block_size])[index % block_size];
    }

    [[nodiscard]] auto allocated_count() const -> uint32;
    [[nodiscard]] auto free_count() const -> uint32;

private:
    auto ensure_capacity_(uint32 index) -> void;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<std::array<page_type, block_size>>> blocks_;
    std::vector<uint32> free_indices_;
    uint32 next_index_ = 0;
};

enum class page_mode : uint8 { empty = 0, uniform = 1, sparse = 2 };

// Что говорит о целом объёме одна лишь таблица страниц. Важны оба крайних случая:
// сплошная порода и открытый воздух — это две вещи, из которых может состоять
// чанк, не имеющий собственных граней.
enum class model_fill : uint8 { mixed = 0, air = 1, solid = 2 };

// Два бита режима и тридцать полезных, общие на однородную и разреженную запись.
// Делить их незачем: запись бывает либо однородной, либо разреженной, и второе
// поле в каждом из случаев мертво. Однородной хватает восьми бит номера в
// наборе, разреженной — двадцати под индекс страницы в пуле.
struct page_entry {
    uint32 data = 0;

    [[nodiscard]] auto mode() const -> page_mode {
        return static_cast<page_mode>(data & 0x3U);
    }

    [[nodiscard]] auto fill_index() const -> block_index {
        return block_index{static_cast<uint8>((data >> 2) & 0xFFU)};
    }

    [[nodiscard]] auto pool_index() const -> uint32 {
        return data >> 2;
    }

    [[nodiscard]] static auto make_empty() -> page_entry {
        return {0U};
    }

    [[nodiscard]] static auto make_uniform(block_index index) -> page_entry {
        return {1U | (static_cast<uint32>(index.value) << 2)};
    }

    [[nodiscard]] static auto make_sparse(uint32 index) -> page_entry {
        return {2U | (index << 2)};
    }
};

}  // namespace vw::asset
export template <>
struct std::hash<vw::asset::model_identity> {
    auto operator()(const vw::asset::model_identity& id) const noexcept -> std::size_t {
        std::size_t x = (std::size_t{id.generation} << 32) | std::size_t{id.index};

        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        x = x ^ (x >> 31);

        return x;
    }
};
