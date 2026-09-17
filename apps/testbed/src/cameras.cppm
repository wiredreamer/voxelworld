export module vw.testbed:cameras;

import std;

import vw.core;
import :camera;

export namespace vw::testbed {

inline constexpr float32 path_clearance = 400.0f;

class parked_rig final : public camera_rig {
public:
    using camera_rig::camera_rig;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "parked";
    }

    auto drive(const camera_hint& hint, float32 delta_time) -> void override;
};

class spin_rig final : public camera_rig {
public:
    using camera_rig::camera_rig;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "spin";
    }

    auto drive(const camera_hint& hint, float32 delta_time) -> void override;

private:
    uint64 frame_ = 0;
};

class walk_rig final : public camera_rig {
public:
    using camera_rig::camera_rig;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "walk";
    }

    auto drive(const camera_hint& hint, float32 delta_time) -> void override;

private:
    static constexpr float32 per_frame = 13.0f;

    uint64 frame_ = 0;
};

class orbit_rig final : public camera_rig {
public:
    using camera_rig::camera_rig;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "orbit";
    }

    auto drive(const camera_hint& hint, float32 delta_time) -> void override;

private:
    static constexpr float32 radius = 1500.0f;

    uint64 frame_ = 0;
};

class free_rig final : public camera_rig {
public:
    using camera_rig::camera_rig;

    [[nodiscard]] auto name() const -> std::string_view override {
        return "free";
    }

    [[nodiscard]] auto needs_ground() const -> bool override {
        return false;
    }

    auto drive(const camera_hint& hint, float32 delta_time) -> void override;
};

[[nodiscard]] auto find_camera(std::string_view name) -> std::optional<camera_factory>;

[[nodiscard]] auto camera_names() -> std::vector<std::string_view>;

}  // namespace vw::testbed
