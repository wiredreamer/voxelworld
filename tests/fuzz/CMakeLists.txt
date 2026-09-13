# Фаззеры разборщиков ассетов.
#
# Собираются только Clang'ом: libFuzzer — его санитайзер, у MSVC аналога нет.
# Смысл они имеют лишь вместе с ASan и UBSan, поэтому флаги санитайзеров
# ожидаются снаружи (см. джоб `fuzz` в .github/workflows/analysis.yml), а здесь
# добавляется только сам движок фаззинга.
#
# Корпус не хранится отдельно: `assets/` уже содержит по-настоящему валидные
# файлы обоих форматов, и они же служат затравкой.
#
# Затравку libFuzzer'у передают ВТОРЫМ аргументом и позже: первый каталог он
# считает рабочим корпусом и складывает туда всё новое, что нашёл. Указать
# первым `assets/` — значит засыпать репозиторий тысячами файлов со случайными
# именами.
#
# На Windows собирается, но требует -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded:
# libFuzzer от LLVM собран под статический рантайм, и линковка с обычной
# сборкой падает на /failifmismatch. В CI фаззеры идут на Linux, где этого
# расхождения нет.

function(vw_add_fuzzer name source)
    add_executable(${name} ${source})

    target_link_libraries(${name} PRIVATE vw_core vw_asset)
    target_compile_features(${name} PRIVATE cxx_std_23)
    vw_use_std_module(${name})

    # На таргет, а не в общие флаги: -fsanitize=fuzzer подменяет main, и любой
    # другой исполняемый файл с ним перестал бы запускаться.
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)

    vw_set_warnings(${name})
endfunction()

vw_add_fuzzer(vox_parse_fuzzer vox_parse.fuzz.cpp)
vw_add_fuzzer(voxa_parse_fuzzer voxa_parse.fuzz.cpp)

# Прогон затравки как обычный тест: он не ищет новых путей, но ловит то, из-за
# чего фаззер не запустился бы вовсе — от несобравшегося корпуса до падения на
# первом же валидном файле.
add_test(NAME fuzz_vox_seed_corpus
    COMMAND vox_parse_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/assets/models
)
add_test(NAME fuzz_voxa_seed_corpus
    COMMAND voxa_parse_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/assets/animations
)
