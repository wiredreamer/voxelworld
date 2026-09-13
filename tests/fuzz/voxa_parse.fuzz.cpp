// Фаззер .voxa — то же, что и для .vox, но площадь другая: здесь разбираются
// числа с плавающей точкой, имена интерполяций и ключевые кадры, которые потом
// складываются в дорожки. Пустой клип и клип без единого кадра — законные
// исходы, падение и порча памяти — нет.

import std;

import vw.core;
import vw.asset;

namespace {

// Куда стекает результат: без потребителя оптимизатор вправе выбросить разбор
// целиком, и фаззер измерял бы пустой цикл.
volatile vw::uint32 sink = 0;

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const vw::uint8* data, std::size_t size) -> int {
    // Лог гасится разом на весь прогон: почти каждый вход для разборщика —
    // неизвестная команда, и запись о ней стоит дороже самого разбора, а вывод
    // фаззера топит целиком.
    static const bool log_silenced = [] {
        vw::log::set_level(vw::log::level::off);
        return true;
    }();
    static_cast<void>(log_silenced);

    std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size));

    vw::asset::voxa_deserializer deserializer;
    const auto result = deserializer.deserialize(stream);

    if (result.has_value() && *result) {
        sink = static_cast<vw::uint32>((*result)->get_tracks().size());
    }

    return 0;
}
