export module vw.asset:serial.library;

import std;

import vw.core;
import :model;
import :serial.ref;
import :serial.voxm;

export namespace vw::asset {

// Объёмы, загруженные по ссылке. Ключ — сама ссылка, а не имя узла: один .voxm,
// на который смотрят два узла, лежит в памяти один раз, и два узла `head` из
// разных префабов перестают быть одной записью.
class model_library final {
public:
    using load_error = voxm_deserializer::error_type;
    using save_error = voxm_serializer::error_type;

    model_library(
        model_registry& registry, const block_registry& blocks, std::filesystem::path root
    );

    [[nodiscard]] auto load(const asset_ref& ref)
        -> std::expected<std::shared_ptr<model>, load_error>;

    [[nodiscard]] auto save(const asset_ref& ref, const model& volume)
        -> std::expected<void, save_error>;

    // Объём, собранный не из файла — новый узел редактора или .vox 2.0 с
    // вокселями внутри дерева, — встаёт под своей ссылкой сразу. Иначе
    // следующая загрузка того же пути прочитает его с диска вторым экземпляром,
    // и правки разойдутся по двум копиям.
    auto adopt(const asset_ref& ref, std::shared_ptr<model> volume) -> void;

    [[nodiscard]] auto find(const asset_ref& ref) const -> std::shared_ptr<model>;

    [[nodiscard]] auto path_of(const asset_ref& ref) const -> std::filesystem::path;

    [[nodiscard]] auto root() const -> const std::filesystem::path& {
        return root_;
    }

    [[nodiscard]] auto registry() const -> model_registry& {
        return *registry_;
    }

private:
    model_registry* registry_;
    const block_registry* blocks_;
    std::filesystem::path root_;
    std::unordered_map<asset_ref, std::shared_ptr<model>> loaded_;
};

}  // namespace vw::asset
