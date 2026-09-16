export module vw.asset:serial.voxm;

import std;

import vw.core;
import :model;
import :serial.version;

export namespace vw::asset {

// 1.0 — первый формат, в котором объём живёт отдельно от дерева префаба.
// 2.0 — номера набора для покраски стали цветами палитры, и старые номера значат
// в нём другие цвета. Мажор поднят, чтобы файл версии 1.0 отказался читаться, а
// не перекрасился молча.
inline constexpr std::string_view voxm_file_version = "2.0";

// Как записаны воксели. Поле стоит в шапке, чтобы двоичная запись появилась
// позже без смены расширения и версии: читатель выберет ветку по нему.
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

    voxm_deserializer(model_registry& registry, const block_registry& blocks);

    auto deserialize(const std::filesystem::path& filepath)
        -> std::expected<std::shared_ptr<model>, error_type>;

    // Разбор в отрыве от файловой системы: тем же путём идёт и файл, и буфер из
    // фаззера, и строка из теста. Ошибку открытия эта форма вернуть не может.
    auto deserialize(std::istream& input) -> std::expected<std::shared_ptr<model>, error_type>;

private:
    auto process_comment_(std::istringstream& iss) -> void;
    auto process_encoding_(std::istringstream& iss) -> void;
    auto process_category_(std::istringstream& iss) -> void;
    auto process_size_(std::istringstream& iss) -> void;
    auto process_pivot_(std::istringstream& iss) -> void;
    auto process_run_(std::istringstream& iss) -> void;

    // Объём нельзя создать раньше, чем прочитаны размер и набор, а пробеги идут
    // следом за ними. Между шапкой и телом и стоит эта отложенная сборка.
    [[nodiscard]] auto ensure_model_() -> bool;

    model_registry* registry_;
    const block_registry* blocks_;

    std::shared_ptr<model> model_;
    std::optional<error_type> error_;

    // Блок вне каталога чтение не рвёт — он нарисуется заглушкой, и это видно.
    // Но сказать о нём надо один раз, а не по разу на пробег.
    std::unordered_set<uint16> unknown_blocks_;

    block_category category_{};
    vec3i size_{};
    vec3f pivot_{};
    bool has_category_ = false;
    bool has_size_     = false;
};

}  // namespace vw::asset
