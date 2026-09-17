export module vw.asset:serial.voxf;

import std;

import vw.core;
import :anim;
import :serial.ref;
import :serial.version;
import :serial.text;

export namespace vw::asset {

inline constexpr std::string_view voxf_file_version = "1.0";

enum class voxf_param_type : uint8 { real, integer, boolean, trigger };

struct voxf_param {
    std::string name;
    voxf_param_type type = voxf_param_type::real;
    float32 value        = 0.0F;

    [[nodiscard]] auto operator==(const voxf_param& other) const -> bool = default;
};

struct voxf_state {
    std::string name;
    asset_ref clip;
    animation_loop_mode loop_mode = animation_loop_mode::loop;
    float32 rate                  = 1.0F;
    transition fade_in;
    transition fade_out;
    std::vector<animation_fsm::transition_rule> transitions;

    [[nodiscard]] auto operator==(const voxf_state& other) const -> bool = default;
};

struct voxf_data {
    std::string rig;
    std::string entry_state;
    std::vector<voxf_param> params;
    std::vector<voxf_state> states;
    std::vector<animation_fsm::transition_rule> any_transitions;

    [[nodiscard]] auto operator==(const voxf_data& other) const -> bool = default;
};

using voxf_clip_resolver = std::function<std::shared_ptr<animation_clip>(const asset_ref&)>;

[[nodiscard]] auto build_fsm(const voxf_data& data, const voxf_clip_resolver& resolve)
    -> animation_fsm;

auto apply_defaults(const voxf_data& data, fsm_blackboard& board) -> void;

class voxf_serializer final {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    explicit voxf_serializer(const voxf_data& data);

    auto serialize(const std::filesystem::path& filepath) -> std::expected<void, error_type>;
    auto serialize(std::ostream& output) -> std::expected<void, error_type>;

private:
    auto write_header_(std::ostream& output) -> void;
    auto write_state_(std::ostream& output, const voxf_state& state) -> void;
    auto write_rule_(
        std::ostream& output, const animation_fsm::transition_rule& rule, std::string_view indent
    ) -> void;

    const voxf_data* data_;
};

class voxf_deserializer final {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    auto deserialize(const std::filesystem::path& filepath)
        -> std::expected<voxf_data, error_type>;

    auto deserialize(std::istream& input) -> std::expected<voxf_data, error_type>;

private:
    static constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

    auto process_line_(std::string_view line) -> void;
    auto process_version_(std::string_view text) -> void;

    auto process_top_(std::string_view name, std::string_view value) -> void;
    auto process_state_tag_(std::string_view name, std::string_view value) -> void;
    auto process_rule_prop_(std::string_view name, std::string_view value) -> void;

    auto process_param_(std::string_view value) -> void;

    [[nodiscard]] auto current_rules_() -> std::vector<animation_fsm::transition_rule>*;

    voxf_data data_;

    std::size_t state_index_ = no_index;
    std::size_t rule_index_  = no_index;

    bool in_any_ = false;

    std::optional<error_type> error_;
};

}  // namespace vw::asset
