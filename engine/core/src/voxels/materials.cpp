module vw.core;

import std;

namespace vw {

material_table::material_table()
    : material_table(std::span<const material_type>{default_material_catalog}) {}

material_table::material_table(
    std::span<const material_type> rows
)
    : rows_(rows.begin(), rows.end())
    , named_(rows.size()) {
    if (rows_.size() > static_cast<std::size_t>(material_capacity)) {
        throw std::length_error("more materials than a byte holds");
    }
    rows_.resize(material_capacity);
}

auto material_table::find(
    std::string_view name
) const -> std::optional<material> {
    for (std::size_t row = 0; row < named_; ++row) {
        if (rows_[row].name == name) {
            return material{static_cast<uint8>(row)};
        }
    }
    return std::nullopt;
}

auto material_table::swaying() const -> material_set {
    material_set out;
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        out.set(row, rows_[row].sways);
    }
    return out;
}

auto material_table::emission() const -> material_levels {
    material_levels out{};
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        out[row] = rows_[row].emission;
    }
    return out;
}

auto default_material_table() -> const material_table& {
    static const material_table table;
    return table;
}

}  // namespace vw
