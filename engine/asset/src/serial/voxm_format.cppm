export module vw.asset:serial.voxm;

import std;

import vw.core;
import :model;
import :serial.version;

export namespace vw::asset {

inline constexpr std::string_view voxm_file_version = "5.0";

enum class voxm_encoding : uint8 { rle };

class voxm_serializer final {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    explicit voxm_serializer(const model& model);

    auto serialize(const std::filesystem::path& filepath) -> std::expected<void, error_type>;
    auto serialize(std::ostream& output) -> void;

private:
    const model* model_;
};

class voxm_deserializer final {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    voxm_deserializer(model_registry& registry, const voxel_registry& voxel_types);

    auto deserialize(const std::filesystem::path& filepath)
        -> std::expected<std::shared_ptr<model>, error_type>;

    auto deserialize(std::istream& input) -> std::expected<std::shared_ptr<model>, error_type>;

private:
    auto process_comment_(std::istringstream& iss) -> void;
    auto process_encoding_(std::istringstream& iss) -> void;
    auto process_size_(std::istringstream& iss) -> void;
    auto process_pivot_(std::istringstream& iss) -> void;
    auto process_run_(std::istringstream& iss) -> void;

    [[nodiscard]] auto ensure_model_() -> bool;

    model_registry* registry_;
    const voxel_registry* voxel_types_;

    std::shared_ptr<model> model_;
    std::optional<error_type> error_;

    std::unordered_set<uint8> unknown_voxels_;

    vec3i size_{};
    vec3f pivot_{};
    bool has_size_     = false;
};

}  // namespace vw::asset
