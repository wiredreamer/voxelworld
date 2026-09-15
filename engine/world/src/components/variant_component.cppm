export module vw.world:components.variant;

import std;

import vw.core;
import vw.asset;
import vw.ecs;

export namespace vw::ecs {

class variant_system;

// Слот варианта: узел объявляет, чем его можно заменить и чем заменён сейчас.
// Кандидат — ссылка без поля типа: `.voxm` подменяет объём узла, `.vox`
// подставляет поддерево, и различает их расширение. Простой случай не платит за
// сложный — голова другого размера остаётся одним файлом объёма, а не копией
// всего персонажа.
struct variant_slot_component final {
    [[nodiscard]] auto get_name() const -> const std::string& {
        return name_;
    }

    [[nodiscard]] auto get_candidates() const -> const std::vector<asset::asset_ref>& {
        return candidates_;
    }

    [[nodiscard]] auto get_selected() const -> std::size_t {
        return selected_;
    }

    [[nodiscard]] auto selected_ref() const -> const asset::asset_ref& {
        static const asset::asset_ref none;
        return selected_ < candidates_.size() ? candidates_[selected_] : none;
    }

    // Контракт: что кандидат обязан принести с собой, чтобы встать в слот.
    // Пустой контракт — не «ничего не проверяем», а «узел ничем наружу не
    // обязан»: объёму нечего закрывать, целями и сокетами владеет сам узел.
    [[nodiscard]] auto required_targets() const -> const std::vector<std::string>& {
        return required_targets_;
    }

    [[nodiscard]] auto required_sockets() const -> const std::vector<std::string>& {
        return required_sockets_;
    }

private:
    friend class variant_system;

    std::string name_;
    std::vector<asset::asset_ref> candidates_;
    std::size_t selected_ = 0;

    std::vector<std::string> required_targets_;
    std::vector<std::string> required_sockets_;
};

}  // namespace vw::ecs
