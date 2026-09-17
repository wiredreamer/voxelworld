export module vw.world:systems.variant;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;

export namespace vw::ecs {

class world;

enum class variant_error : uint8 {
    no_slot,
    out_of_range,
    load_failed,

    unsupported_kind,

    contract_unmet,
};

struct variant_report {
    std::vector<std::string> missing_targets;
    std::vector<std::string> missing_sockets;

    [[nodiscard]] auto ok() const -> bool {
        return missing_targets.empty() && missing_sockets.empty();
    }
};

[[nodiscard]] auto check_candidate(
    const variant_slot_component& slot, const asset::vox_prefab_data& candidate
) -> variant_report;

class variant_system {
public:
    static constexpr std::string_view system_name = "variant";

    explicit variant_system(world& w);

    class variant_modifier {
    public:
        explicit variant_modifier(variant_slot_component* component);

        auto set_name(std::string name) const -> void;
        auto add_candidate(asset::asset_ref ref) const -> void;
        auto set_candidates(std::vector<asset::asset_ref> candidates) const -> bool;
        auto set_required_targets(std::vector<std::string> targets) const -> void;
        auto set_required_sockets(std::vector<std::string> sockets) const -> void;

        auto select(std::size_t index) const -> void;

        auto set_content(std::vector<entity> content) const -> void;

    private:
        variant_slot_component* component_;
    };

    auto modify(entity ent) -> variant_modifier;

    auto mark_content(entity content, entity owner) -> void;

    auto apply(entity ent, asset::model_library& library, std::size_t index)
        -> std::expected<void, variant_error>;

    auto update(float32 dt) -> void;

private:
    world* world_;
};

}  // namespace vw::ecs
