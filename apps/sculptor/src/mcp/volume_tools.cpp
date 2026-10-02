module vw.sculptor;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.gfx;

namespace vw::sculptor::mcp {

namespace {

constexpr std::size_t max_cells_in_layers = 40'000;
constexpr std::size_t max_cells_in_write  = 2'000'000;

constexpr char air_symbol = '.';
constexpr std::string_view voxel_symbols =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+*#@";

constexpr std::string_view get_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of the node whose volume to read."},
        "min": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3, "description": "Low corner [x, y, z] of the region to return as layers. Defaults to the low corner of the occupied voxels."},
        "max": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3, "description": "High corner [x, y, z] of the region, inclusive. Defaults to the high corner of the occupied voxels."}
    },
    "required": ["node"],
    "additionalProperties": false
})";

constexpr std::string_view write_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of the node whose volume to write."},
        "boxes": {
            "description": "Solid boxes, applied first and in order. Corners are inclusive.",
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "min": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3},
                    "max": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3},
                    "voxel": {"description": "Voxel name from palette_list, its index, or 'air' to erase.", "type": ["string", "integer", "null"]}
                },
                "required": ["min", "max", "voxel"],
                "additionalProperties": false
            }
        },
        "points": {
            "description": "Single voxels, applied after the boxes.",
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "voxel": {"description": "Voxel name from palette_list, its index, or 'air' to erase.", "type": ["string", "integer", "null"]},
                    "at": {"type": "array", "items": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3}, "description": "Positions [x, y, z] to set to this voxel."}
                },
                "required": ["voxel", "at"],
                "additionalProperties": false
            }
        },
        "layers": {
            "description": "Voxels drawn as text, applied last; the shape volume_get returns. slices[k] is the layer y = origin.y + k, its row j is z = origin.z + j, and the character i of a row is x = origin.x + i. '.' is air; every other character must be in the legend.",
            "type": "object",
            "properties": {
                "origin": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3, "description": "Position of the first character of the first row of the first slice. Default [0, 0, 0]."},
                "legend": {"type": "object", "description": "Maps a character to a voxel name or index.", "additionalProperties": {"type": ["string", "integer"]}},
                "slices": {"type": "array", "items": {"type": "array", "items": {"type": "string"}}},
                "air": {"enum": ["erase", "keep"], "description": "What '.' does: 'erase' clears the cell (default), 'keep' leaves it as it is."}
            },
            "required": ["legend", "slices"],
            "additionalProperties": false
        }
    },
    "required": ["node"],
    "additionalProperties": false
})";

constexpr std::string_view reshape_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of the node whose volume to reshape."},
        "resize": {
            "description": "Add cells at the low and the high side of each axis; a negative amount removes them. Growing at the low side moves every voxel and the pivot up by that amount.",
            "type": "object",
            "properties": {
                "min": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3, "description": "Cells to add before x, y and z = 0. Default [0, 0, 0]."},
                "max": {"type": "array", "items": {"type": "integer"}, "minItems": 3, "maxItems": 3, "description": "Cells to add after the last x, y and z. Default [0, 0, 0]."}
            },
            "additionalProperties": false
        },
        "trim": {"type": "boolean", "description": "true cuts the volume down to its occupied voxels."},
        "rotate": {
            "type": "object",
            "description": "Turn the voxels about an axis through the pivot, in quarter turns.",
            "properties": {
                "axis": {"enum": ["x", "y", "z"]},
                "quarter_turns": {"type": "integer", "description": "1 is 90 degrees, -1 the other way, 2 is 180."}
            },
            "required": ["axis", "quarter_turns"],
            "additionalProperties": false
        },
        "mirror": {"enum": ["x", "y", "z"], "description": "Flip the voxels along this axis through the pivot."}
    },
    "required": ["node"],
    "additionalProperties": false
})";

constexpr std::string_view pivot_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of the node whose volume to change."},
        "pivot": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "The point of the volume, in voxel coordinates, that sits at the origin of the node. May be fractional."}
    },
    "required": ["node", "pivot"],
    "additionalProperties": false
})";

