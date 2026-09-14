---
name: cmake-build
description: Сборка voxelworld — генератор Ninja и окружение vcvars, таргеты vw_core/vw_ecs/vw_world/vw_platform/vwengine, опции VW_BUILD_*, подключение нового модульного юнита, тесты и headless-конфигурация без Vulkan. Читай при правке CMakeLists.txt, cmake/*.cmake, vcpkg.json и перед запуском сборки или тестов.
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
cmake -S . -B build/tests -DCMAKE_TOOLCHAIN_FILE=C:/Users/lucius/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows -DVW_BUILD_APPS=OFF
cmake --build build/tests --target core_tests asset_tests ecs_tests world_tests
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
| `vw_ecs` | модуль `vw.ecs` (`engine/ecs/`) |
| `vw_world` | модуль `vw.world` (`engine/world/`) |
| `vw_platform` | модуль `vw.platform` (`engine/platform/`); только при `VW_BUILD_GFX=ON` |
| `vwengine` | INTERFACE-библиотека header-only gfx; существует только при `VW_BUILD_GFX=ON` |
| `core_tests` `asset_tests` `ecs_tests` `world_tests` | тесты Catch2; линкуются на модульные таргеты, никогда на `vwengine` |
| `view_bench` | микробенчмарк обхода ECS (регрессионный сторож из M2) |

Опции: `VW_BUILD_GFX` (по умолчанию ON), `VW_BUILD_APPS`, `VW_BUILD_TESTS`.
`VW_BUILD_APPS=ON` при `VW_BUILD_GFX=OFF` — ошибка конфигурации: приложениям
нужно окно.

`VW_SCULPTOR_ASSET_ROOT` (по умолчанию `${CMAKE_SOURCE_DIR}/assets`) — каталог,
который Sculptor открывает и в который пишет: `prefabs/`, `models/`,
`animations/` и `fsm/` ищутся в нём (имена — `vw::asset::dirs`). Редактор правит
ассеты репозитория, поэтому `git status` показывает правку сразу; сборке «на
раздачу» нужно поставить `.`, и корнем станет каталог рядом с исполняемым
файлом. Арене ассеты по-прежнему копирует `vw_setup_assets`
(`cmake/assets.cmake`) — она их только читает.

Зависимости gfx (glfw3, imgui) вынесены в vcpkg-фичу `gfx`, включённую по
умолчанию; `-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON` оставляет только Catch2.

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

Каждой модульной библиотеке нужен `vw_use_std_module(<target>)` — он включает
`CXX_MODULE_STD` для MSVC и подсовывает собственный таргет `std` для Clang
(`cmake/vw_std_module.cmake`).

`cmake/vw_vulkan_module.cmake` даёт `vw_add_vulkan_module()` — таргет
`VulkanHppModule` для `import vulkan`. Пока не вызывается, понадобится в M5.

## Настройки, доходящие до кода

Только как экспортированный `constexpr`, не как макрос в заголовке потребителя:
cache-переменная → `target_compile_definitions(... PRIVATE ...)` →
`inline constexpr` в партиции. Образец — `VW_LOG_MIN_LEVEL` → `vw::log::min_level`
в `engine/core/CMakeLists.txt` и `engine/core/src/log/log.cppm`.
