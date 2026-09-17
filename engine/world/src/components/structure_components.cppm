export module vw.world:components.structure;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

enum class structure_size : uint8 { unspecified, small, medium, large, extra_large };

struct structure_component final {
    [[nodiscard]] auto get_type() const -> const std::string& {
        return type_;
    }

    [[nodiscard]] auto get_races() const -> std::span<const std::string> {
        return races_;
    }

    [[nodiscard]] auto get_tier() const -> uint8 {
        return tier_;
    }

    [[nodiscard]] auto get_size() const -> structure_size {
        return size_;
    }

private:
    friend class structure_system;

    std::string type_;
    std::vector<std::string> races_;
    uint8 tier_          = 0;
    structure_size size_ = structure_size::unspecified;
};

struct furniture_point_component final {
    [[nodiscard]] auto get_category() const -> const std::string& {
        return category_;
    }

private:
    friend class structure_system;

    std::string category_;
};

struct connection_point_component final {
    [[nodiscard]] auto get_profile() const -> const std::string& {
        return profile_;
    }

private:
    friend class structure_system;

    std::string profile_;
};

}  // namespace vw::ecs
