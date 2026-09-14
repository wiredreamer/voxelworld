export module vw.asset:serial.vox;

import std;

import vw.core;
import :model;
import :anim;
import :serial.ref;
import :serial.version;

export namespace vw::asset {

struct vox_socket_data {
    std::string name;
    vec3f position;
    vec3f rotation;
    vec3f scale;
};

struct vox_entity_data {
    std::string name;
    std::string parent_name;

    vec3f position;
    vec3f rotation;
    vec3f scale{1.0F, 1.0F, 1.0F};
    bool has_transform = false;

    asset_ref model;
    std::optional<std::string> animation_target_name;
    std::vector<vox_socket_data> sockets;
    bool has_sockets = false;
};

struct vox_prefab_data {
    std::string root_name;
    std::vector<vox_entity_data> entities;
};

// 3.0 вынесло воксели из дерева: узел ссылается на .voxm, а точка вращения
// уехала в сам объём. Чтение 2.0 удалено вместе с переводом ассетов — старый
// файл теперь отвергается по версии, а не читается наполовину.
inline constexpr std::string_view vox_file_version = "3.0";

// База для разборщиков формата .vox.
class vox_parser {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    virtual ~vox_parser() = default;

    virtual auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> = 0;
};

// Разборщик текстового варианта .vox.
class vox_parser_plain final : public vox_parser {
public:
    auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> override;

    // Разбор в отрыве от файловой системы: тем же путём идёт и файл, и буфер из
    // фаззера, и строка из теста. Ошибку открытия эта форма вернуть не может.
    auto parse(std::istream& input) -> std::expected<vox_prefab_data, error_type>;

private:
    auto process_comment_(std::istringstream& iss) -> void;
    auto process_root_(std::istringstream& iss) -> void;
    auto process_entity_(std::istringstream& iss) -> void;
    auto process_parent_(std::istringstream& iss) -> void;
    auto process_transform_(std::istringstream& iss) -> void;
    auto process_target_(std::istringstream& iss) -> void;
    auto process_sockets_() -> void;
    auto process_socket_(std::istringstream& iss) -> void;
    auto process_model_(std::istringstream& iss) -> void;

    vox_prefab_data prefab_;
    vox_entity_data* current_entity_ = nullptr;
    std::optional<error_type> error_;
};

inline constexpr std::string_view voxa_file_version = "1.0";

}  // namespace vw::asset
