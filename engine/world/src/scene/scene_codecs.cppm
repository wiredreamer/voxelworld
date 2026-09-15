export module vw.world:scene.codecs;

import std;

import vw.core;
import vw.asset;
import vw.ecs;
import :components;

export namespace vw::ecs {

class world;

// Порядок применения заявлен, а не получается из порядка регистрации: цель
// анимации берёт позу покоя из уже выставленного трансформа, и это зависимость,
// а не соседство строк в одной функции.
enum class apply_phase : uint8 { transform, general };

struct component_read {
    world& target;
    entity ent;
    asset::model_library& library;

    // Имя узла нужно только для сообщений: в мире у сущности имени нет.
    std::string_view node_name;

    // Все теги узла с этим именем разом: повтор тега — это список, и сокеты
    // приходят пачкой.
    std::span<const asset::vox_tag* const> tags;
};

struct component_write {
    world& source;
    entity ent;
    asset::vox_entity_data& out;
};

// Одно место, где компонент встречается с файлом. Чтение и запись
// регистрируются парой и по-другому не бывает: половина без второй молча теряет
// данные при следующем сохранении — ни ошибки, ни лога.
struct component_codec {
    std::string tag;
    apply_phase phase = apply_phase::general;

    std::function<void(const component_read&)> read;
    std::function<void(const component_write&)> write;

    // Компонент, про который этот кодек: его рантайм-идентификатор ставит
    // register_for<T>, и по нему реестр сам решает, звать ли write. Проверять
    // наличие руками в каждом write — верный способ однажды это забыть.
    uint32 component = 0;
};

class component_registry final {
public:
    // Встроенные кодеки регистрирует конструктор: реестр без них — это префаб
    // без трансформов, а не чистый лист.
    component_registry();

    template <typename T>
    auto register_for(component_codec codec) -> void {
        codec.component = component_id_of<T>();
        codecs_.push_back(std::move(codec));
    }

    [[nodiscard]] auto all() const -> std::span<const component_codec> {
        return codecs_;
    }

    [[nodiscard]] auto find(std::string_view tag) const -> const component_codec*;

private:
    std::vector<component_codec> codecs_;
};

// Реестр по умолчанию: кодеки описывают формат, а не состояние мира, поэтому
// заводить их по одному на мир незачем. Тому, кому нужен свой набор, никто не
// мешает собрать реестр и передать явно.
[[nodiscard]] auto default_components() -> component_registry&;

// Одна фаза одного узла. Десериализатор идёт по фазам через все узлы сразу:
// трансформы всего дерева обязаны стоять раньше, чем их прочтёт хоть один
// кодек общей фазы.
auto apply_node_phase(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs, apply_phase phase, std::span<const std::string> skip_tags = {}
) -> void;

// Узел целиком, все фазы по порядку. Тем же кодом префаб встаёт из файла и
// arena собирает часть персонажа — разойтись им больше негде.
auto apply_node(
    world& target, entity ent, const asset::vox_entity_data& data, asset::model_library& library,
    const component_registry& codecs = default_components(),
    std::span<const std::string> skip_tags = {}
) -> void;

// Узел из мира: каждый кодек, чей компонент на месте, дописывает свои теги.
auto extract_node(
    world& source, entity ent, asset::vox_entity_data& out,
    const component_registry& codecs = default_components()
) -> void;

}  // namespace vw::ecs