constexpr std::string_view rename_schema = R"({
    "type": "object",
    "properties": {
        "node": {"type": "string", "description": "Name of a node that holds the volume."},
        "name": {"type": "string", "description": "New file name of the volume, without folder or extension."},
        "overwrite": {"type": "boolean", "description": "Replace a stray volume file of that name. Default false."}
    },
    "required": ["node", "name"],
    "additionalProperties": false
})";

[[nodiscard]] auto label_of(const voxel_registry& registry, voxel value) -> json::value {
    if (value.is_empty()) {
        return "air";
    }
    if (registry.known(value) && !registry.get(value).name.empty()) {
        return registry.get(value).name;
    }
    return value.value;
}

[[nodiscard]] auto cells_of(const asset::voxel_bounds& region) -> std::size_t {
    const vec3i size = region.size();
    return static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
        static_cast<std::size_t>(size.z);
}

[[nodiscard]] auto describe_bounds(const asset::voxel_bounds& bounds) -> json::value {
    return json::object{{"min", json_of(bounds.min)}, {"max", json_of(bounds.max)}};
}

[[nodiscard]] auto count_solid(const asset::model& model) -> std::size_t {
    const vec3i size = model.size();

    std::size_t count = 0;
    vec3i at;
    for (at.x = 0; at.x < size.x; ++at.x) {
        for (at.y = 0; at.y < size.y; ++at.y) {
            for (at.z = 0; at.z < size.z; ++at.z) {
                count += model.is_empty(at.x, at.y, at.z) ? 0 : 1;
            }
        }
    }
    return count;
}

[[nodiscard]] auto describe_volume(const editor_bindings& bindings, std::string_view node)
    -> json::object {
    const auto held = bindings.volumes->find(node);
    if (!held) {
        return json::object{{"node", node}};
    }

    const asset::model& model = **held;

    const auto ent    = bindings.state->scene.name_to_entity.at(std::string{node});
    const auto source = bindings.engine->get_world().get<ecs::model_component>(ent).get_source();

    json::array shared_with;
    for (const std::string& holder : bindings.volumes->holders(*held)) {
        if (holder != node) {
            shared_with.emplace_back(holder);
        }
    }

    const auto occupied = asset::occupied_bounds(model);

    json::object summary{
        {"node", node},
        {"ref", json_or_null(source.str())},
        {"size", json_of(model.size())},
        {"pivot", json_of(model.pivot())},
        {"occupied", occupied ? describe_bounds(*occupied) : json::value{}},
        {"voxel_count", count_solid(model)},
    };
    if (!shared_with.empty()) {
        summary.set("shared_with", std::move(shared_with));
    }
    return summary;
}

[[nodiscard]] auto describe_layers(
    const voxel_registry& registry, const asset::model& model, const asset::voxel_bounds& region
) -> json::value {
    std::array<std::size_t, voxel_type_capacity> counts{};

    vec3i at;
    for (at.y = region.min.y; at.y <= region.max.y; ++at.y) {
        for (at.z = region.min.z; at.z <= region.max.z; ++at.z) {
            for (at.x = region.min.x; at.x <= region.max.x; ++at.x) {
                ++counts[model.get_voxel(at).value];
            }
        }
    }

    std::vector<uint8> used;
    for (std::size_t value = 1; value < counts.size(); ++value) {
        if (counts[value] > 0) {
            used.push_back(static_cast<uint8>(value));
        }
    }
    std::ranges::sort(used, [&counts](uint8 left, uint8 right) {
        return counts[left] != counts[right] ? counts[left] > counts[right] : left < right;
    });

    std::array<char, voxel_type_capacity> symbol_of{};
    symbol_of.fill('?');
    symbol_of[0] = air_symbol;

    json::object legend;
    for (std::size_t rank = 0; rank < used.size() && rank < voxel_symbols.size(); ++rank) {
        symbol_of[used[rank]] = voxel_symbols[rank];
        legend.set(std::string(1, voxel_symbols[rank]), label_of(registry, voxel{used[rank]}));
    }

    json::array slices;
    for (at.y = region.min.y; at.y <= region.max.y; ++at.y) {
        json::array rows;
        for (at.z = region.min.z; at.z <= region.max.z; ++at.z) {
            std::string row;
            for (at.x = region.min.x; at.x <= region.max.x; ++at.x) {
                row.push_back(symbol_of[model.get_voxel(at).value]);
            }
            rows.emplace_back(std::move(row));
        }
        slices.emplace_back(std::move(rows));
    }

    return json::object{
        {"origin", json_of(region.min)},
        {"legend", std::move(legend)},
        {"slices", std::move(slices)},
    };
}

