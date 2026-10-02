module vw.sculptor;

import std;

import vw.core;

namespace vw::sculptor::mcp {

namespace {

[[nodiscard]] auto key_of(std::string_view field_path) -> std::string_view {
    const auto dot = field_path.rfind('.');
    return dot == std::string_view::npos ? field_path : field_path.substr(dot + 1);
}

}  // namespace

argument_reader::argument_reader(const json::value& arguments)
    : at_(arguments, "arguments") {}

argument_reader::argument_reader(json::cursor at)
    : at_(std::move(at)) {}

auto argument_reader::note_(const json::access_error& error) -> void {
    fail(json::describe(error));
}

auto argument_reader::fail(std::string message) -> void {
    if (error_.empty()) {
        error_ = std::move(message);
    }
}

auto argument_reader::failed() const -> bool {
    return !error_.empty();
}

auto argument_reader::error() const -> const std::string& {
    return error_;
}

auto argument_reader::allow(std::initializer_list<std::string_view> known) -> void {
    const auto fields = at_.fields();
    if (!fields) {
        note_(fields.error());
        return;
    }

    for (const auto& [key, field] : *fields) {
        if (std::ranges::contains(known, key)) {
            continue;
        }

        std::string listed;
        for (const std::string_view name : known) {
            if (!listed.empty()) {
                listed += ", ";
            }
            listed += name;
        }
        fail(std::format("{}: unknown field '{}'; the fields are: {}", at_.path(), key_of(field.path()), listed));
        return;
    }
}

auto argument_reader::has(std::string_view key) const -> bool {
    return at_[key].exists();
}

auto argument_reader::is_null(std::string_view key) const -> bool {
    const json::value* field = at_[key].get();
    return field != nullptr && field->is_null();
}

auto argument_reader::at(std::string_view key) const -> json::cursor {
    return at_[key];
}

auto argument_reader::text(std::string_view key) -> std::string {
    const auto read = at_[key].string();
    if (!read) {
        note_(read.error());
        return {};
    }
    return std::string{*read};
}

auto argument_reader::optional_text(std::string_view key) -> std::optional<std::string> {
    if (!has(key) || is_null(key)) {
        return std::nullopt;
    }
    return text(key);
}

auto argument_reader::flag_or(std::string_view key, bool fallback) -> bool {
    if (!has(key)) {
        return fallback;
    }

    const auto read = at_[key].boolean();
    if (!read) {
        note_(read.error());
        return fallback;
    }
    return *read;
}

auto argument_reader::optional_index(std::string_view key) -> std::optional<std::size_t> {
    if (!has(key) || is_null(key)) {
        return std::nullopt;
    }

    const auto read = at_[key].integer();
    if (!read) {
        note_(read.error());
        return std::nullopt;
    }
    if (*read < 0) {
        fail(std::format("{}: must not be negative, got {}", at_[key].path(), *read));
        return std::nullopt;
    }
    return static_cast<std::size_t>(*read);
}

auto argument_reader::optional_vec3f(std::string_view key) -> std::optional<vec3f> {
    if (!has(key) || is_null(key)) {
        return std::nullopt;
    }

    const json::cursor field = at_[key];
    const auto elements      = field.elements();
    if (!elements) {
        note_(elements.error());
        return std::nullopt;
    }
    if (elements->size() != 3) {
        fail(std::format("{}: expected three numbers [x, y, z], found {}", field.path(), elements->size()));
        return std::nullopt;
    }

    std::array<float32, 3> axes{};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const auto number = (*elements)[axis].number();
        if (!number) {
            note_(number.error());
            return std::nullopt;
        }
        axes[axis] = static_cast<float32>(*number);
    }
    return vec3f{axes[0], axes[1], axes[2]};
}

auto argument_reader::optional_vec3i(std::string_view key) -> std::optional<vec3i> {
    if (!has(key) || is_null(key)) {
        return std::nullopt;
    }

    const json::cursor field = at_[key];
    const auto elements      = field.elements();
    if (!elements) {
        note_(elements.error());
        return std::nullopt;
    }
    if (elements->size() != 3) {
        fail(std::format("{}: expected three integers [x, y, z], found {}", field.path(), elements->size()));
        return std::nullopt;
    }

    std::array<int32, 3> axes{};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const auto number = (*elements)[axis].integer();
        if (!number) {
            note_(number.error());
            return std::nullopt;
        }
        if (*number < std::numeric_limits<int32>::min() || *number > std::numeric_limits<int32>::max()) {
            fail(std::format("{}: {} is out of range", (*elements)[axis].path(), *number));
            return std::nullopt;
        }
        axes[axis] = static_cast<int32>(*number);
    }
    return vec3i{axes[0], axes[1], axes[2]};
}

auto argument_reader::text_list(std::string_view key) -> std::vector<std::string> {
    std::vector<std::string> listed;
    if (!has(key) || is_null(key)) {
        return listed;
    }

    const auto elements = at_[key].elements();
    if (!elements) {
        note_(elements.error());
        return listed;
    }

    for (const json::cursor& element : *elements) {
        const auto read = element.string();
        if (!read) {
            note_(read.error());
            return {};
        }
        listed.emplace_back(*read);
    }
    return listed;
}

}  // namespace vw::sculptor::mcp
