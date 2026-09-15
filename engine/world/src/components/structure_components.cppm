export module vw.world:components.structure;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

// Размер из PRD — не габариты объёма, а полка, по которой генератор подбирает
// структуру под место. Габариты он и так посчитает, а «дом это S или M» — вопрос
// к автору.
enum class structure_size : uint8 { unspecified, small, medium, large, extra_large };

// Метаданные структуры для генератора мира. Лежат тегом на корне, как обычный
// компонент: после таблицы кодеков Architect — это несколько компонентов и тип
// документа, а не второй редактор.
//
// Тип и раса — открытые словари строк: их состав знает генератор, а не формат, и
// зашивать сюда перечисление значило бы менять движок ради новой расы.
struct structure_component final {
    [[nodiscard]] auto get_type() const -> const std::string& {
        return type_;
    }

    [[nodiscard]] auto get_races() const -> std::span<const std::string> {
        return races_;
    }

    // Уровень поселения, I—V из PRD. Ноль — не сказано.
    [[nodiscard]] auto get_tier() const -> uint8 {
        return tier_;
    }

    [[nodiscard]] auto get_size() const -> structure_size {
        return size_;
    }

private:
    friend class structure_system;

    std::string type_;
    std::vector<std::string> races_;
    uint8 tier_          = 0;
    structure_size size_ = structure_size::unspecified;
};

// Место под мебель: узел с трансформом и словом о том, что здесь может стоять.
// Отдельный компонент, а не сокет: сокет держит то, что уже прикреплено, а это
// приглашение генератору, и адресатов у них разные.
struct furniture_point_component final {
    [[nodiscard]] auto get_category() const -> const std::string& {
        return category_;
    }

private:
    friend class structure_system;

    std::string category_;
};

// Точка стыка с соседней структурой. Профиль — то, с чем она сходится: дверь
// сходится с дверью, коридор с коридором. Куда она смотрит, говорит трансформ
// узла, поэтому направления здесь нет.
struct connection_point_component final {
    [[nodiscard]] auto get_profile() const -> const std::string& {
        return profile_;
    }

private:
    friend class structure_system;

    std::string profile_;
};

}  // namespace vw::ecs