[[nodiscard]] auto read_position(const json::cursor& at) -> std::expected<vec3i, std::string> {
    const auto elements = at.elements();
    if (!elements) {
        return std::unexpected(json::describe(elements.error()));
    }
    if (elements->size() != 3) {
        return std::unexpected(std::format(
            "{}: expected three integers [x, y, z], found {}", at.path(), elements->size()
        ));
    }

    std::array<int32, 3> axes{};
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
        const auto number = (*elements)[axis].integer();
        if (!number) {
            return std::unexpected(json::describe(number.error()));
        }
        axes[axis] = static_cast<int32>(std::clamp<int64>(
            *number, std::numeric_limits<int32>::min(), std::numeric_limits<int32>::max()
        ));
    }
    return vec3i{axes[0], axes[1], axes[2]};
}

[[nodiscard]] auto read_voxel(const voxel_registry& registry, const json::cursor& at)
    -> std::expected<voxel, std::string> {
    const json::value* given = at.get();
    if (given == nullptr) {
        return std::unexpected(std::format("{}: missing, expected a voxel name or index", at.path()));
    }
    if (given->is_null()) {
        return voxel{};
    }

    if (const std::string* name = given->as_string()) {
        if (*name == "air") {
            return voxel{};
        }
        if (const auto found = registry.find(*name)) {
            return *found;
        }
        return std::unexpected(std::format(
            "{}: there is no voxel named '{}'; palette_list names them", at.path(), *name
        ));
    }

    if (const auto index = given->as_integer()) {
        if (*index == 0) {
            return voxel{};
        }
        if (*index > 0 && *index < static_cast<int64>(voxel_type_capacity) &&
            registry.known(voxel{static_cast<uint8>(*index)})) {
            return voxel{static_cast<uint8>(*index)};
        }
        return std::unexpected(std::format(
            "{}: there is no voxel with index {}; palette_list names them", at.path(), *index
        ));
    }

    return std::unexpected(std::format("{}: expected a voxel name or index", at.path()));
}

[[nodiscard]] auto outside(const json::cursor& at, vec3i position, vec3i size) -> std::string {
    return std::format(
        "{}: [{}, {}, {}] is outside the volume, which spans [0, 0, 0] to [{}, {}, {}]; grow it "
        "with volume_reshape first",
        at.path(), position.x, position.y, position.z, size.x - 1, size.y - 1, size.z - 1
    );
}

class edit_collector final {
public:
    edit_collector(const voxel_registry& registry, vec3i size)
        : registry_(&registry),
          size_(size) {}

