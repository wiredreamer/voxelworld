module vw.asset;

import std;
import vw.core;

namespace vw::asset {

namespace detail {
constexpr log::log_category voxm_lc{"voxm"};

auto encoding_name(voxm_encoding encoding) -> std::string_view {
    switch (encoding) {
        case voxm_encoding::rle: return "rle";
    }
    return "rle";
}

}  // namespace detail

voxm_serializer::voxm_serializer(
    const model& model
)
    : model_(&model) {}

auto voxm_serializer::serialize(
    const std::filesystem::path& filepath
) -> std::expected<void, error_type> {
    std::ofstream file(filepath.string(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn(detail::voxm_lc, "failed to open file for writing: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    serialize(file);

    if (!file.good()) {
        log::warn(detail::voxm_lc, "write error for file: {}", filepath.string());
        return std::unexpected(error_type::write_failed);
    }

    return {};
}

auto voxm_serializer::serialize(
    std::ostream& output
) -> void {
    const auto size  = model_->size();
    const auto pivot = model_->pivot();

    output << std::format("# Voxm File Version {}\n", voxm_file_version);
    output << std::format("encoding {}\n", detail::encoding_name(voxm_encoding::rle));
    output << std::format("category {}\n", model_->category().value);
    output << std::format("size {} {} {}\n", size.x, size.y, size.z);
    output << std::format("pivot {} {} {}\n", pivot.x, pivot.y, pivot.z);

    // Пробегами по X: набор у модели один, поэтому в записи стоит только номер
    // в наборе. Строка одинаковых вокселей — обычное дело даже у персонажа, и
    // построчная запись короче повоксельной в разы.
    for (int32 z = 0; z < size.z; ++z) {
        for (int32 y = 0; y < size.y; ++y) {
            int32 x = 0;
            while (x < size.x) {
                const auto index = model_->get_index(x, y, z);
                if (index.is_empty()) {
                    ++x;
                    continue;
                }

                int32 run = 1;
                while (x + run < size.x && model_->get_index(x + run, y, z) == index) {
                    ++run;
                }

                output << std::format("r {} {} {} {} {}\n", x, y, z, run, index.value);
                x += run;
            }
        }
    }
}

voxm_deserializer::voxm_deserializer(
    model_registry& registry, const voxel_registry& voxel_types
)
    : registry_(&registry), voxel_types_(&voxel_types) {}

auto voxm_deserializer::deserialize(
    const std::filesystem::path& filepath
) -> std::expected<std::shared_ptr<model>, error_type> {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log::warn(detail::voxm_lc, "failed to open file: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    auto result = deserialize(file);
    if (!result) {
        log::warn(detail::voxm_lc, "parse error in file: {}", filepath.string());
    }
    return result;
}

auto voxm_deserializer::deserialize(
    std::istream& input
) -> std::expected<std::shared_ptr<model>, error_type> {
    model_        = nullptr;
    error_        = std::nullopt;
    has_category_ = false;
    has_size_     = false;
    pivot_        = vec3f{};
    unknown_voxels_.clear();

    std::string line;
    while (std::getline(input, line)) {
        if (error_.has_value()) {
            break;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd.empty()) {
            continue;
        }

        if (cmd[0] == '#') {
            process_comment_(iss);
        } else if (cmd == "encoding") {
            process_encoding_(iss);
        } else if (cmd == "category") {
            process_category_(iss);
        } else if (cmd == "size") {
            process_size_(iss);
        } else if (cmd == "pivot") {
            process_pivot_(iss);
        } else if (cmd == "r") {
            process_run_(iss);
        } else {
            log::warn(detail::voxm_lc, "unknown command: {}", cmd);
        }
    }

    if (error_.has_value()) {
        return std::unexpected(*error_);
    }

    // Пустой объём — законный файл: модель, у которой стёрли все воксели, тоже
    // должна сохраняться и читаться.
    if (!ensure_model_()) {
        return std::unexpected(error_type::parse_error);
    }

    model_->compact_pages();
    model_->set_pivot(pivot_);

    return std::move(model_);
}

auto voxm_deserializer::process_comment_(std::istringstream& iss) -> void {
    const auto version = detail::read_header_version(iss);
    if (!version.has_value()) {
        return;
    }

    if (detail::major_version_differs(*version, voxm_file_version)) {
        log::warn(
            detail::voxm_lc, "unsupported voxm version {}, expected {}", *version, voxm_file_version
        );
        error_ = error_type::unsupported_version;
    }
}

auto voxm_deserializer::process_encoding_(std::istringstream& iss) -> void {
    std::string name;
    iss >> name;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }

    if (name != detail::encoding_name(voxm_encoding::rle)) {
        log::warn(detail::voxm_lc, "unsupported voxm encoding '{}'", name);
        error_ = error_type::parse_error;
    }
}

auto voxm_deserializer::process_category_(std::istringstream& iss) -> void {
    uint32 value = 0;
    iss >> value;
    if (iss.fail() || value > std::numeric_limits<uint8>::max()) {
        error_ = error_type::parse_error;
        return;
    }

    category_     = voxel_category{static_cast<uint8>(value)};
    has_category_ = true;
}

auto voxm_deserializer::process_size_(std::istringstream& iss) -> void {
    vec3i size;
    iss >> size.x >> size.y >> size.z;
    if (iss.fail() || size.x <= 0 || size.y <= 0 || size.z <= 0) {
        error_ = error_type::parse_error;
        return;
    }

    size_     = size;
    has_size_ = true;
}

auto voxm_deserializer::process_pivot_(std::istringstream& iss) -> void {
    vec3f pivot;
    iss >> pivot.x >> pivot.y >> pivot.z;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }

    pivot_ = pivot;
}

auto voxm_deserializer::process_run_(std::istringstream& iss) -> void {
    if (!ensure_model_()) {
        error_ = error_type::parse_error;
        return;
    }

    vec3i at;
    int32 count  = 0;
    uint32 index = 0;
    iss >> at.x >> at.y >> at.z >> count >> index;
    if (iss.fail() || count <= 0 || index > std::numeric_limits<uint8>::max()) {
        error_ = error_type::parse_error;
        return;
    }

    if (at.x < 0 || at.y < 0 || at.z < 0 || at.y >= size_.y || at.z >= size_.z ||
        at.x + count > size_.x) {
        error_ = error_type::parse_error;
        return;
    }

    const auto id = voxel{category_, static_cast<uint8>(index)};

    // Блок вне каталога — не повод потерять пробег: он нарисуется заглушкой, и
    // это видно сразу, а половина модели из-за одного номера пропасть не может.
    if (id != voxels::air && voxel_types_->slot_of(id) == missing_voxel_slot &&
        unknown_voxels_.insert(id.value).second) {
        log::warn(
            detail::voxm_lc, "voxel {}:{} is not in the catalog and will draw as the missing voxel",
            category_.value, index
        );
    }

    model_writer writer{*model_};
    for (int32 i = 0; i < count; ++i) {
        writer.set(vec3i{at.x + i, at.y, at.z}, id);
    }
}

auto voxm_deserializer::ensure_model_() -> bool {
    if (model_) {
        return true;
    }

    if (!has_category_ || !has_size_) {
        return false;
    }

    model_ = registry_->create_unnamed(category_, size_);
    return model_ != nullptr;
}

}  // namespace vw::asset
