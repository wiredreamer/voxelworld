export module vw.asset:anim.clip;

import std;

import vw.core;
import :anim.keyframe;
import :anim.channel;

export namespace vw::asset {

struct animation_event {
    float32 time = 0.0F;
    std::string name;
    std::string payload;

    [[nodiscard]] auto operator==(const animation_event& other) const -> bool = default;
};

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

    [[nodiscard]] auto get_events() const -> const std::vector<animation_event>& {
        return events_;
    }

    auto add_event(animation_event event) -> void;
    auto set_events(std::vector<animation_event> events) -> void;

private:
    std::string name_;
    std::string rig_;
    std::vector<animation_track> tracks_;
    std::vector<animation_event> events_;
};

[[nodiscard]] auto find_problems(const animation_clip& clip) -> std::vector<std::string>;

class animation_clip_registry final {
public:
    using map_type = string_map<std::shared_ptr<animation_clip>>;

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
