export module vw.asset:anim.channel;

import std;

import vw.core;
import :anim.keyframe;

export namespace vw::asset {

template <typename T>
// Одно анимируемое свойство одной цели: отсортированный список ключевых кадров
// плюс интерполяция между соседями.
class animation_channel final {
public:
    enum class error_type : uint8 { empty };

    explicit animation_channel(animation_property property) : property_(property) {}

    auto add(const keyframe<T>& kf) -> void {
        keyframes_.push_back(kf);
        if (keyframes_.back().id_ == invalid_keyframe_id) {
            keyframes_.back().id_ = next_id_++;
        }
        std::sort(keyframes_.begin(), keyframes_.end());
    }

    auto replace(uint32 id, const keyframe<T>& new_kf) -> void {
        const auto it =
            std::ranges::find_if(keyframes_, [id](const auto& kf) { return kf.id_ == id; });
        if (it != keyframes_.end()) {
            const uint32 preserved_id = it->id_;
            *it                       = new_kf;
            it->id_                   = preserved_id;
            std::sort(keyframes_.begin(), keyframes_.end());
        }
    }

    auto remove(uint32 id) -> void {
        std::erase_if(keyframes_, [id](const auto& kf) { return kf.id_ == id; });
    }

    auto set_keyframes(std::vector<keyframe<T>> keyframes) -> void {
        keyframes_ = std::move(keyframes);
        next_id_   = 0;
        for (auto& kf : keyframes_) {
            kf.id_ = next_id_++;
        }
        std::sort(keyframes_.begin(), keyframes_.end());
    }

    auto clear() -> void {
        keyframes_.clear();
    }

    [[nodiscard]] auto evaluate(float32 time) const -> std::expected<T, error_type> {
        if (keyframes_.empty()) {
            return std::unexpected(error_type::empty);
        }

        if (keyframes_.size() == 1 || time <= keyframes_.front().time) {
            return keyframes_.front().value;
        }

        if (time >= keyframes_.back().time) {
            return keyframes_.back().value;
        }

        const auto it = std::ranges::lower_bound(
            keyframes_, time, {}, [](const keyframe<T>& kf) { return kf.time; });

        if (it == keyframes_.end()) {
            return keyframes_.back().value;
        }

        if (it == keyframes_.begin()) {
            return keyframes_.front().value;
        }

        const keyframe<T>& kf0 = *(it - 1);
        const keyframe<T>& kf1 = *it;

        const float32 duration = kf1.time - kf0.time;
        if (duration <= 0.0F) {
            return kf0.value;
        }

        const float32 t = (time - kf0.time) / duration;

        return math::interpolate(
            kf0.value, kf1.value, t, kf0.interp, kf0.tangent_in, kf0.tangent_out);
    }

    [[nodiscard]] auto get_duration() const -> std::expected<float32, error_type> {
        if (keyframes_.empty()) {
            return std::unexpected(error_type::empty);
        }
        return keyframes_.back().time;
    }

    [[nodiscard]] auto get_property() const -> animation_property {
        return property_;
    }

    [[nodiscard]] auto is_empty() const -> bool {
        return keyframes_.empty();
    }

    [[nodiscard]] auto keyframe_count() const -> std::size_t {
        return keyframes_.size();
    }

    [[nodiscard]] auto get_keyframes() const -> const std::vector<keyframe<T>>& {
        return keyframes_;
    }

    [[nodiscard]] auto get_keyframes_mut() -> std::vector<keyframe<T>>& {
        return keyframes_;
    }

private:
    animation_property property_;
    std::vector<keyframe<T>> keyframes_;
    uint32 next_id_ = 0;
};

using animation_channel_variant = std::variant<animation_channel<vec3f>, animation_channel<quat>>;

template <animation_property Prop>
using animation_channel_for = animation_channel<typename animation_property_traits<Prop>::type>;

template <animation_property Prop>
auto make_animation_channel() -> animation_channel_for<Prop> {
    return animation_channel_for<Prop>(Prop);
}

// Все каналы одной цели, запечённые в фиксированную частоту кадров при первом
// запросе и перезапекаемые при любом изменении канала.
class animation_track final {
public:
    enum class error_type : uint8 { empty };

    explicit animation_track(std::string target_name, float32 fps = 60.0F);

    template <animation_property Prop>
    auto add(animation_channel_for<Prop> channel) -> void {
        add_impl(animation_channel_variant(std::move(channel)));
    }

    auto remove_channel(animation_property prop) -> void;

    [[nodiscard]] auto get_transform(float32 time) const -> std::expected<transform, error_type>;
    [[nodiscard]] auto get_duration() const -> float32;
    [[nodiscard]] auto get_frame_time() const -> float32;

    [[nodiscard]] auto get_target_name() const -> const std::string& {
        return target_name_;
    }

    [[nodiscard]] auto get_channel(animation_property prop) const
        -> const animation_channel_variant*;
    [[nodiscard]] auto get_channel_mut(animation_property prop) -> animation_channel_variant*;
    [[nodiscard]] auto has_channel(animation_property prop) const -> bool;

    [[nodiscard]] auto get_channels() const -> const std::vector<animation_channel_variant>& {
        return channels_;
    }

    [[nodiscard]] auto get_fps() const -> float32 {
        return compiled_fps_;
    }

    auto mark_dirty() -> void {
        is_dirty_ = true;
    }

private:
    auto add_impl(animation_channel_variant channel) -> void;
    auto recompile_if_needed() const -> void;

    std::string target_name_;
    std::vector<animation_channel_variant> channels_;

    mutable bool is_dirty_ = true;
    mutable float32 compiled_fps_;
    mutable float32 frame_time_      = 0.0F;
    mutable float32 cached_duration_ = 0.0F;
    mutable std::vector<transform> compiled_transforms_;
};

}  // namespace vw::asset
