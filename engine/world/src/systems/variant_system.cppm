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

    contract_unmet,
};

// Что кандидат обязан был принести и чего не принёс. Считается по данным файла,
// до всякой постановки в мир: отказ не должен оставлять после себя половину
// поддерева.
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
        // Выбор держится за ссылку, а не за номер: номер — это позиция в
        // списке, и удаление соседа сверху молча перевело бы выбор на другого
        // кандидата. Отвечает, пришлось ли выбор переставить: если пришлось, в
        // сцене стоит кандидат, которого в списке больше нет.
        auto set_candidates(std::vector<asset::asset_ref> candidates) const -> bool;
        auto set_required_targets(std::vector<std::string> targets) const -> void;
        auto set_required_sockets(std::vector<std::string> sockets) const -> void;

        // Только поле: объём при этом не меняется. Кто его меняет — apply, и он
        // умеет отказать.
        auto select(std::size_t index) const -> void;

        // Состав поставленного поддерева: слот держит его, чтобы снять при
        // следующей подмене.
        auto set_content(std::vector<entity> content) const -> void;

    private:
        variant_slot_component* component_;
    };

    auto modify(entity ent) -> variant_modifier;

    // Пометить сущность как пришедшую по ссылке. Владелец — узел со слотом либо
    // ничто: превью сокета тоже содержимое по ссылке, но слота за ним нет.
    auto mark_content(entity content, entity owner) -> void;

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
