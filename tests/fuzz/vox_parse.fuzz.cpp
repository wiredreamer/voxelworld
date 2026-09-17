// Фаззер текстового .vox: разбор идёт из памяти, поэтому файловая система в
// цикле не участвует и прогон измеряется десятками тысяч запусков в секунду.
//
// Ищется не «неверный ввод» — на него разборщик обязан вернуть parse_error, и
// это штатный исход. Ищется всё остальное: выход за границы, чтение
// освобождённого, знаковое переполнение в разборе чисел, необработанное
// исключение. Их видят санитайзеры, под которыми фаззер и собирается.

import std;

import vw.core;
import vw.asset;

namespace {

// Куда стекает результат: без потребителя оптимизатор вправе выбросить разбор
// целиком, и фаззер измерял бы пустой цикл.
volatile vw::uint32 sink = 0;

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const vw::uint8* data, std::size_t size) -> int {
    // Реестр строится один раз на процесс: он неизменен, а его конструктор
    // заметно дороже самого разбора.
    static const vw::voxel_registry registry;

    // Лог гасится там же, разом на весь прогон: почти каждый вход для
    // разборщика — неизвестная команда, и запись о ней стоит дороже самого
    // разбора, а вывод фаззера топит целиком.
    static const bool log_silenced = [] {
        vw::log::set_level(vw::log::level::off);
        return true;
    }();
    static_cast<void>(log_silenced);

    std::istringstream stream(std::string(reinterpret_cast<const char*>(data), size));

    vw::asset::vox_parser_plain parser;
    const auto result = parser.parse(stream);

    if (result.has_value()) {
        sink = static_cast<vw::uint32>(result->entities.size());
    }

    return 0;
}