    auto add_boxes(const json::cursor& boxes) -> void {
        const auto elements = boxes.elements();
        if (!elements) {
            fail_(json::describe(elements.error()));
            return;
        }

        for (const json::cursor& box : *elements) {
            argument_reader fields{box};
            fields.allow({"min", "max", "voxel"});
            if (fields.failed()) {
                fail_(fields.error());
                return;
            }

            const auto low   = read_position(box["min"]);
            const auto high  = read_position(box["max"]);
            const auto value = read_voxel(*registry_, box["voxel"]);
            if (!low || !high || !value) {
                fail_(!low ? low.error() : !high ? high.error() : value.error());
                return;
            }
            if (low->x > high->x || low->y > high->y || low->z > high->z) {
                fail_(std::format("{}: min must not exceed max on any axis", box.path()));
                return;
            }
            if (!asset::contains(size_, *low)) {
                fail_(outside(box["min"], *low, size_));
                return;
            }
            if (!asset::contains(size_, *high)) {
                fail_(outside(box["max"], *high, size_));
                return;
            }

            const asset::voxel_bounds region{.min = *low, .max = *high};
            if (!reserve_(cells_of(region))) {
                return;
            }

            vec3i at;
            for (at.x = low->x; at.x <= high->x; ++at.x) {
                for (at.y = low->y; at.y <= high->y; ++at.y) {
                    for (at.z = low->z; at.z <= high->z; ++at.z) {
                        edits_.push_back(asset::voxel_edit{.position = at, .value = *value});
                    }
                }
            }
        }
    }

    auto add_points(const json::cursor& points) -> void {
        const auto groups = points.elements();
        if (!groups) {
            fail_(json::describe(groups.error()));
            return;
        }

        for (const json::cursor& group : *groups) {
            argument_reader fields{group};
            fields.allow({"voxel", "at"});
            if (fields.failed()) {
                fail_(fields.error());
                return;
            }

            const auto value     = read_voxel(*registry_, group["voxel"]);
            const auto positions = group["at"].elements();
            if (!value || !positions) {
                fail_(!value ? value.error() : json::describe(positions.error()));
                return;
            }
            if (!reserve_(positions->size())) {
                return;
            }

            for (const json::cursor& entry : *positions) {
                const auto position = read_position(entry);
                if (!position) {
                    fail_(position.error());
                    return;
                }
                if (!asset::contains(size_, *position)) {
                    fail_(outside(entry, *position, size_));
                    return;
                }
                edits_.push_back(asset::voxel_edit{.position = *position, .value = *value});
            }
        }
    }

    auto add_layers(const json::cursor& layers) -> void {
        argument_reader fields{layers};
        fields.allow({"origin", "legend", "slices", "air"});

        const vec3i origin   = fields.optional_vec3i("origin").value_or(vec3i{});
        const auto air_mode  = fields.optional_text("air").value_or(std::string{"erase"});
        const auto legend    = layers["legend"].fields();
        const auto slices    = layers["slices"].elements();
        if (fields.failed() || !legend || !slices) {
            fail_(
                fields.failed() ? fields.error()
                : !legend       ? json::describe(legend.error())
                                : json::describe(slices.error())
            );
            return;
        }
        if (air_mode != "erase" && air_mode != "keep") {
            fail_(std::format("{}: expected 'erase' or 'keep', found '{}'", layers["air"].path(), air_mode));
            return;
        }

        std::array<std::optional<voxel>, 256> voxel_of{};
        for (const auto& [symbol, entry] : *legend) {
            if (symbol.size() != 1 || symbol.front() == air_symbol) {
                fail_(std::format(
                    "{}: a legend key is one character other than '.', found '{}'",
                    layers["legend"].path(), symbol
                ));
                return;
            }
            const auto value = read_voxel(*registry_, entry);
            if (!value) {
                fail_(value.error());
                return;
            }
            voxel_of[static_cast<uint8>(symbol.front())] = *value;
        }

        for (std::size_t slice_index = 0; slice_index < slices->size(); ++slice_index) {
            const auto rows = (*slices)[slice_index].elements();
            if (!rows) {
                fail_(json::describe(rows.error()));
                return;
            }

            for (std::size_t row_index = 0; row_index < rows->size(); ++row_index) {
                const json::cursor& row_at = (*rows)[row_index];
                const auto row             = row_at.string();
                if (!row) {
                    fail_(json::describe(row.error()));
                    return;
                }
                if (!reserve_(row->size())) {
                    return;
                }

                for (std::size_t column = 0; column < row->size(); ++column) {
                    const char symbol = (*row)[column];
                    const vec3i position{
                        origin.x + static_cast<int32>(column),
                        origin.y + static_cast<int32>(slice_index),
                        origin.z + static_cast<int32>(row_index),
                    };

                    std::optional<voxel> value;
                    if (symbol == air_symbol) {
                        if (air_mode == "keep") {
                            continue;
                        }
                        value = voxel{};
                    } else {
                        value = voxel_of[static_cast<uint8>(symbol)];
                    }

                    if (!value) {
                        fail_(std::format(
                            "{}: the character '{}' at column {} is not in the legend", row_at.path(),
                            symbol, column
                        ));
                        return;
                    }
                    if (!asset::contains(size_, position)) {
                        fail_(outside(row_at, position, size_));
                        return;
                    }
                    edits_.push_back(asset::voxel_edit{.position = position, .value = *value});
                }
            }
        }
    }

