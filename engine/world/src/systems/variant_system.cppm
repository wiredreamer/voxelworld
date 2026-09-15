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

    // Кандидат-поддерево (`.vox`) ставится не подменой объёма, а инстанцированием
    // — это умеет только тот, у кого есть разборщик. Слот про такого кандидата
    // знает, система его не ставит.
    unsupported_kind,
};

class variant_system {
public:
    static constexpr std::string_view system_name = "variant";

    explicit variant_system(world& w);

    class variant_modifier {
    public:
        explicit variant_modifier(variant_slot_component* component);

        auto set_name(std::string name) const -> void;
        auto add_candidate(asset::asset_ref ref) const -> void;
        auto set_candidates(std::vector<asset::asset_ref> candidates) const -> void;
        auto set_required_targets(std::vector<std::string> targets) const -> void;
        auto set_required_sockets(std::vector<std::string> sockets) const -> void;

        // Только поле: объём при этом не меняется. Кто его меняет — apply, и он
        // умеет отказать.
        auto select(std::size_t index) const -> void;

    private:
        variant_slot_component* component_;
    };

    auto modify(entity ent) -> variant_modifier;

    // Поставить кандидата на узел и запомнить выбор. Объём подменяется прямо
    // здесь: точка вращения живёт в самом объёме, поэтому узел не двигается, а
    // вместе с ним остаются на месте и дети, и сокеты.
    auto apply(entity ent, asset::model_library& library, std::size_t index)
        -> std::expected<void, variant_error>;

    auto update(float32 dt) -> void;

private:
    world* world_;
};

}  // namespace vw::ecs
