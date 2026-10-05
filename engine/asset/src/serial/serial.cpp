module vw.asset;


import std;
import vw.core;


namespace vw::asset {

namespace detail {
constexpr log::log_category vox_parser_plain_lc{"vox_parser_plain"};
}  // namespace detail

auto vox_parser_plain::parse(const std::filesystem::path& filepath)
    -> std::expected<vox_prefab_data, error_type> {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log::warn(detail::vox_parser_plain_lc, "failed to open file: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    auto result = parse(file);
    if (!result) {
        log::warn(detail::vox_parser_plain_lc, "parse error in file: {}", filepath.string());
    }
    return result;
}

auto parse_floats(
    std::string_view text, std::span<float32> out
) -> bool {
    std::istringstream iss{std::string{text}};
    for (auto& value : out) {
        iss >> value;
        if (iss.fail()) {
            return false;
        }
    }

    return true;
}

auto vox_parser_plain::parse(std::istream& input)
    -> std::expected<vox_prefab_data, error_type> {
    prefab_       = {};
    entity_index_ = no_index;
    tag_index_    = no_index;
    error_        = std::nullopt;

    std::string line;
    while (std::getline(input, line)) {
        if (error_.has_value()) {
            break;
        }

        process_line_(line);
    }

    if (error_.has_value()) {
        return std::unexpected(*error_);
    }

    return std::move(prefab_);
}

auto vox_parser_plain::process_line_(
    std::string_view line
) -> void {
    const auto parsed = detail::split_line(line);
    if (!parsed.has_value()) {
        return;
    }

    if (parsed->name == "#") {
        process_version_(parsed->value);
        return;
    }

    switch (parsed->depth) {
        case 0: process_top_(parsed->name, parsed->value); return;
        case 1: process_tag_(parsed->name, parsed->value); return;
        default: process_prop_(parsed->name, parsed->value); return;
    }
}

auto vox_parser_plain::process_version_(
    std::string_view text
) -> void {
    std::istringstream iss{std::string{text}};

    const auto version = detail::read_header_version(iss);
    if (!version.has_value()) {
        return;
    }

    if (detail::major_version_differs(*version, vox_file_version)) {
        log::warn(
            detail::vox_parser_plain_lc, "unsupported vox version {}, expected {}", *version,
            vox_file_version
        );
        error_ = error_type::unsupported_version;
    }
}

auto vox_parser_plain::process_top_(
    std::string_view name, std::string_view value
) -> void {
    if (name != "entity" && name != "root" && name != "rig" && name != "fsm") {
        log::warn(detail::vox_parser_plain_lc, "unknown top-level command: {}", name);
        return;
    }

    if (value.empty()) {
        error_ = error_type::parse_error;
        return;
    }

    if (name == "root") {
        prefab_.root_name = std::string{value};
        return;
    }

    if (name == "rig") {
        prefab_.rig = std::string{value};
        return;
    }

    if (name == "fsm") {
        prefab_.fsm_refs.emplace_back(value);
        return;
    }

    prefab_.entities.push_back(vox_entity_data{.name = std::string{value}});
    entity_index_ = prefab_.entities.size() - 1;
    tag_index_    = no_index;
}

auto vox_parser_plain::process_tag_(
    std::string_view name, std::string_view value
) -> void {
    if (entity_index_ == no_index) {
        error_ = error_type::parse_error;
        return;
    }

    auto& entity = prefab_.entities[entity_index_];

    if (name == "parent") {
        entity.parent_name = std::string{value};
        tag_index_         = no_index;
        return;
    }

    entity.add(std::string{name}, std::string{value});
    tag_index_ = entity.tags.size() - 1;
}

auto vox_parser_plain::process_prop_(
    std::string_view name, std::string_view value
) -> void {
    if (entity_index_ == no_index || tag_index_ == no_index) {
        error_ = error_type::parse_error;
        return;
    }

    prefab_.entities[entity_index_].tags[tag_index_].set_prop(
        std::string{name}, std::string{value}
    );
}

}  // namespace vw::asset


namespace vw::asset {

namespace detail {
constexpr log::log_category vox_writer_plain_lc{"vox_writer_plain"};
}  // namespace detail

auto vox_writer_plain::write(
    const std::filesystem::path& filepath, const vox_prefab_data& prefab
) -> std::expected<void, error_type> {
    std::ofstream file(filepath.string(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn(
            detail::vox_writer_plain_lc, "failed to open file for writing: {}", filepath.string()
        );
        return std::unexpected(error_type::file_open_failed);
    }

    const auto result = write(file, prefab);
    if (!result.has_value()) {
        log::warn(detail::vox_writer_plain_lc, "write error for file: {}", filepath.string());
    }

    return result;
}

auto vox_writer_plain::write(
    std::ostream& output, const vox_prefab_data& prefab
) -> std::expected<void, error_type> {
    write_header_(output, prefab);

    for (const auto& ent : prefab.entities) {
        write_entity_(output, ent);
    }

    if (!output.good()) {
        return std::unexpected(error_type::write_failed);
    }

    return {};
}

auto vox_writer_plain::write_header_(
    std::ostream& output, const vox_prefab_data& prefab
) -> void {
    output << std::format("# Vox File Version {}\n", vox_file_version);
    if (!prefab.rig.empty()) {
        output << std::format("rig {}\n", prefab.rig);
    }
    for (const auto& ref : prefab.fsm_refs) {
        output << std::format("fsm {}\n", ref.str());
    }
    output << std::format("root {}\n", prefab.root_name);
}

auto vox_writer_plain::write_entity_(
    std::ostream& output, const vox_entity_data& ent
) -> void {
    output << std::format("entity {}\n", ent.name);

    if (!ent.parent_name.empty()) {
        output << std::format("\tparent {}\n", ent.parent_name);
    }

    for (const auto& tag : ent.tags) {
        if (tag.value.empty()) {
            output << std::format("\t{}\n", tag.name);
        } else {
            output << std::format("\t{} {}\n", tag.name, tag.value);
        }

        for (const auto& [key, value] : tag.props) {
            output << std::format("\t\t{} {}\n", key, value);
        }
    }
}

}  // namespace vw::asset


namespace vw::asset {

namespace detail {
constexpr log::log_category voxa_serializer_lc{"voxa_serializer"};
}  // namespace detail

voxa_serializer::voxa_serializer(const animation_clip& clip) : clip_(&clip) {}

auto voxa_serializer::serialize(
    const std::filesystem::path& filepath
) -> std::expected<void, error_type> {
    std::ofstream file(filepath.string(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn(detail::voxa_serializer_lc, "failed to open file for writing: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    write_header_(file);

    for (const auto& track : clip_->get_tracks()) {
        write_track_(file, track);
    }

    if (!file.good()) {
        log::warn(detail::voxa_serializer_lc, "write error for file: {}", filepath.string());
        return std::unexpected(error_type::write_failed);
    }

    return {};
}

auto voxa_serializer::write_header_(std::ofstream& file) -> void {
    file << std::format("# Voxa File Version {}\n", voxa_file_version);
    if (!clip_->get_rig().empty()) {
        file << std::format("rig {}\n", clip_->get_rig());
    }

    for (const auto& event : clip_->get_events()) {
        if (event.payload.empty()) {
            file << std::format("event {:.6g} {}\n", event.time, event.name);
        } else {
            file << std::format("event {:.6g} {} {}\n", event.time, event.name, event.payload);
        }
    }
}

auto voxa_serializer::write_track_(
    std::ofstream& file, const animation_track& track
) -> void {
    file << std::format("\ntrack {} {:.6g}\n", track.get_target_name(), track.get_fps());

    for (const auto& channel : track.get_channels()) {
        write_channel_(file, channel);
    }
}

auto voxa_serializer::write_channel_(
    std::ofstream& file, const animation_channel_variant& channel
) -> void {
    std::visit(
        [&](const auto& ch) {
            std::string_view prop_name;
            switch (ch.get_property()) {
                case animation_property::position: prop_name = "position"; break;
                case animation_property::rotation: prop_name = "rotation"; break;
                case animation_property::scale: prop_name = "scale"; break;
            }
            file << std::format("  channel {}\n", prop_name);

            using channel_type = std::decay_t<decltype(ch)>;
            if constexpr (std::is_same_v<channel_type, animation_channel<vec3f>>) {
                write_keyframes_vec3f_(file, ch);
            } else if constexpr (std::is_same_v<channel_type, animation_channel<quat>>) {
                write_keyframes_quat_(file, ch);
            }
        },
        channel
    );
}

auto voxa_serializer::write_keyframes_vec3f_(
    std::ofstream& file, const animation_channel<vec3f>& ch
) -> void {
    for (const auto& kf : ch.get_keyframes()) {
        file << std::format(
            "    k {:.6g} {:.6g} {:.6g} {:.6g} {} {:.6g} {:.6g}\n",
            kf.time, kf.value.x, kf.value.y, kf.value.z,
            detail::interp_to_text(kf.interp), kf.tangent_in, kf.tangent_out
        );
    }
}

auto voxa_serializer::write_keyframes_quat_(
    std::ofstream& file, const animation_channel<quat>& ch
) -> void {
    for (const auto& kf : ch.get_keyframes()) {
        file << std::format(
            "    k {:.6g} {:.6g} {:.6g} {:.6g} {:.6g} {} {:.6g} {:.6g}\n",
            kf.time, kf.value.x, kf.value.y, kf.value.z, kf.value.w,
            detail::interp_to_text(kf.interp), kf.tangent_in, kf.tangent_out
        );
    }
}

}  // namespace vw::asset


namespace vw::asset {

namespace detail {
constexpr log::log_category voxa_deserializer_lc{"voxa_deserializer"};
}  // namespace detail

auto voxa_deserializer::deserialize(
    const std::filesystem::path& filepath
) -> std::expected<std::shared_ptr<animation_clip>, error_type> {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log::warn(detail::voxa_deserializer_lc, "failed to open file: {}", filepath.string());
        return std::unexpected(error_type::file_open_failed);
    }

    auto result = deserialize(file);
    if (!result) {
        log::warn(detail::voxa_deserializer_lc, "parse error in file: {}", filepath.string());
        return result;
    }

    (*result)->set_name(filepath.stem().string());
    return result;
}

auto voxa_deserializer::deserialize(
    std::istream& input
) -> std::expected<std::shared_ptr<animation_clip>, error_type> {
    clip_ = std::make_shared<animation_clip>(std::string{});
    current_track_ = nullptr;
    has_current_channel_ = false;
    error_ = std::nullopt;
    rig_.clear();
    vec3f_keyframes_.clear();
    quat_keyframes_.clear();

    std::string line;
    while (std::getline(input, line)) {
        if (error_.has_value()) break;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd.empty()) {
            continue;
        }

        if (cmd[0] == '#') {
            process_comment_(iss);
            continue;
        }

        if (cmd == "rig") {
            process_rig_(iss);
        } else if (cmd == "track") {
            process_track_(iss);
        } else if (cmd == "channel") {
            process_channel_(iss);
        } else if (cmd == "k") {
            process_keyframe_(iss);
        } else if (cmd == "event") {
            process_event_(iss);
        } else {
            log::warn(detail::voxa_deserializer_lc, "unknown command: {}", cmd);
        }
    }

    if (error_.has_value()) {
        return std::unexpected(*error_);
    }

    finalize_channel_();
    finalize_track_();

    if (!rig_.empty()) {
        clip_->set_rig(rig_);
    }

    return clip_;
}

auto voxa_deserializer::process_comment_(std::istringstream& iss) -> void {
    const auto version = detail::read_header_version(iss);
    if (!version.has_value()) {
        return;
    }

    if (detail::major_version_differs(*version, voxa_file_version)) {
        log::warn(
            detail::voxa_deserializer_lc, "unsupported voxa version {}, expected {}", *version,
            voxa_file_version
        );
        error_ = error_type::unsupported_version;
    }
}

auto voxa_deserializer::process_rig_(std::istringstream& iss) -> void {
    std::string name;
    iss >> name;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }
    rig_ = name;
}

auto voxa_deserializer::process_event_(std::istringstream& iss) -> void {
    animation_event event;
    iss >> event.time >> event.name;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }

