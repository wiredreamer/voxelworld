export module vw.asset:anim.fsm;

import std;

import vw.core;
import :anim.keyframe;
import :anim.channel;
import :anim.clip;

export namespace vw::asset {

// Независимый проигрыватель одного клипа с маской целей: смешивается с клипом,
// который заменил, и появляется или гаснет целиком.
struct animation_layer {
    std::shared_ptr<animation_clip> clip;
    animation_state state         = animation_state::stopped;
    float32 time                  = 0.0F;
    float32 playback_speed        = 1.0F;
    animation_loop_mode loop_mode = animation_loop_mode::once;
    float32 direction             = 1.0F;

    std::shared_ptr<animation_clip> blend_prev_clip;
    float32 blend_prev_time           = 0.0F;
    float32 blend_prev_playback_speed = 1.0F;
    float32 blend_prev_direction      = 1.0F;
    float32 blend_elapsed             = 0.0F;
    transition blend_transition;
    std::unordered_map<std::string, transform> blend_snapshot;

    float32 fade_influence = 0.0F;
    float32 fade_elapsed   = 0.0F;
    transition fade_in;
    transition fade_out;
    bool fade_is_out = false;

    std::unordered_set<std::string> mask;

    [[nodiscard]] auto is_active() const -> bool {
        if (state == animation_state::playing || fade_is_out) {
            return true;
        }
        return blend_transition.duration > 0.0F && blend_elapsed < blend_transition.duration;
    }

    [[nodiscard]] auto is_blending() const -> bool {
        return blend_prev_clip && blend_elapsed < blend_transition.duration;
    }

    [[nodiscard]] auto affects_target(const std::string& name) const -> bool {
        return mask.contains(name);
    }
};

// Доска параметров: плоский список пар. Их единицы, линейный поиск по ним
// дешевле хеша, и порядок в файле совпадает с порядком здесь.
//
// Тип один — число. Булево живёт здесь же нулём и единицей, целое точным
// значением: отдельные типы стоили бы варианта в каждом сравнении ради того,
// что и так помещается.
class fsm_blackboard final {
public:
    auto set(std::string_view name, float32 value) -> void;

    // Нет параметра — ноль, а не отказ: условие по незаполненному параметру
    // просто не выполняется, и автомат остаётся там, где стоял.
    [[nodiscard]] auto get(std::string_view name) const -> float32;

    [[nodiscard]] auto entries() const -> std::span<const std::pair<std::string, float32>> {
        return values_;
    }

private:
    std::vector<std::pair<std::string, float32>> values_;
};

enum class fsm_compare : uint8 {
    equal,
    not_equal,
    less,
    less_equal,
    greater,
    greater_equal,
};

// Условие перехода — данные, а не лямбда: лямбду не записать в файл, и до этого
// автомат мог существовать только в коде.
struct fsm_condition {
    std::string parameter;
    fsm_compare compare = fsm_compare::greater;
    float32 value       = 0.0F;

    [[nodiscard]] auto holds(const fsm_blackboard& board) const -> bool;

    [[nodiscard]] auto operator==(const fsm_condition& other) const -> bool = default;
};

class animation_fsm final {
public:
    struct transition_rule {
        std::string target_state;

        // Все условия разом: «или» выражается двумя переходами в одно
        // состояние, и читается это лучше, чем дерево из скобок в файле.
        std::vector<fsm_condition> conditions;

        std::string trigger_name;
        transition blend;
        bool wait_until_end   = false;
        bool wait_until_blend = false;

        [[nodiscard]] auto operator==(const transition_rule& other) const -> bool = default;
    };

    struct state_node {
        std::string name;
        std::shared_ptr<animation_clip> clip;
        animation_loop_mode loop_mode = animation_loop_mode::loop;
        float32 playback_speed        = 1.0F;
        transition layer_blend_in;
        transition layer_blend_out;
        std::vector<transition_rule> transitions;
    };

    struct transition_result {
        std::string target_state;
        std::shared_ptr<animation_clip> clip;
        animation_loop_mode loop_mode;
        float32 playback_speed;
        transition blend;
        transition layer_blend_in;
        transition layer_blend_out;
    };

    using trigger_set = std::unordered_set<std::string>;

    auto add_state(state_node state) -> void;
    auto set_entry_state(std::string_view name) -> void;

    // Переход из любого состояния. Проверяется раньше правил текущего: правило,
    // объявленное для всех состояний, — это и есть «важнее того, что сейчас».
    auto add_any_transition(transition_rule rule) -> void;

    [[nodiscard]] auto get_current_state() const -> const std::string& {
        return current_state_;
    }

    [[nodiscard]] auto get_current_state_node() const -> const state_node*;
    [[nodiscard]] auto evaluate(
        const animation_layer& layer, trigger_set& triggers, const fsm_blackboard& board
    ) -> std::optional<transition_result>;

    auto apply_transition(const transition_result& result) -> void;

private:
    [[nodiscard]] auto match_(
        const transition_rule& rule, const animation_layer& layer, trigger_set& triggers,
        const fsm_blackboard& board
    ) const -> bool;

    [[nodiscard]] auto result_of_(const transition_rule& rule) const
        -> std::optional<transition_result>;

    std::string current_state_;
    std::string entry_state_;
    std::unordered_map<std::string, state_node> states_;
    std::vector<transition_rule> any_transitions_;
};
}  // namespace vw::asset
