export module vw.world:components.animation;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :spatial;

export namespace vw::ecs {

class animation_fsm_system;
class animation_system;
class character_controller_system;
class hierarchy_system;
class light_system;
class model_system;
class physics_system;
class socket_system;
class spatial_system;
class transform_system;
class world_grid_system;

// Риг — имя контракта анимации, а не список целей: цели выводятся из узлов с
// animation_target_component, и сверка по ним остаётся настоящей проверкой.
// Имя лежит на корне префаба и отвечает на вопрос, который по узлам не задашь:
// «этот клип вообще про это существо?».
// Автоматы префаба, по одному на слой. Живут на корне рядом с ригом: в файле
// это шапка, а не тег узла, и узла у них нет. Порядок — это номера слоёв.
//
// Здесь ссылки, а не сами автоматы: редактору хватает ссылок, а игре автомат
// собирает хранилище, у которого есть клипы.
struct animation_machines_component final {
    [[nodiscard]] auto get_sources() const -> std::span<const asset::asset_ref> {
        return sources_;
    }

private:
    friend class animation_fsm_system;

    std::vector<asset::asset_ref> sources_;
};

struct rig_component final {
    [[nodiscard]] auto get_name() const -> const std::string& {
        return rig_name_;
    }

private:
    friend class animation_system;

    std::string rig_name_;
};

struct animation_target_component final {
    [[nodiscard]] auto get_name() const -> const std::string& {
        return target_name_;
    }

    [[nodiscard]] auto get_rest_transform() const -> const transform& {
        return rest_transform_;
    }

private:
    friend class animation_system;

    std::string target_name_;
    transform rest_transform_;
};

struct animation_player_component final {
    [[nodiscard]] auto layer_count() const -> std::size_t {
        return layers_.size();
    }

    [[nodiscard]] auto has_layer(std::size_t index) const -> bool {
        return index < layers_.size();
    }

    [[nodiscard]] auto get_layer(std::size_t index) const -> const asset::animation_layer& {
        return layers_[index];
    }

    [[nodiscard]] auto is_any_playing() const -> bool {
        return std::ranges::any_of(layers_, [](const auto& layer) { return layer.is_active(); });
    }

private:
    friend class animation_system;

    std::vector<asset::animation_layer> layers_;
};

struct animation_fsm_component final {
    [[nodiscard]] auto machine_count() const -> std::size_t {
        return machines_.size();
    }

    [[nodiscard]] auto get_machine(std::size_t index) -> asset::animation_fsm& {
        return machines_[index];
    }

    [[nodiscard]] auto get_machine(std::size_t index) const -> const asset::animation_fsm& {
        return machines_[index];
    }

    // Доска параметров одна на все слои сущности: условия в них общие — скорость
    // и опора не бывают отдельно для локомоции и отдельно для атаки.
    [[nodiscard]] auto get_board() const -> const asset::fsm_blackboard& {
        return board_;
    }

private:
    friend class animation_fsm_system;

    using trigger_set = asset::animation_fsm::trigger_set;

    std::vector<asset::animation_fsm> machines_;
    trigger_set triggers_;
    asset::fsm_blackboard board_;
};

}  // namespace vw::ecs
