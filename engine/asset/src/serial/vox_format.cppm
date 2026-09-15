export module vw.asset:serial.vox;

import std;

import vw.core;
import :model;
import :anim;
import :serial.ref;
import :serial.version;
import :serial.text;

export namespace vw::asset {

// Тег узла: имя, остаток строки за ним и блок «ключ → остаток строки». Значения
// не разобраны намеренно — сколько в строке чисел и что они значат, знает тот,
// кто тег читает. Поэтому весь .vox разбирает vw.asset, не зная ни одного
// компонента.
struct vox_tag {
    std::string name;
    std::string value;
    std::vector<std::pair<std::string, std::string>> props;

    // Пусто и «нет такого ключа» — один ответ: читателю в обоих случаях нечего
    // взять.
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

    // Порядок тегов сохраняется: повтор тега — это список (у кисти два сокета),
    // и запись обязана отдать их в том порядке, в каком прочла.
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

    // Что это за документ: character, structure — или пусто. Строкой, а не
    // перечислением, потому что из шести префабов пять предметы: меч не character
    // и не structure, и выдуманное третье слово было бы ответом, в который мы
    // сами не верим.
    //
    // В мир kind не едет: от него не меняется ни один компонент, он говорит
    // редактору, каким набором блоков открывать документ, а генератору — стоит
    // ли на него смотреть.
    std::string kind;

    // Имя рига в шапке — не список целей: цели выводятся из узлов, и сверка по
    // ним остаётся настоящей проверкой. Имя отвечает на другой вопрос — «этот
    // клип вообще про это существо?» — и отвечает, не читая узлов.
    std::string rig;

    // Автоматы существа, по одному на слой: порядок ссылок — это и есть номера
    // слоёв, поэтому повтор тега здесь не просто список, а список упорядоченный.
    std::vector<asset_ref> fsm_refs;

    std::vector<vox_entity_data> entities;

    [[nodiscard]] auto operator==(const vox_prefab_data& other) const -> bool = default;
};

// Текст → числа. Формат хранит строку целиком, а сколько в ней чисел, знает
// читатель тега; false — их оказалось меньше, чем просили.
[[nodiscard]] auto parse_floats(std::string_view text, std::span<float32> out) -> bool;

// Слова для kind в шапке. Их два, и оба лежат здесь, а не строками по
// приложениям: редактор пишет, генератор читает, и разойтись им незачем.
namespace kinds {
inline constexpr std::string_view character = "character";
inline constexpr std::string_view structure = "structure";
}  // namespace kinds

// 3.0 вынесло воксели из дерева: узел ссылается на .voxm, а точка вращения
// уехала в сам объём. 3.1 добавило в шапку имя рига. 4.0 сделало узел открытым
// списком тегов: свойства компонента идут блоком с отступом, повтор тега — это
// список, а незнакомый тег проносится нетронутым. Старшая цифра, потому что
// прежний плоский синтаксис сокетов новым разбором не читается.
inline constexpr std::string_view vox_file_version = "4.0";

// База для разборщиков формата .vox.
class vox_parser {
public:
    enum class error_type : uint8 { file_open_failed, parse_error, unsupported_version };

    virtual ~vox_parser() = default;

    virtual auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> = 0;
};

// Разборщик текстового варианта .vox.
class vox_parser_plain final : public vox_parser {
public:
    auto parse(const std::filesystem::path& filepath)
        -> std::expected<vox_prefab_data, error_type> override;

    // Разбор в отрыве от файловой системы: тем же путём идёт и файл, и буфер из
    // фаззера, и строка из теста. Ошибку открытия эта форма вернуть не может.
    auto parse(std::istream& input) -> std::expected<vox_prefab_data, error_type>;

private:
    static constexpr std::size_t no_index = std::numeric_limits<std::size_t>::max();

    auto process_line_(std::string_view line) -> void;
    auto process_version_(std::string_view text) -> void;

    // Структурных слов ровно четыре: entity открывает узел, parent держит
    // дерево, root и rig — шапку. Всё прочее уходит в теги, и про них разборщик
    // не знает ничего, кроме имени и глубины отступа.
    auto process_top_(std::string_view name, std::string_view value) -> void;
    auto process_tag_(std::string_view name, std::string_view value) -> void;
    auto process_prop_(std::string_view name, std::string_view value) -> void;

    vox_prefab_data prefab_;

    // Индексы, а не указатели: вектор узлов растёт по ходу разбора и переезжает
    // вместе со всеми тегами внутри.
    std::size_t entity_index_ = no_index;
    std::size_t tag_index_    = no_index;

    std::optional<error_type> error_;
};

// База для писателей формата .vox.
class vox_writer {
public:
    enum class error_type : uint8 { file_open_failed, write_failed };

    virtual ~vox_writer() = default;

    virtual auto write(const std::filesystem::path& filepath, const vox_prefab_data& prefab)
        -> std::expected<void, error_type> = 0;
};

// Писатель текстового варианта .vox. Стоит рядом с разборщиком намеренно: это
// две половины одного формата, и пока они жили в разных библиотеках, забытое
// поле в одной из них молча теряло данные при следующем сохранении.
class vox_writer_plain final : public vox_writer {
public:
    auto write(const std::filesystem::path& filepath, const vox_prefab_data& prefab)
        -> std::expected<void, error_type> override;

    // Запись в отрыве от файловой системы — пара к parse(std::istream&): тем же
    // путём идёт и файл, и буфер теста. Ошибку открытия эта форма вернуть не
    // может.
    auto write(std::ostream& output, const vox_prefab_data& prefab)
        -> std::expected<void, error_type>;

private:
    auto write_header_(std::ostream& output, const vox_prefab_data& prefab) -> void;
    auto write_entity_(std::ostream& output, const vox_entity_data& ent) -> void;
};

// 1.1 добавило в шапку имя рига: строка необязательная, файл 1.0 читается как
// клип без рига.
inline constexpr std::string_view voxa_file_version = "1.1";

}  // namespace vw::asset
