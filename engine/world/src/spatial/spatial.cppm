export module vw.world:spatial;

import std;

import vw.core;
import vw.ecs;

export namespace vw::ecs {

using spatial_layer_mask = uint16;

namespace spatial_layer {

inline constexpr spatial_layer_mask none       = 0;
inline constexpr spatial_layer_mask terrain    = 1 << 0;
inline constexpr spatial_layer_mask character  = 1 << 1;
inline constexpr spatial_layer_mask prop       = 1 << 2;
inline constexpr spatial_layer_mask trigger    = 1 << 3;
inline constexpr spatial_layer_mask projectile = 1 << 4;
inline constexpr spatial_layer_mask all        = 0xFFFF;

}  // namespace spatial_layer

class dynamic_aabb_tree {
public:
    dynamic_aabb_tree();

    auto insert(entity e, const spatial::aabb& bounds,
                spatial_layer_mask layer = spatial_layer::all) -> void;
    auto remove(entity e) -> void;
    auto update(entity e, const spatial::aabb& new_bounds,
                spatial_layer_mask layer = spatial_layer::all) -> void;

    auto query_all(const spatial::frustum& f, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all(const spatial::ray& r, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all(const spatial::aabb& bounds, std::vector<entity>& result_out,
                   spatial_layer_mask layer_mask = spatial_layer::all) const -> void;

    auto query_all_any(std::span<const spatial::frustum> frustums,
                       std::vector<entity>& result_out) const -> void;

    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;

    auto clear() -> void;

private:
    static constexpr uint32 invalid_node_index = std::numeric_limits<uint32>::max();

    struct node {
        spatial::aabb bounds;
        entity entity_id         = invalid_entity;
        spatial_layer_mask layer = spatial_layer::none;
        uint32 parent            = invalid_node_index;
        uint32 left              = invalid_node_index;
        uint32 right             = invalid_node_index;
        int32 height             = 0;
        bool is_leaf             = false;
    };

    struct sibling_candidate {
        uint32 index;
        float32 inherited_cost;
    };

    [[nodiscard]] auto allocate_node() -> uint32;
    auto free_node(uint32 index) -> void;

    [[nodiscard]] auto find_best_sibling(uint32 new_node_index) const -> uint32;
    auto insert_leaf(uint32 leaf_index) -> void;
    auto remove_leaf(uint32 leaf_index) -> void;
    auto refit(uint32 index) -> void;
    auto rotate(uint32 index) -> void;

    std::vector<node> nodes_;
    std::vector<uint32> free_nodes_;
    uint32 root_index_ = invalid_node_index;
    std::unordered_map<entity, uint32> entity_to_node_;
    mutable std::vector<uint32> query_stack_;
    mutable std::vector<sibling_candidate> sibling_stack_;
};

}  // namespace vw::ecs
