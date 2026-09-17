export module vw.asset:serial.ref;

import std;

import vw.core;

export namespace vw::asset {

namespace dirs {
inline constexpr std::string_view prefabs    = "prefabs";
inline constexpr std::string_view models     = "models";
inline constexpr std::string_view animations = "animations";
inline constexpr std::string_view fsm        = "fsm";
}  // namespace dirs

class asset_ref final {
public:
    asset_ref() = default;
    explicit asset_ref(std::string_view path);

    [[nodiscard]] auto empty() const -> bool {
        return path_.empty();
    }

    [[nodiscard]] auto str() const -> const std::string& {
        return path_;
    }

    [[nodiscard]] auto extension() const -> std::string_view;
    [[nodiscard]] auto stem() const -> std::string_view;

    [[nodiscard]] auto operator==(const asset_ref& other) const -> bool = default;

private:
    std::string path_;
};

[[nodiscard]] auto default_model_ref(const asset_ref& prefab, std::string_view entity_name)
    -> asset_ref;

}  // namespace vw::asset

export template <>
struct std::hash<vw::asset::asset_ref> {
    auto operator()(const vw::asset::asset_ref& ref) const noexcept -> std::size_t {
        return std::hash<std::string>{}(ref.str());
    }
};
