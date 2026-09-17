export module vw.ecs:entity;

import std;

import vw.core;

export namespace vw::ecs {

struct entity final {
    static constexpr uint32 invalid_index = std::numeric_limits<uint32>::max();

    uint32 index      = invalid_index;
    uint32 generation = 0;

    [[nodiscard]] auto operator==(const entity& rhs) const -> bool {
        return index == rhs.index && generation == rhs.generation;
    }

    [[nodiscard]] auto operator!=(const entity& rhs) const -> bool {
        return !(*this == rhs);
    }

    [[nodiscard]] auto operator<(const entity& rhs) const -> bool {
        return index != rhs.index ? index < rhs.index : generation < rhs.generation;
    }

    [[nodiscard]] auto is_valid() const -> bool {
        return index != invalid_index;
    }
};

inline constexpr entity invalid_entity = entity{};

class entity_pool final {
public:
    static constexpr uint32 default_capacity = 1024;

    explicit entity_pool(uint32 capacity = default_capacity);

    [[nodiscard]] auto create() -> entity;
    [[nodiscard]] auto batch_create(uint32 count) -> std::vector<entity>;
    [[nodiscard]] auto has(entity e) const -> bool;

    auto destroy(entity e) -> void;
    auto batch_destroy(const std::vector<entity>& entities) -> void;

    [[nodiscard]] auto alive_entities() const -> std::vector<entity>;

private:
    std::vector<uint32> generations_;
    std::vector<uint32> free_indices_;
};

}  // namespace vw::ecs

export template <>
struct std::hash<vw::ecs::entity> {
    auto operator()(const vw::ecs::entity& ent) const noexcept -> std::size_t {
        return std::hash<std::size_t>()(ent.index) ^
            (std::hash<std::size_t>()(ent.generation) << 1);
    }
};