    [[nodiscard]] auto failed() const -> bool {
        return !error_.empty();
    }

    [[nodiscard]] auto error() const -> const std::string& {
        return error_;
    }

    [[nodiscard]] auto take() -> std::vector<asset::voxel_edit> {
        return std::move(edits_);
    }

private:
    auto fail_(std::string message) -> void {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    [[nodiscard]] auto reserve_(std::size_t more) -> bool {
        if (edits_.size() + more > max_cells_in_write) {
            fail_(std::format(
                "one call may write at most {} cells; split the work into several calls",
                max_cells_in_write
            ));
            return false;
        }
        edits_.reserve(edits_.size() + more);
        return true;
    }

    const voxel_registry* registry_;
    vec3i size_;
    std::vector<asset::voxel_edit> edits_;
    std::string error_;
};

[[nodiscard]] auto axis_of(std::string_view text) -> std::optional<asset::voxel_axis> {
    if (text == "x") {
        return asset::voxel_axis::x;
    }
    if (text == "y") {
        return asset::voxel_axis::y;
    }
    if (text == "z") {
        return asset::voxel_axis::z;
    }
    return std::nullopt;
}

[[nodiscard]] auto read_volume(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"node", "min", "max"});
    const std::string node = in.text("node");
    const auto low         = in.optional_vec3i("min");
    const auto high        = in.optional_vec3i("max");
    if (in.failed()) {
        return tool_failure(in.error());
    }

    const auto held = bindings.volumes->find(node);
    if (!held) {
        return tool_failure(held.error());
    }

    const asset::model& model = **held;
    const vec3i size          = model.size();

    json::object described = describe_volume(bindings, node);

    const auto occupied = asset::occupied_bounds(model);
    if (!occupied && !low && !high) {
        described.set("layers", json::value{});
        return tool_success(described);
    }

    const asset::voxel_bounds whole{.min = {}, .max = {size.x - 1, size.y - 1, size.z - 1}};
    const asset::voxel_bounds fallback = occupied.value_or(whole);
    const asset::voxel_bounds region{
        .min = low.value_or(fallback.min), .max = high.value_or(fallback.max)
    };

    if (!asset::contains(size, region.min) || !asset::contains(size, region.max) ||
        region.min.x > region.max.x || region.min.y > region.max.y || region.min.z > region.max.z) {
        return tool_failure(std::format(
            "the region [{}, {}, {}]..[{}, {}, {}] does not fit the volume, which spans [0, 0, 0] "
            "to [{}, {}, {}]",
            region.min.x, region.min.y, region.min.z, region.max.x, region.max.y, region.max.z,
            size.x - 1, size.y - 1, size.z - 1
        ));
    }

    if (cells_of(region) > max_cells_in_layers) {
        described.set(
            "truncated",
            std::format(
                "the region holds {} cells, more than {} in one answer; ask for a part of it with "
                "min and max",
                cells_of(region), max_cells_in_layers
            )
        );
        return tool_success(described);
    }

    described.set(
        "layers", describe_layers(bindings.engine->get_voxel_registry(), model, region)
    );
    return tool_success(described);
}

