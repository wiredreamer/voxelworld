export module vw.world:serial.vox;

import std;

import vw.core;
import :anim;
import :model;

export namespace vw::asset {

struct vox_socket_data {
    std::string name;
    vec3f position;
    vec3f rotation;
    vec3f scale;
};

struct vox_model_data {
    vec3i size;

    // Набор блоков модели. Выводится из первого непустого вокселя, а остальные
    // обязаны ему соответствовать: модель несёт ровно один набор, потому что
    // страница хранит номер в наборе, а не идентификатор целиком.
    block_category category;

    std::vector<std::pair<vec3i, voxel>> voxels;
};

struct vox_entity_data {
    std::string name;
    std::string parent_name;

    vec3f position;
    vec3f rotation;
    vec3f scale{1.0F, 1.0F, 1.0F};
    vec3f origin;
    bool has_transform = false;

    std::optional<vox_model_data> model;
    std::optional<std::string> animation_target_name;
    std::vector<vox_socket_data> sockets;
    bool has_sockets = false;
};

struct vox_prefab_data {
    std::string root_name;
    std::vector<vox_entity_data> entities;
};

// База для разборщиков формата .vox.
class vox_parser {
public:
    enum class error_type : uint8 { file_open_failed, parse_error };

    virtual ~vox_parser() = default;

    virtual auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> = 0;
};

// Разборщик текстового варианта .vox.
class vox_parser_plain final : public vox_parser {
public:
    explicit vox_parser_plain(const block_registry& block_registry);

    auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> override;

    // Разбор в отрыве от файловой системы: тем же путём идёт и файл, и буфер из
    // фаззера, и строка из теста. Ошибку открытия эта форма вернуть не может.
    auto parse(std::istream& input) -> std::expected<vox_prefab_data, error_type>;

private:
    auto process_root_(std::istringstream& iss) -> void;
    auto process_entity_(std::istringstream& iss) -> void;
    auto process_parent_(std::istringstream& iss) -> void;
    auto process_transform_(std::istringstream& iss) -> void;
    auto process_target_(std::istringstream& iss) -> void;
    auto process_sockets_() -> void;
    auto process_socket_(std::istringstream& iss) -> void;
    auto process_model_(std::istringstream& iss) -> void;
    auto process_voxel_(std::istringstream& iss) -> void;

    [[nodiscard]] auto parse_block_id_(std::string_view token) -> std::optional<block_id>;

    const block_registry* block_registry_;
    vox_prefab_data prefab_;
    vox_entity_data* current_entity_ = nullptr;
    std::optional<error_type> error_;

    // Блок вне каталога разбор не рвёт — он нарисуется заглушкой, и это видно.
    // Но сказать о нём надо один раз, а не по разу на воксель.
    std::unordered_set<uint16> unknown_blocks_;
};

inline constexpr std::string_view voxa_file_version = "1.0";

}  // namespace vw::asset
