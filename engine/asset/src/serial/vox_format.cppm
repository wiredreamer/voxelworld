export module vw.asset:serial.vox;

import std;

import vw.core;
import :model;
import :anim;
import :serial.ref;
import :serial.version;
import :serial.text;

export namespace vw::asset {

struct vox_tag {
    std::string name;
    std::string value;
    std::vector<std::pair<std::string, std::string>> props;

    [[nodiscard]] auto prop(std::string_view key) const -> std::string_view {
        const auto it = std::ranges::find(props, key, &std::pair<std::string, std::string>::first);
        return it != props.end() ? std::string_view{it->second} : std::string_view{};
    }

    auto set_prop(std::string key, std::string text) -> vox_tag& {
        props.emplace_back(std::move(key), std::move(text));
        return *this;
    }

    [[nodiscard]] auto operator==(const vox_tag& other) const -> bool = default;
};

struct vox_entity_data {
    std::string name;
    std::string parent_name;

    std::vector<vox_tag> tags;

    [[nodiscard]] auto find(std::string_view tag_name) const -> const vox_tag* {
        const auto it = std::ranges::find(tags, tag_name, &vox_tag::name);
        return it != tags.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] auto value_of(std::string_view tag_name) const -> std::string_view {
        const auto* tag = find(tag_name);
        return tag != nullptr ? std::string_view{tag->value} : std::string_view{};
    }

    auto add(std::string tag_name, std::string value = {}) -> vox_tag& {
        tags.push_back(vox_tag{.name = std::move(tag_name), .value = std::move(value)});
        return tags.back();
    }

    [[nodiscard]] auto operator==(const vox_entity_data& other) const -> bool = default;
};

struct vox_prefab_data {
    std::string root_name;

    std::string rig;

    std::vector<asset_ref> fsm_refs;

    std::vector<vox_entity_data> entities;

    [[nodiscard]] auto operator==(const vox_prefab_data& other) const -> bool = default;
};

[[nodiscard]] auto parse_floats(std::string_view text, std::span<float32> out) -> bool;

inline constexpr std::string_view vox_file_version = "4.0";

class vox_parser {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    virtual ~vox_parser() = default;

    virtual auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> = 0;
};

class vox_parser_plain final : public vox_parser {
public:
    auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> override;

    auto parse(std::istream& input) -> std::expected<vox_prefab_data, error_type>;

private:
    static constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

    auto process_line_(std::string_view line) -> void;
    auto process_version_(std::string_view text) -> void;

    auto process_top_(std::string_view name, std::string_view value) -> void;
    auto process_tag_(std::string_view name, std::string_view value) -> void;
    auto process_prop_(std::string_view name, std::string_view value) -> void;

    vox_prefab_data prefab_;

    std::size_t entity_index_ = no_index;
    std::size_t tag_index_    = no_index;

    std::optional<error_type> error_;
};

class vox_writer {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    virtual ~vox_writer() = default;

    virtual auto write(const std::filesystem::path& filepath, const vox_prefab_data& prefab)
        -> std::expected<void, error_type> = 0;
};

class vox_writer_plain final : public vox_writer {
public:
    auto write(const std::filesystem::path& filepath, const vox_prefab_data& prefab)
        -> std::expected<void, error_type> override;

    auto write(std::ostream& output, const vox_prefab_data& prefab)
        -> std::expected<void, error_type>;

private:
    auto write_header_(std::ostream& output, const vox_prefab_data& prefab) -> void;
    auto write_entity_(std::ostream& output, const vox_entity_data& ent) -> void;
};

inline constexpr std::string_view voxa_file_version = "2.0";

}  // namespace vw::asset
