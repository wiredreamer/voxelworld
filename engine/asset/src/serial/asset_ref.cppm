export module vw.asset:serial.ref;

import std;

import vw.core;

export namespace vw::asset {

// Раскладка от корня ассетов. Ссылка внутри файла на неё не смотрит — она несёт
// готовый путь, — но правило имени безымянного объёма и списки в редакторе
// обязаны сходиться, поэтому имена каталогов лежат в одном месте, а не строками
// по приложениям.
namespace dirs {
inline constexpr std::string_view prefabs    = "prefabs";
inline constexpr std::string_view models     = "models";
inline constexpr std::string_view animations = "animations";
inline constexpr std::string_view fsm        = "fsm";
}  // namespace dirs

// Путь к ассету от корня ассетов — не путь на диске. Корень знает приложение: у
// редактора он свой, у игры свой. Поэтому перетасовка папок остаётся правкой
// ссылок, а не сменой формата.
class asset_ref final {
public:
    asset_ref() = default;
    explicit asset_ref(std::string_view path);

    [[nodiscard]] auto empty() const -> bool {
        return path_.empty();
    }

    [[nodiscard]] auto str() const -> const std::string& {
        return path_;
    }

    // Расширение вместе с точкой: по нему слот варианта отличит подмену объёма
    // от подстановки поддерева, не заводя в ссылке поля типа.
    [[nodiscard]] auto extension() const -> std::string_view;
    [[nodiscard]] auto stem() const -> std::string_view;

    [[nodiscard]] auto operator==(const asset_ref& other) const -> bool = default;

private:
    std::string path_;
};

// Куда ложится объём узла, у которого своей ссылки в файле нет: в dirs::models,
// в папку по имени префаба. Правило общее для конвертации старых .vox и для
// первой записи нового узла — иначе один объём получит два имени.
[[nodiscard]] auto default_model_ref(const asset_ref& prefab, std::string_view entity_name)
    -> asset_ref;

}  // namespace vw::asset

export template <>
struct std::hash<vw::asset::asset_ref> {
    auto operator()(const vw::asset::asset_ref& ref) const noexcept -> std::size_t {
        return std::hash<std::string>{}(ref.str());
    }
};