    iss >> event.payload;
    std::string extra;
    if (iss >> extra) {
        error_ = error_type::parse_error;
        return;
    }

    clip_->add_event(std::move(event));
}

auto voxa_deserializer::process_track_(std::istringstream& iss) -> void {
    finalize_channel_();
    finalize_track_();

    std::string target_name;
    float32 fps = 60.0f;
    iss >> target_name >> fps;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }

    current_track_ = std::make_unique<animation_track>(target_name, fps);
}

auto voxa_deserializer::process_channel_(std::istringstream& iss) -> void {
    finalize_channel_();

    std::string prop_name;
    iss >> prop_name;
    if (iss.fail()) {
        error_ = error_type::parse_error;
        return;
    }

    if (prop_name == "position") {
        current_property_ = animation_property::position;
        current_channel_is_quat_ = false;
    } else if (prop_name == "rotation") {
        current_property_ = animation_property::rotation;
        current_channel_is_quat_ = true;
    } else if (prop_name == "scale") {
        current_property_ = animation_property::scale;
        current_channel_is_quat_ = false;
    } else if (prop_name == "origin") {
        log::warn(detail::voxa_deserializer_lc, "channel 'origin' is obsolete and is skipped");
        has_current_channel_ = false;
        return;
    }

    has_current_channel_ = true;
}

auto voxa_deserializer::process_keyframe_(std::istringstream& iss) -> void {
    float32 time;
    iss >> time;

    if (current_channel_is_quat_) {
        keyframe<quat> kf;
        kf.time = time;
        std::string interp_str;
        iss >> kf.value.x >> kf.value.y >> kf.value.z >> kf.value.w;
        iss >> interp_str >> kf.tangent_in >> kf.tangent_out;
        if (iss.fail()) {
            error_ = error_type::parse_error;
            return;
        }
        kf.interp = detail::interp_from_text(interp_str);
        quat_keyframes_.push_back(kf);
    } else {
        keyframe<vec3f> kf;
        kf.time = time;
        std::string interp_str;
        iss >> kf.value.x >> kf.value.y >> kf.value.z;
        iss >> interp_str >> kf.tangent_in >> kf.tangent_out;
        if (iss.fail()) {
            error_ = error_type::parse_error;
            return;
        }
        kf.interp = detail::interp_from_text(interp_str);
        vec3f_keyframes_.push_back(kf);
    }
}

auto voxa_deserializer::finalize_channel_() -> void {
    if (!has_current_channel_ || !current_track_) {
        return;
    }

    if (current_channel_is_quat_) {
        animation_channel<quat> ch(current_property_);
        ch.set_keyframes(std::move(quat_keyframes_));
        current_track_->add<animation_property::rotation>(std::move(ch));
        quat_keyframes_.clear();
    } else {
        animation_channel<vec3f> ch(current_property_);
        ch.set_keyframes(std::move(vec3f_keyframes_));
        switch (current_property_) {
            case animation_property::position:
                current_track_->add<animation_property::position>(std::move(ch));
                break;
            case animation_property::scale:
                current_track_->add<animation_property::scale>(std::move(ch));
                break;
            default: break;
        }
        vec3f_keyframes_.clear();
    }

    has_current_channel_ = false;
}

auto voxa_deserializer::finalize_track_() -> void {
    if (!current_track_ || !clip_) {
        return;
    }

    clip_->add_track(std::move(*current_track_));
    current_track_ = nullptr;
}

}  // namespace vw::asset


