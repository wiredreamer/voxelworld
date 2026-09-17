# Voxel World

Воксельный движок на C++23/Vulkan. Движок + ECS + приложение Sculptor (редактор
вокселей). Описание движка — `docs/ENGINE.md`, продукт — `docs/PRD.md`.
Подсистемы подробно: `docs/lighting.md` (свет), `docs/rendering.md` (рендер),
`docs/world.md` (мировая сетка).

## Architecture

Всё — именованные модули C++: движок из шести библиотек плюс приложения
`vw.sculptor` и `vw.arena`. Заголовков движка не существует, только `import`.

- **vw.core** (`engine/core/src/`, таргет `vw_core`) — типы, math, transform,
  лог, воксели, геометрия `vw::spatial`; каталоги `types/ math/ utils/ spatial/ voxels/ log/`
- **vw.asset** (`engine/asset/src/`, таргет `vw_asset`) — модели, анимации,
  форматы `.vox`/`.voxa`, хранилище ассетов. Зависит только от `vw.core` и
  ничего не знает про ECS; каталоги `model/ anim/ serial/`
- **vw.ecs** (`engine/ecs/src/`, таргет `vw_ecs`) — entity, type-erased пул,
  реестр с рантайм-идентификаторами компонентов
- **vw.world** (`engine/world/src/`, таргет `vw_world`) — компоненты, системы,
  сетка чанков, свет, чтение и запись сцены. Собирается без Vulkan;
  каталоги `scene/ components/ systems/ grid/ light/ spatial/`
- **vw.platform** (`engine/platform/src/`, таргет `vw_platform`) — окно, ввод,
  события; GLFW живёт ровно в одном `.cpp`; каталоги `input/ window/`
- **vw.gfx** (`engine/gfx/src/`, таргет `vw_gfx`) — рендер на `vk::` через
  `import vulkan`, камера, ImGui, debug; C API Vulkan в исходниках нет;
  каталоги `camera/ resource/ render/ debug/ engine/`

- **Apps** — `apps/sculptor/` (модуль `vw.sculptor`, партиции `:state`,
  `:shortcuts`, `:operations`, `:services`, `:tools`, `:ui`, `:app`), `apps/testbed/`
  (`vw.testbed`: стенд `:app`, сцена на партицию в `:scenes.*`, пути камеры
  `:cameras`, приборы `:probes.*`), `apps/arena/` (`vw.arena`)
- **Shaders** — GLSL → SPIR-V (`shaders/`)

Исходники разложены по смысловым каталогам внутри `src/`; в корне лежит только
первичный интерфейс модуля. Имя партиции повторяет каталог
(`vw.world:systems.transform`), а крупные партиции — агрегаторы из `export import`.

Пространства имён: `vw` (core), `vw::spatial` (геометрия), `vw::asset` (данные
ассетов), `vw::ecs` (реестр, мир, компоненты, системы), `vw::plat` (окно и ввод),
`vw::gfx`, `vw::sculptor`. Модуль ≠ namespace: `vw.world` экспортирует `vw::ecs`,
включая сериализацию сцены в `:scene.*`, а `vw.asset` — одноимённое `vw::asset`.

Undo/redo в Sculptor — command-паттерн через `base_operation`.

## Key Commands

Только генератор Ninja и только из окружения `vcvars64.bat`.

- `cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release`
- `ctest --test-dir build/release --output-on-failure`

Подробности — навык `cmake-build`.

## CRITICAL RULES

- ALWAYS trailing return type: `auto foo() -> int32;`
- ALWAYS типы проекта (`uint32`, `float32`) вместо встроенных и std
- NEVER комментарии в коде — ни в C++, ни в GLSL, ни в CMake, Python или CI.
  Код объясняет себя именами и типами. Сложный алгоритм, замеренное решение или
  контракт между модулями описывается в `docs/`, а в коде остаётся максимум
  ссылка `// см. docs/<файл>.md#<раздел>`. Исключения и проверка — `cpp-style`
- ALWAYS запускай тесты после изменений в `engine/`
- NEVER форвард-объявляй сущности модулей вне их модуля — только импортируй
- NEVER исключения в горячих путях
- NEVER `#include` своих заголовков и `.inl.h` — их больше нет, только `import`
- NEVER текстовые std-заголовки: `import std;` (макросы — исключение, см. `cpp-style`)
- NEVER коммить и создавать ветки без прямой просьбы пользователя

## Skills

- **`cpp-style`** — стиль C++, устройство модульного кода, рецепты добавления
  компонентов и систем, правка форматов ассетов
- **`cmake-build`** — сборка, таргеты, опции, тесты, headless-конфигурация
- **`render-bench`** — кадровые замеры: запуск `testbed` по сценам,
  снятие чисел до и после правки, пороги и вердикт по отчёту
- **`gpu-contracts`** — данные, общие для C++ и шейдеров: что менять вместе и
  чем проверить
- **`sculptor`** — редактор: рецепты инструментов и операций, ловушки записи,
  undo и ImGui
- **`git-workflow`** — что агент делает сам, формат коммитов и веток

## Working Style

- Сначала план, потом код
- Маленькие изменения: один файл → тесты → следующий
- Используй субагентов для исследования кодовой базы
