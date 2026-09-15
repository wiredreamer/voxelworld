export module vw.asset:serial.voxf;

import std;

import vw.core;
import :anim;
import :serial.ref;
import :serial.version;
import :serial.text;

export namespace vw::asset {

// 1.0 — первый формат, в котором автомат живёт отдельно от кода. До него он
// существовал только как вызовы add_state в приложении.
inline constexpr std::string_view voxf_file_version = "1.0";

// Тип параметра — факт про авторство, а не про хранение: на доске всё лежит
// одним числом. Тип говорит редактору, что рисовать — галочку, целое или
// дробное, — и читателю файла, что значит «1» в grounded.
enum class voxf_param_type : uint8 { real, integer, boolean, trigger };

// Объявление параметра. Исполнению оно не нужно — незаполненный параметр и так
// читается нулём, — но объявленный виден в отладочном окне с первого кадра, а
// не с того, в котором его впервые записали.
struct voxf_param {
    std::string name;
    voxf_param_type type = voxf_param_type::real;
    float32 value        = 0.0F;

    [[nodiscard]] auto operator==(const voxf_param& other) const -> bool = default;
};

// Состояние из файла. От animation_fsm::state_node отличается одним полем:
// клип здесь ссылка, потому что разобрать текст и загрузить клип — разные
// работы, и вторую делает тот, у кого есть библиотека.
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

// Файл — один автомат, то есть один слой. Номер слоя в файле не пишется: его
// задаёт порядок ссылок в .vox, и тогда один и тот же автомат действия годится
// нескольким существам, у которых локомоция разная.
struct voxf_data {
    std::string rig;
    std::string entry_state;
    std::vector<voxf_param> params;
    std::vector<voxf_state> states;
    std::vector<animation_fsm::transition_rule> any_transitions;

    [[nodiscard]] auto operator==(const voxf_data& other) const -> bool = default;
};

// Ссылки на клипы резолвит тот, у кого есть библиотека. Не нашёлся — состояние
// останется без клипа: слой на нём молчит, но автомат работает, и видно это
// сразу, а не падением загрузки.
using voxf_clip_resolver = std::function<std::shared_ptr<animation_clip>(const asset_ref&)>;

[[nodiscard]] auto build_fsm(const voxf_data& data, const voxf_clip_resolver& resolve)
    -> animation_fsm;

// Параметры из объявлений на доску. Отдельно от build_fsm: автомат и доска
// живут порознь — доска одна на все слои существа, автоматов на нём несколько.
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

    // Разбор в отрыве от файловой системы: тем же путём идёт и файл, и буфер из
    // фаззера, и строка из теста. Ошибку открытия эта форма вернуть не может.
    auto deserialize(std::istream& input) -> std::expected<voxf_data, error_type>;

private:
    static constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

    auto process_line_(std::string_view line) -> void;
    auto process_version_(std::string_view text) -> void;

    // Структурных слов два: state открывает состояние, any — список переходов,
    // которые важнее текущего состояния. Остальное на верхнем уровне — шапка.
    auto process_top_(std::string_view name, std::string_view value) -> void;
    auto process_state_tag_(std::string_view name, std::string_view value) -> void;
    auto process_rule_prop_(std::string_view name, std::string_view value) -> void;

    auto process_param_(std::string_view value) -> void;

    // Правило ложится либо в состояние, либо в список «из любого»: указатель
    // хранить нельзя — векторы растут по ходу разбора.
    [[nodiscard]] auto current_rules_() -> std::vector<animation_fsm::transition_rule>*;

    voxf_data data_;

    std::size_t state_index_ = no_index;
    std::size_t rule_index_  = no_index;

    // Открыт блок any: правила идут в data_.any_transitions, а не в состояние.
    bool in_any_ = false;

    std::optional<error_type> error_;
};

}  // namespace vw::asset