[[nodiscard]] auto write_volume(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"node", "boxes", "points", "layers"});
    const std::string node = in.text("node");
    if (in.failed()) {
        return tool_failure(in.error());
    }
    if (!in.has("boxes") && !in.has("points") && !in.has("layers")) {
        return tool_failure("give at least one of boxes, points and layers");
    }

    const auto held = bindings.volumes->find(node);
    if (!held) {
        return tool_failure(held.error());
    }

    edit_collector collected{bindings.engine->get_voxel_registry(), (*held)->size()};
    if (in.has("boxes")) {
        collected.add_boxes(in.at("boxes"));
    }
    if (in.has("points") && !collected.failed()) {
        collected.add_points(in.at("points"));
    }
    if (in.has("layers") && !collected.failed()) {
        collected.add_layers(in.at("layers"));
    }
    if (collected.failed()) {
        return tool_failure(collected.error());
    }

    auto edits               = collected.take();
    const std::size_t cells  = edits.size();
    const auto written       = bindings.volumes->write(node, std::move(edits));
    if (!written) {
        return tool_failure(written.error());
    }

    json::object described = describe_volume(bindings, node);
    described.set("cells_written", cells);
    return tool_success(described);
}

[[nodiscard]] auto reshape_volume(const editor_bindings& bindings, const json::value& arguments)
    -> tool_outcome {
    argument_reader in{arguments};
    in.allow({"node", "resize", "trim", "rotate", "mirror"});
    const std::string node = in.text("node");
    const bool trims       = in.flag_or("trim", false);
    if (in.failed()) {
        return tool_failure(in.error());
    }

    const int32 actions = static_cast<int32>(in.has("resize")) + static_cast<int32>(trims) +
        static_cast<int32>(in.has("rotate")) + static_cast<int32>(in.has("mirror"));
    if (actions != 1) {
        return tool_failure("give exactly one of resize, trim, rotate and mirror");
    }

    const auto held = bindings.volumes->find(node);
    if (!held) {
        return tool_failure(held.error());
    }

    volume_service::outcome done;
    json::value shift;

    if (in.has("resize")) {
        argument_reader resize{in.at("resize")};
        resize.allow({"min", "max"});
        const vec3i at_min = resize.optional_vec3i("min").value_or(vec3i{});
        const vec3i at_max = resize.optional_vec3i("max").value_or(vec3i{});
        if (resize.failed()) {
            return tool_failure(resize.error());
        }

        done  = bindings.volumes->resize(node, at_min, at_max);
        shift = json_of(at_min);
    } else if (trims) {
        const auto occupied = asset::occupied_bounds(**held);
        done                = bindings.volumes->trim(node);
        if (occupied) {
            shift = json_of(vec3i{-occupied->min.x, -occupied->min.y, -occupied->min.z});
        }
    } else if (in.has("rotate")) {
        argument_reader rotate{in.at("rotate")};
        rotate.allow({"axis", "quarter_turns"});
        const auto axis  = axis_of(rotate.text("axis"));
        const auto turns = rotate.at("quarter_turns").integer();
        if (rotate.failed() || !axis || !turns) {
            return tool_failure(
                rotate.failed() ? rotate.error()
                : !axis         ? std::format("{}: expected x, y or z", rotate.at("axis").path())
                                : json::describe(turns.error())
            );
        }

        done = bindings.volumes->reorient(
            node, asset::rotated_orientation(*axis, static_cast<int32>(*turns % 4))
        );
    } else {
        const auto axis = axis_of(in.text("mirror"));
        if (in.failed() || !axis) {
            return tool_failure(
                in.failed() ? in.error() : std::string{"arguments.mirror: expected x, y or z"}
            );
        }

        done = bindings.volumes->reorient(node, asset::mirrored_orientation(*axis));
    }

    if (!done) {
        return tool_failure(done.error());
    }

    json::object described = describe_volume(bindings, node);
    if (shift.is_null()) {
        described.set("note", "the voxels were rearranged; read the volume again before writing");
    } else {
        described.set("shift", std::move(shift));
    }
    return tool_success(described);
}

}  // namespace

