---
name: cmake-build
description: Сборка и CI voxelworld — генератор Ninja и окружение vcvars, таргеты vw_core/vw_asset/vw_net/vw_ecs/vw_world/vw_platform/vw_gfx/vwengine, опции VW_BUILD_*, подключение нового модульного юнита, тесты и headless-конфигурация без Vulkan, гейт import std при обновлении CMake, доставка шейдеров и ассетов, предупреждения, джобы GitHub Actions (санитайзеры, фаззеры, покрытие, Linux на libc++, релиз) и vcpkg-триплет. Читай при правке CMakeLists.txt, cmake/*.cmake, cmake/triplets/*, vcpkg.json, .github/workflows/*.yml и перед запуском сборки или тестов.
---

# Сборка voxelworld

## Обязательные условия

**Только генератор Ninja.** Visual Studio не умеет `import std`; корневой
`CMakeLists.txt` падает с ошибкой на любом другом генераторе.

**Только из окружения Developer Command Prompt** (нужна переменная
`VCToolsInstallDir`, по ней ищется `std.ixx` для Clang-ветки). Из обычной
оболочки — через `vcvars64.bat`:

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build build/release'
```

Отдельные каталоги сборки на конфигурацию: `build/release`, `build/debug`,
`build/tests`, `build/headless` — не голый `build/`.

## Команды

```
# полная сборка
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release

# тесты
cmake -S . -B build/tests -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/Users/lucius/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows -DVW_BUILD_APPS=OFF
cmake --build build/tests --target core_tests asset_tests net_tests ecs_tests world_tests
ctest --test-dir build/tests --output-on-failure
ctest --test-dir build/tests -R core        # или -R ecs, -R world

# headless: ни Vulkan, ни GLFW, ни imgui — ни в линковке, ни в vcpkg
cmake -S . -B build/headless -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DVW_BUILD_GFX=OFF -DVW_BUILD_APPS=OFF -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON \
      -DCMAKE_TOOLCHAIN_FILE=C:/Users/lucius/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows

# Clang вместо MSVC: -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang
# (драйвер clang++, не clang-cl: сканирование графа импортов CMake включает
#  только для GNU-подобного фронтенда, с clang-cl конфигурация падает)
```

## Таргеты и опции

| Таргет | Что это |
|---|---|
| `vw_core` | модуль `vw.core` (`engine/core/`) |
| `vw_asset` | модуль `vw.asset` (`engine/asset/`); линкуется только с `vw_core` |
| `vw_net` | модуль `vw.net` (`engine/net/`); линкуется только с `vw_core`, на Windows тянет `ws2_32` |
| `vw_ecs` | модуль `vw.ecs` (`engine/ecs/`) |
| `vw_world` | модуль `vw.world` (`engine/world/`) |
| `vw_platform` | модуль `vw.platform` (`engine/platform/`); только при `VW_BUILD_GFX=ON` |
| `vw_gfx` | модуль `vw.gfx` (`engine/gfx/`) поверх `VulkanHppModule`; только при `VW_BUILD_GFX=ON` |
| `vwengine` | INTERFACE-набор всех семи модулей, на него линкуются приложения; только при `VW_BUILD_GFX=ON` |
| `core_tests` `asset_tests` `net_tests` `ecs_tests` `world_tests` | тесты Catch2; линкуются на модульные таргеты, никогда на `vwengine` |
| `gfx_tests` | тесты мешера и камеры на CPU; только при `VW_BUILD_GFX=ON` |
| `view_bench` | микробенчмарк обхода ECS (регрессионный сторож из M2) |
| `vox_parse_fuzzer` `voxa_parse_fuzzer` `json_parse_fuzzer` `http_head_fuzzer` | фаззеры разборщиков; только при `VW_BUILD_FUZZERS=ON`, см. «Фаззинг» |

Опции: `VW_BUILD_GFX` (по умолчанию ON), `VW_BUILD_APPS`, `VW_BUILD_TESTS`,
`VW_BUILD_FUZZERS` (OFF, только Clang), `VW_WARNINGS_AS_ERRORS` (OFF),
`VW_LOG_MIN_LEVEL`, `ENABLE_CLANG_TIDY` (таргет `sculptor_clang_tidy`).
`VW_BUILD_APPS=ON` при `VW_BUILD_GFX=OFF` — ошибка конфигурации: приложениям
нужно окно.

`VW_SCULPTOR_MCP` (ON) — собирать ли в Sculptor сервер MCP: при ON в сборку идут
`src/mcp/*.cpp` и на Windows линкуется `ws2_32`, при OFF — один
`src/mcp/server_disabled.cpp`. Слушает ли сервер порт, решает флаг запуска
`--mcp`, а не сборка (`docs/mcp.md`). CI собирает только ON.

`VW_SCULPTOR_ASSET_ROOT` (по умолчанию `${CMAKE_SOURCE_DIR}/assets`) — каталог,
который Sculptor открывает и в который пишет: `prefabs/`, `models/`,
`animations/` и `fsm/` ищутся в нём (имена — `vw::asset::dirs`). Редактор правит
ассеты репозитория, поэтому `git status` показывает правку сразу; сборке «на
раздачу» нужно поставить `.`, и корнем станет каталог рядом с исполняемым
файлом. Тип в командной строке обязателен —
`-DVW_SCULPTOR_ASSET_ROOT:STRING=.`: у кэш-записи тип `PATH`, и без него CMake
развернёт `.` в абсолютный путь сборочной машины, а сборка молча останется с ним.
Арене ассеты копирует `vw_setup_assets` (см. «Шейдеры и ассеты») — она их только
читает.

Зависимости gfx (glfw3, imgui, vulkan-headers, stb) вынесены в vcpkg-фичу `gfx`,
включённую по умолчанию; `-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` оставляет
только Catch2.

## Как подключить новый файл модуля

Интерфейсные и внутренние партиции идут в `FILE_SET CXX_MODULES`,
имплементационные юниты — в `PRIVATE`:

```cmake
target_sources(vw_world
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/world.cppm
            src/components/components.cppm   # агрегатор партиций
            src/components/light_component.cppm
            src/systems/hooks.cppm           # внутренняя партиция — тоже сюда
    PRIVATE
        src/world.cpp                        # module vw.world;
        src/systems/light_system.cpp
)
```

`BASE_DIRS src` остаётся неизменным: подкаталоги внутри `src/` в путях файлов.

Каждой модульной библиотеке нужны `vw_use_std_module(<target>)` и
`vw_set_warnings(<target>)`. Первый включает `CXX_MODULE_STD` (MSVC, Clang на
Linux), а для Clang с ABI MSVC линкует `vw_std` — std-модуль из `std.ixx`
MS STL (`cmake/vw_std_module.cmake`).

`cmake/vw_vulkan_module.cmake` даёт `vw_add_vulkan_module()` — таргет
`VulkanHppModule` для `import vulkan` из `vulkan/vulkan.cppm` Vulkan-Headers;
вызывается в `engine/gfx/CMakeLists.txt`. Макросы `VULKAN_HPP_*` задаются
PUBLIC только на этом таргете.

## Настройки, доходящие до кода

Только как экспортированный `constexpr`, не как макрос в заголовке потребителя:
cache-переменная → `target_compile_definitions(... PRIVATE ...)` →
`inline constexpr` в партиции. Образец — `VW_LOG_MIN_LEVEL` → `vw::log::min_level`
в `engine/core/CMakeLists.txt` и `engine/core/src/log/log.cppm`.

Так же до кода доходит конфигурация сборки: `CMAKE_BUILD_TYPE` →
`VW_BUILD_CONFIG` → `vw::build::config` (`engine/core/src/utils/build_info.cppm`).
`vw::build::titled(имя)` дописывает её к заголовку окна — `Sculptor 0.2.0
[Debug]`; все три приложения зовут его, а Sculptor ещё и при каждой смене
заголовка. Отчёт замера берёт строку `build:` оттуда же, поэтому
`RelWithDebInfo` в нём больше не выдаёт себя за `Release`.

## Обновление CMake

`import std` открывается экспериментальным гейтом
`CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`, и CMake принимает только точный UUID своего
релиза. Таблица — лестница `if(CMAKE_VERSION VERSION_LESS …)` в начале корневого
`CMakeLists.txt`.

- Новая версия CMake — добавь ветку со значением из раздела `import std` файла
  `Help/dev/experimental.rst` этой версии. CMake новее таблицы останавливается
  на `FATAL_ERROR` с той же подсказкой.
- Неверный UUID в ветке молча закрывает гейт, и падает уже генерация:
  «CXX_MODULE_STD requires toolchain support» — про гейт в ошибке ни слова.
  Гейт читают только таргеты с `CXX_MODULE_STD`, поэтому Clang на Windows при
  этом собирается, а MSVC — нет.
- Отрицательный результат детекции кэшируется в
  `build/<dir>/CMakeFiles/<версия>/CMakeCXXCompiler.cmake`: после правки таблицы
  удали `CMakeFiles/<версия>/` в каждом затронутом каталоге сборки, иначе ошибка
  остаётся. Локально CMake два — из PATH и встроенный в CLion; каталог сборки
  живёт на версии того, кто его конфигурировал.
- В CI версия прибита переменной `VW_CMAKE_VERSION` в `env` всех трёх workflow
  (`ci.yml`, `analysis.yml`, `release.yml`) — поднимай её тем же изменением, что
  и таблицу. Без пина раннер сам подтянет новый CMake, и CI упадёт на
  конфигурации.

## Шейдеры и ассеты рядом с исполняемым файлом

`cmake/shaders.cmake`: `vw_compile_shaders()` (один раз на проект) компилирует
`shaders/*.vert`, `*.frag`, `*.comp` через `glslc` в
`${CMAKE_BINARY_DIR}/shaders_compiled`; `vw_setup_shaders(<exe>)` заводит таргет
`<exe>_shaders`, копирующий этот каталог в `$<TARGET_FILE_DIR>/shaders`, и
вешает на него исполняемый файл. `cmake/assets.cmake`: `vw_setup_assets(<exe>)` —
таргет `<exe>_assets` стирает `$<TARGET_FILE_DIR>/assets` и копирует `assets/`
целиком. Шейдеры подключают все три приложения, ассеты — только `arena`.

- Доставка — только custom target, от которого зависит исполняемый файл.
  `add_custom_command(TARGET … POST_BUILD)` срабатывает лишь при перелинковке:
  правка одного шейдера или модели не доезжает, и приложение молча работает со
  старой.
- Копируй каталог, который наполнил шаг компиляции, а не список, снятый glob'ом
  каталога сборки на конфигурации: такой список не видит шейдер, добавленный
  после неё.
- Ассеты — стирание и копия всего дерева, не поштучно по расширениям: иначе
  переименованный или перенесённый файл навсегда оставляет старую копию. Новому
  формату в `assets/` строка в CMake не нужна; новой стадии шейдера (`.geom`,
  `.tesc`…) — нужна: glob в `vw_compile_shaders` знает три расширения.

## Предупреждения

`vw_set_warnings(<target>)` (`cmake/vw_warnings.cmake`) ставит флаги PRIVATE.
Ветка — по `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`: MSVC-синтаксис получает
`/W4 /permissive- /wd4324`, GNU-синтаксис (и clang++ на Windows) — `-Wall -Wextra …`.
`VW_WARNINGS_AS_ERRORS=ON` добавляет `/WX` или `-Werror`; все сборочные джобы
`ci.yml` его включают.

- `/permissive-` не убирай: без него MSVC принимает код, который другие
  компиляторы отвергают.
- C4324 («структура дополнена из-за описателя выравнивания») гасится флагом, а
  не `#pragma warning`: оно всплывает у каждого импортёра структуры с `alignas`,
  и прагма внутри модуля до него не доходит. Паддинг UBO под std140 — цель;
  раскладку стерегут `static_assert(offsetof(...))` в
  `engine/gfx/src/render/render_uniforms.cppm` — при правке структуры правь их.

## CI

Workflow лежат в `.github/workflows/`: `ci.yml` и `analysis.yml` идут на каждый
push, `release.yml` — на тег `v*`. Версии инструментов — `VW_CMAKE_VERSION` и
`VW_LLVM_VERSION` в `env` каждого файла; меняй во всех трёх разом.

| Файл | Джоб | Что проверяет |
|---|---|---|
| `ci.yml` | `build` | Windows, `msvc`/`clang` × `Debug`/`Release`, полная конфигурация и тесты |
| `ci.yml` | `headless` | Windows без Vulkan SDK, `VW_BUILD_GFX=OFF` |
| `ci.yml` | `linux` | ubuntu-24.04, Clang + libc++, триплет `x64-linux-libcxx`, полная конфигурация |
| `ci.yml` | `lint` | `python scripts/lint_modules.py` |
| `analysis.yml` | `sanitizers` | Linux, ASan+UBSan и TSan на headless-сборке |
| `analysis.yml` | `fuzz` | Linux, по минуте libFuzzer на разборщики `.vox`, `.voxa` и JSON |
| `analysis.yml` | `coverage` | Linux, llvm-cov по headless-тестам, отчёт в summary джоба |
| `analysis.yml` | `asan-windows` | MSVC ASan, gfx включён — под санитайзер попадает мешер |
| `release.yml` | `build-windows` `build-linux` `release` | пакеты `sculptor` и GitHub Release |

Джоб `coverage` перечисляет бинарники тестов вручную — список `test_targets` в
шаге Report, из него собирается `objects` для `llvm-cov`. Новый тестовый таргет
впиши и туда, иначе его строки в покрытие не попадут.

### Санитайзеры

Linux (`sanitizers`, `coverage`) идёт без vcpkg: Clang + libc++, флаги через
`CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS`, `VW_BUILD_GFX=OFF`, опции рантайма —
в `env` шага Test. TSan — отдельная строка матрицы: с ASan он несовместим.

Windows (`asan-windows`) — MSVC `/fsanitize=address`; локально так же:

```
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVW_BUILD_APPS=OFF \
      -DCMAKE_DISABLE_FIND_PACKAGE_Catch2=ON \
      -DCMAKE_CXX_FLAGS_INIT="/fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="/INCREMENTAL:NO" \
      -DCMAKE_TOOLCHAIN_FILE=C:/Users/lucius/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows
```

- `CMAKE_CXX_FLAGS_INIT`, а не `CMAKE_CXX_FLAGS`: значение второго из командной
  строки замещает подготовленное CMake (`/DWIN32 /D_WINDOWS /GR /EHsc` у MSVC), и
  сборка осталась бы без `/EHsc`. К `_INIT` платформенный модуль дописывает своё,
  но читается оно только при первой конфигурации каталога сборки — меняешь флаг,
  конфигурируй заново пустой каталог.
- `RelWithDebInfo`, не `Debug`: CMake кладёт `/RTC1` в отладочные флаги, а ASan
  от MSVC с ним несовместим. `/INCREMENTAL:NO` обязателен — ASan требует
  неинкрементальной линковки.
- Catch2 из vcpkg под ASan не линкуется — LNK2038 про `annotate_string`: ASan
  меняет раскладку строк и векторов, а предсобранная библиотека без
  инструментации. Отсюда `CMAKE_DISABLE_FIND_PACKAGE_Catch2=ON`; остальные
  зависимости остаются из vcpkg.

`tests/CMakeLists.txt` ищет Catch2 через `find_package(Catch2 3 CONFIG QUIET)` и
при промахе берёт его FetchContent'ом. Не упрощай до `REQUIRED`: на запасной
ветке живут все санитайзерные сборки и покрытие — предсобранный Catch2 собран
другой стандартной библиотекой и без инструментации, смешивать его с
санитайзерным рантаймом нельзя. Версию поднимай парой: `GIT_TAG` там и `catch2`
в `vcpkg.json`.

### Фаззинг

`VW_BUILD_FUZZERS=ON` подключает `tests/fuzz/` независимо от `VW_BUILD_TESTS`;
без Clang конфигурация падает — libFuzzer есть только у него. `vw_add_fuzzer(name
source)` вешает `-fsanitize=fuzzer-no-link`/`-fsanitize=fuzzer` на свой таргет;
`fuzzer-no-link` получают и сами `vw_core`, `vw_asset` и `vw_net` — без него покрытие
снимается только с обвязки, и фаззер перебирает входы вслепую. ASan и UBSan
ожидаются снаружи в `CMAKE_CXX_FLAGS`/`CMAKE_EXE_LINKER_FLAGS`, как
в шаге Configure джоба `fuzz`. Затравка — то, что уже лежит в репозитории: `assets/models`,
`assets/animations`, для JSON — `tests/data/json_test_suite/test_parsing`, для
головы HTTP-запроса — два файла в `tests/data/http_heads`;
ctest-тесты `fuzz_*_seed_corpus` прогоняют её с
`-runs=0`. Упавшие входы джоб выгружает артефактом `fuzz-crashes`.

- libFuzzer пишет всё найденное в **первый** каталог-аргумент. Первым ставь
  временный (`corpus/vox`), `assets/…` — вторым, только как затравку; наоборот —
  и репозиторий засыплет тысячами файлов со случайными именами.
- На Windows фаззеры собираются clang++ только с
  `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`: libFuzzer из LLVM собран под
  статический рантайм, и линковка с обычной сборкой падает на `/failifmismatch`.
  CI фаззит только на Linux.

### Linux: раннер и триплет libc++

Джобы `linux` (`ci.yml`) и `build-linux` (`release.yml`): ubuntu-24.04, Clang
`VW_LLVM_VERSION` из apt.llvm.org, движок на libc++, зависимости из vcpkg через
overlay-триплет `cmake/triplets/x64-linux-libcxx.cmake`
(`-DVCPKG_OVERLAY_TRIPLETS=…/cmake/triplets -DVCPKG_TARGET_TRIPLET=x64-linux-libcxx`).
Триплет собирает порты с `-stdlib=libc++`: Catch2 поверх libstdc++ с движком не
линкуется — он передаёт `std::string` через границу библиотеки.

- Компилятор в триплете не назначай и `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` не ставь:
  он целиком замещает штатный `scripts/toolchains/linux.cmake` вместе с `-fPIC`,
  `CMAKE_SYSTEM_NAME`, сбросом `CMAKE_CROSSCOMPILING` и policy. Флаги — через
  `VCPKG_C_FLAGS`/`VCPKG_CXX_FLAGS`/`VCPKG_LINKER_FLAGS`, штатный тулчейн
  подхватывает их сам.
- Компилятор портам приходит из `CC`/`CXX`, которые шаг Install Clang пишет в
  `$GITHUB_ENV`, — с суффиксом версии (`clang++-20`): голый `clang++` на
  Ubuntu 24.04 — Clang 18 с libstdc++.
- Шаг Install Vulkan SDK обязан выставить `VULKAN_SDK=/usr`: пакет LunarG
  раскладывает SDK по `/usr`, но переменную не заводит, а порт `vulkan` (его
  тянет `imgui[vulkan-binding]`) падает `FATAL_ERROR`, если `find_package(Vulkan)`
  промахнулся.

### Упаковка релиза

`build-windows` и `build-linux` собирают только таргет `sculptor`
(`VW_BUILD_TESTS=OFF`, `VW_SCULPTOR_ASSET_ROOT:STRING=.`) и складывают в
`package/` исполняемый файл, `shaders/` и `assets/` (на Windows ещё `*.dll`);
`release` скачивает артефакты, пакует Windows-часть в zip и публикует оба архива.

- Корень ассетов и копия дерева `assets/` в пакете — одно изменение: с корнем `.`
  Sculptor читает и пишет рядом с собой, и без дерева в пакете ему нечего
  открыть. Ассеты берутся из чекаута, как их берёт `vw_setup_assets` для арены.

- Linux-архив (`tar -czf`) собирается в `build-linux`, а не в `release`:
  `upload-artifact` передаёт файлы zip'ом без прав доступа, и бинарь доехал бы
  без бита исполнения. Новый Linux-бинарь в релизе пакуй в tar там же, где
  собран.
