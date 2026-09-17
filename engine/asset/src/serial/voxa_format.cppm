export module vw.asset:serial.voxa;

import std;

import vw.core;
import :model;
import :anim;
import :serial.vox;
import :serial.text;

export namespace vw::asset {

class voxa_serializer final {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    explicit voxa_serializer(const animation_clip& clip);

    auto serialize(const std::filesystem::path& filepath) -> std::expected<void, error_type>;

private:
    auto write_header_(std::ofstream& file) -> void;
    auto write_track_(std::ofstream& file, const animation_track& track) -> void;
    auto write_channel_(std::ofstream& file, const animation_channel_variant& channel) -> void;
    auto write_keyframes_vec3f_(std::ofstream& file, const animation_channel<vec3f>& ch) -> void;
    auto write_keyframes_quat_(std::ofstream& file, const animation_channel<quat>& ch) -> void;

    const animation_clip* clip_;
};

class voxa_deserializer final {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    auto deserialize(const std::filesystem::path& filepath)
        -> std::expected<std::shared_ptr<animation_clip>, error_type>;

    auto deserialize(std::istream& input)
        -> std::expected<std::shared_ptr<animation_clip>, error_type>;

private:
    auto process_comment_(std::istringstream& iss) -> void;
    auto process_clip_(std::istringstream& iss) -> void;
    auto process_rig_(std::istringstream& iss) -> void;
    auto process_track_(std::istringstream& iss) -> void;
    auto process_channel_(std::istringstream& iss) -> void;
    auto process_keyframe_(std::istringstream& iss) -> void;
    auto finalize_channel_() -> void;
    auto finalize_track_() -> void;

    std::shared_ptr<animation_clip> clip_;
    std::optional<error_type> error_;

    std::string rig_;

    std::unique_ptr<animation_track> current_track_;
    animation_property current_property_ = animation_property::position;
    bool has_current_channel_            = false;
    bool current_channel_is_quat_        = false;

    std::vector<keyframe<vec3f>> vec3f_keyframes_;
    std::vector<keyframe<quat>> quat_keyframes_;
};

}  // namespace vw::asset