auto append_volume_tools(std::vector<tool>& tools, const editor_bindings& bindings) -> void {
    tools.push_back(tool{
        .name = "volume_get",
        .description =
            "Read the voxel volume of a node: its size, pivot, the box of occupied voxels and "
            "the voxels themselves as text layers. x and z lie in the layer, y is up. In "
            "layers.slices[k] row j character i is the voxel at x = origin.x + i, y = origin.y "
            "+ k, z = origin.z + j; '.' is air and layers.legend names the other characters. "
            "Without min and max the layers cover the occupied voxels.",
        .input_schema = get_schema,
        .run          = [bindings](const json::value& arguments) -> tool_outcome {
            return read_volume(bindings, arguments);
        },
    });

    tools.push_back(tool{
        .name = "volume_write",
        .description =
            "Set voxels of a node's volume as one undo step: boxes first, then points, then "
            "layers, each overriding what came before. Every position must lie inside the "
            "volume; grow it with volume_reshape first if it does not. A volume shared by "
            "several nodes changes for all of them. Opens the volume in the editor.",
        .input_schema = write_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return write_volume(bindings, arguments);
            }
        ),
    });

    tools.push_back(tool{
        .name = "volume_reshape",
        .description =
            "Change the shape of a node's volume: resize it, trim it to its voxels, rotate it "
            "in quarter turns or mirror it. Exactly one action per call. The pivot follows the "
            "voxels, so the model stays where it was on the node. 'shift' in the answer is how "
            "far every voxel coordinate moved.",
        .input_schema = reshape_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                return reshape_volume(bindings, arguments);
            }
        ),
    });

    tools.push_back(tool{
        .name = "volume_set_pivot",
        .description =
            "Move the pivot of a node's volume: the point of the volume that sits at the origin "
            "of the node and that the node rotates about. Moving it shifts the model on every "
            "node that shares the volume.",
        .input_schema = pivot_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"node", "pivot"});
                const std::string node = in.text("node");
                const auto pivot       = in.optional_vec3f("pivot");
                if (in.failed() || !pivot) {
                    return tool_failure(
                        in.failed() ? in.error()
                                    : std::string{"arguments.pivot: missing, expected [x, y, z]"}
                    );
                }

                const auto moved = bindings.volumes->set_pivot(node, *pivot);
                if (!moved) {
                    return tool_failure(moved.error());
                }
                return tool_success(describe_volume(bindings, node));
            }
        ),
    });

    tools.push_back(tool{
        .name = "volume_fork",
        .description =
            "Give a node a volume of its own when it shares one with other nodes: the node gets "
            "a copy under a new file name and the others keep the old volume. As one undo step; "
            "the file is written when the prefab is saved. Edits of a shared volume show in "
            "every node that holds it, so fork before changing one of them alone.",
        .input_schema = rename_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"node", "name", "overwrite"});
                const std::string node = in.text("node");
                const std::string name = in.text("name");
                const bool overwrite   = in.flag_or("overwrite", false);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto forked = bindings.volumes->fork(node, name, overwrite);
                if (!forked) {
                    return tool_failure(forked.error());
                }
                return tool_success(describe_volume(bindings, node));
            }
        ),
    });

    tools.push_back(tool{
        .name = "volume_rename",
        .description =
            "Rename the file of a node's volume inside the folder of the prefab. Writes the "
            "volume and the prefab at once and clears the undo history.",
        .input_schema = rename_schema,
        .run          = when_idle(
            bindings,
            [bindings](const json::value& arguments) -> tool_outcome {
                argument_reader in{arguments};
                in.allow({"node", "name", "overwrite"});
                const std::string node = in.text("node");
                const std::string name = in.text("name");
                const bool overwrite   = in.flag_or("overwrite", false);
                if (in.failed()) {
                    return tool_failure(in.error());
                }

                const auto renamed = bindings.volumes->rename(node, name, overwrite);
                if (!renamed) {
                    return tool_failure(renamed.error());
                }
                return tool_success(describe_volume(bindings, node));
            }
        ),
    });
}

}  // namespace vw::sculptor::mcp