namespace vw::asset {

namespace detail {
constexpr log::log_category asset_storage_lc{"asset_storage"};
}  // namespace detail

asset_storage::asset_storage(vox_parser& parser, model_library& library)
    : parser_(&parser), library_(&library) {}

auto asset_storage::load_prefab(
    std::string_view name, const asset_ref& ref
) -> void {
    auto result = parser_->parse(library_->path_of(ref));
    if (!result.has_value()) {
        log::warn(detail::asset_storage_lc, "failed to load prefab '{}': {}", name, ref.str());
        return;
    }

    for (const auto& ent : result->entities) {
        const auto source = ent.value_of("model");
        if (!source.empty()) {
            static_cast<void>(library_->load(asset_ref{source}));
        }
    }

    for (const auto& fsm_ref : result->fsm_refs) {
        load_fsm(fsm_ref);
    }

    prefabs_[std::string(name)] = std::move(*result);
}

auto asset_storage::get_prefab(std::string_view name) const -> const vox_prefab_data* {
    const auto it = prefabs_.find(name);
    return it != prefabs_.end() ? &it->second : nullptr;
}

auto asset_storage::load_clip(
    const asset_ref& ref
) -> std::shared_ptr<animation_clip> {
    const auto it = clips_.find(ref);
    if (it != clips_.end()) {
        return it->second;
    }

    voxa_deserializer deserializer;
    auto result = deserializer.deserialize(library_->path_of(ref));
    if (!result.has_value()) {
        log::warn(detail::asset_storage_lc, "failed to load clip: {}", ref.str());
        return nullptr;
    }

    return clips_.emplace(ref, std::move(*result)).first->second;
}

auto asset_storage::load_fsm(
    const asset_ref& ref
) -> void {
    if (machines_.contains(ref)) {
        return;
    }

    voxf_deserializer deserializer;
    auto result = deserializer.deserialize(library_->path_of(ref));
    if (!result.has_value()) {
        log::warn(detail::asset_storage_lc, "failed to load fsm: {}", ref.str());
        return;
    }

    for (const auto& state : result->states) {
        if (!state.clip.empty()) {
            static_cast<void>(load_clip(state.clip));
        }
    }

    machines_.emplace(ref, std::move(*result));
}

auto asset_storage::get_fsm(const asset_ref& ref) const -> const voxf_data* {
    const auto it = machines_.find(ref);
    return it != machines_.end() ? &it->second : nullptr;
}

auto asset_storage::make_fsm(const voxf_data& data) const -> animation_fsm {
    return build_fsm(data, [this](const asset_ref& ref) { return get_clip(ref); });
}

auto asset_storage::get_entity(
    std::string_view prefab, std::string_view entity_name
) const -> const vox_entity_data& {
    auto pit = prefabs_.find(prefab);
    for (const auto& ent : pit->second.entities) {
        if (ent.name == entity_name) {
            return ent;
        }
    }

    log::warn(detail::asset_storage_lc, "entity '{}' not found in prefab '{}'", entity_name, prefab);
    return pit->second.entities.front();
}

auto asset_storage::get_model(
    std::string_view prefab, std::string_view entity_name
) const -> std::shared_ptr<model> {
    const auto pit = prefabs_.find(prefab);
    if (pit == prefabs_.end()) {
        return nullptr;
    }

    for (const auto& ent : pit->second.entities) {
        if (ent.name == entity_name) {
            return library_->find(asset_ref{ent.value_of("model")});
        }
    }
    return nullptr;
}

auto asset_storage::get_clip(const asset_ref& ref) const -> std::shared_ptr<animation_clip> {
    const auto it = clips_.find(ref);
    return it != clips_.end() ? it->second : nullptr;
}

}  // namespace vw::asset
