export module vw.asset:anim.clip;

import std;

import vw.core;
import :anim.keyframe;
import :anim.channel;

export namespace vw::asset {

class animation_clip final {
public:
    explicit animation_clip(std::string name);

    auto add_track(animation_track track) -> void;

    [[nodiscard]] auto get_track(std::string_view target_name) const -> const animation_track*;
    [[nodiscard]] auto get_track_mut(std::string_view target_name) -> animation_track*;
    [[nodiscard]] auto has_track(std::string_view target_name) const -> bool;

    [[nodiscard]] auto get_tracks() const -> const std::vector<animation_track>& {
        return tracks_;
    }

    auto remove_track(std::string_view target_name) -> void;

    [[nodiscard]] auto get_duration() const -> float32;

    [[nodiscard]] auto get_name() const -> const std::string& {
        return name_;
    }

    auto set_name(std::string name) -> void;

    [[nodiscard]] auto get_rig() const -> const std::string& {
        return rig_;
    }

    auto set_rig(std::string rig) -> void;

    [[nodiscard]] auto get_target_names() const -> std::unordered_set<std::string>;

private:
    std::string name_;
    std::string rig_;
    std::vector<animation_track> tracks_;
};

struct string_hash {
    using is_transparent = void;

    [[nodiscard]] auto operator()(std::string_view sv) const noexcept -> std::size_t {
        return std::hash<std::string_view>{}(sv);
    }
};

class animation_clip_registry final {
public:
    using map_type = std::
        unordered_map<std::string, std::shared_ptr<animation_clip>, string_hash, std::equal_to<>>;

    [[nodiscard]] auto create(std::string_view name) -> std::shared_ptr<animation_clip>;
    auto add(std::string_view name, std::shared_ptr<animation_clip> clip) -> void;
    [[nodiscard]] auto get(std::string_view name) const -> std::shared_ptr<animation_clip>;
    [[nodiscard]] auto has(std::string_view name) const -> bool;
    auto remove(std::string_view name) -> void;

    [[nodiscard]] auto all() const -> const map_type& {
        return clips_;
    }

    [[nodiscard]] auto count() const -> std::size_t {
        return clips_.size();
    }

    auto clear() -> void;

private:
    map_type clips_;
};

}  // namespace vw::asset
