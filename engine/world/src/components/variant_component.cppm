export module vw.world:components.variant;

import std;

import vw.core;
import vw.asset;
import vw.ecs;

export namespace vw::ecs {

class variant_system;

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

    [[nodiscard]] auto required_targets() const -> const std::vector<std::string>& {
        return required_targets_;
    }

    [[nodiscard]] auto required_sockets() const -> const std::vector<std::string>& {
        return required_sockets_;
    }

    [[nodiscard]] auto get_content() const -> const std::vector<entity>& {
        return content_;
    }

private:
    friend class variant_system;

    std::string name_;
    std::vector<asset::asset_ref> candidates_;
    std::size_t selected_ = 0;

    std::vector<std::string> required_targets_;
    std::vector<std::string> required_sockets_;
    std::vector<entity> content_;
};

struct slot_content_component final {
    [[nodiscard]] auto get_owner() const -> entity {
        return owner_;
    }

private:
    friend class variant_system;

    entity owner_;
};

}  // namespace vw::ecs
