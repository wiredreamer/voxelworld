export module vw.world:serial.writer;

import std;

import vw.core;
import vw.ecs;
import :anim;
import :model;
import :serial.vox;

export namespace vw::ecs {

// 2.0 отличается от 1.0 записью вокселя: вместо числа, которое было то цветом,
// то индексом блока, стоит идентификатор блока «категория:номер».
inline constexpr std::string_view vox_file_version = "2.0";

// База для писателей формата .vox.
class vox_writer {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    virtual ~vox_writer() = default;

    virtual auto write(const std::filesystem::path& filepath,
                       const asset::vox_prefab_data& prefab) -> std::expected<void, error_type> = 0;
};

// Писатель текстового варианта .vox.
class vox_writer_plain final : public vox_writer {
public:
    auto write(const std::filesystem::path& filepath, const asset::vox_prefab_data& prefab)
        -> std::expected<void, error_type> override;

private:
    auto write_header_(std::ofstream& file, const asset::vox_prefab_data& prefab) -> void;
    auto write_entity_(std::ofstream& file, const asset::vox_entity_data& ent) -> void;
    auto write_model_(std::ofstream& file, const asset::vox_model_data& mdl) -> void;
};

}  // namespace vw::ecs
