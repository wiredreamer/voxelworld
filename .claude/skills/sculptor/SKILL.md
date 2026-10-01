---
name: sculptor
description: Редактор вокселей Sculptor — apps/sculptor, модуль vw.sculptor с партициями :state, :shortcuts, :operations, :services, :tools, :ui, :app. Рецепты нового инструмента (base_tool, регистрация, кнопка, горячая клавиша) и операции с undo/redo (base_operation, composite_operation, operation_manager); сервисы и панели ImGui; ловушки записи .voxm и общих объёмов, контекста правки после undo, выделения, буфера и режима вставки, ключей анимации и правок посреди обхода ImGui. Читай перед правкой apps/sculptor.
---

# Sculptor

## Где что лежит

| Партиция | Интерфейс | Тела | Что внутри |
|---|---|---|---|
| `:state` | `state.cppm` | `app/app_state.cpp` | `app_state`: документ, контекст правки, сцена, кисть, анимация; `enum class tools`, `panels` |
| `:shortcuts` | `shortcuts.cppm` | `shortcuts/shortcuts.cpp` | `enum class command`, таблица `shortcuts`, `tool_of`, `command_for_tool` |
| `:operations` | `operations.cppm` | `operations/*.cpp` | `base_operation`, `composite_operation`, `operation_manager`, все операции |
| `:services` | `services.cppm` | `services/*.cpp` | `file_service`, `clip_service`, `clipboard_service`, `keyframe_service`, `playback_service`, `fsm_service` |
| `:tools` | `tools.cppm` | `tools/*.cpp` | `base_tool`, инструменты, `gizmo` |
| `:ui` | `ui.cppm` | `ui/*.cpp` | панели, модальные окна, `component_drawer` |
| `:mcp` | `mcp.cppm` | `mcp/*.cpp` | `mcp_server`: слушатель HTTP, диспетчер JSON-RPC, инструменты агента |
| `:app` | `app.cppm` | `app/app.cpp`, `app/launch_options.cpp` | `app`: владеет всем перечисленным, тик, кадр, ввод; разбор флагов запуска |

Партиция — один `.cppm` без подпартиций: новый класс дописывается в него
отдельным блоком `export namespace vw::sculptor { … }`, список `.cppm` в CMake
не меняется. Тело — `.cpp` с `module vw.sculptor;`, строкой в `PRIVATE` в
`apps/sculptor/CMakeLists.txt`. Проверка — `cmake --build build/release --target
sculptor`; тестов у приложения нет.

## Тик и кадр

`app::update` движок зовёт каждую итерацию цикла, даже когда окно свёрнуто и
кадра нет; `app::render` — только когда кадр открылся (см.
`docs/ENGINE.md#главный-цикл`). В `update` живёт обслуживание состояния, без
которого документ расходится с миром: `collect_dirty_models`, `prune_contexts_`,
`clipboard_service::sync`, границы объёма, видимость, допустимость инструмента.
В `render` — всё, что рисует: ImGui, отладочные линии, камера, инструменты.

Новый шаг, от которого зависит сохранение или целостность документа, клади в
`update`: в `render` он пропадёт на всё время, пока окно свёрнуто. Вызовы
ImGui и `renderer.draw_*` в `update` запрещены — кадра там может не быть, а
отладочные примитивы копились бы без очистки.

## MCP

Устройство сервера, HTTP и протокол — `docs/mcp.md`. Здесь только то, что нужно
при правке.

**Новый инструмент агента** — запись `mcp_tool` в `make_editor_tools`
(`mcp/editor_tools.cpp`): `name`, `description`, `input_schema` текстом JSON
Schema и `run`, который получает аргументы объектом `json::value` и возвращает
`tool_success(значение)` либо `tool_failure("причина")`.

- Аргументы читай через `json::cursor`: его ошибка уже содержит путь до поля,
  её текст и отдаётся в `tool_failure`.
- Сбой — это `tool_failure`, а не исключение и не молчаливый выход: агент
  исправляется по тексту причины, поэтому в нём должно быть сказано, что
  допустимо («узла `hand` нет; есть: …»).
- Схема разбирается при старте; битая пишет ошибку в лог и заменяется на пустой
  объект. Проверяй лог после добавления инструмента.
- Доступ к редактору — через `mcp_bindings`. Новую зависимость (сервис,
  `operation_manager`) добавляй полем туда и в `app::start_mcp_`.
- `run` исполняется на главном потоке из `app::update`: ImGui и
  `renderer.draw_*` там звать нельзя (см. «Тик и кадр»), а проверка открытого
  попапа через `ImGui::IsPopupOpen` допустима — она только читает состояние.
- Правящий инструмент обязан отказать, если `busy_reason` непуст, и менять
  документ только через `op_manager.execute`, как любой другой код редактора.

После правки в `mcp/` прогони `python tools/mcp_smoke.py` по запущенному
`sculptor --mcp` — на видимом и на свёрнутом окне.

## Новый инструмент

1. `state.cppm`: значение в `enum class tools` и ветка в
   `context_state::allows_tool`. Без ветки `app::update` каждый тик сбрасывает
   выбор на `default_tool()`, и инструмент молча не включается. Основной
   инструмент контекста — ещё и в `default_tool`.
2. `tools.cppm`: `class <name>_tool final : public base_tool` с конструктором
   `(engine_type& eng, app_state& st, operation_manager& op_manager)` и всеми
   шестью переопределениями: `render`, `on_key_press`, `on_mouse_move`,
   `on_mouse_press`, `on_mouse_release`, `on_activate`. Тело —
   `tools/<name>_tool.cpp`. Образец — `paint_tool`.
3. Конструктор `app::app` (`app/app.cpp`):
   `tools_[tools::<name>] = std::make_unique<<name>_tool>(eng, state_, op_manager_);`.
   Без этой строки `tools_[active_tool_]` вернёт пустой указатель, и приложение
   упадёт при первом выборе инструмента.
4. Кнопка — `render_tool_button(tools::<name>, "Label")` в `tool_panel::render`
   (`ui/tool_panel.cpp`). Панель видна только в контексте объёма.
5. Клавиша:
   - значение в `enum class command` и строка в `shortcuts` (`shortcuts.cppm`)
     рядом с остальными из группы `"Tools"`: окно подсказок рисует заголовок
     при каждой смене `group`;
   - ветки в `tool_of` и `command_for_tool` (`shortcuts/shortcuts.cpp`);
     инструмент без клавиши возвращает из `command_for_tool` `std::nullopt`;
   - `case command::<…>:` в хвост `switch` в `app::run_command_`: `switch` там
     без `default`.

   Подпись у кнопки и окно подсказок берут клавишу из таблицы сами.

Инструмент меняет документ только операцией через `op_manager_->execute`, а
узел берёт через `state_->edited_node()` (см. «Контекст правки и undo»).

## Новая операция (undo/redo)

1. `operations.cppm`: `struct <name>_params` и
   `class <name>_operation final : public base_operation` отдельным блоком.
   Конструктор — `(engine_type& engine, app_state& state, const <name>_params& params)`,
   а если операция грузит объём по ссылке — ещё `asset::model_library&`. Поля:
   `engine_type* engine_`, `app_state* state_`, `<name>_params params_` и всё,
   что нужно для отката.
2. Тело — `operations/<name>_operation.cpp`. Родственные операции могут делить
   файл (`fsm_operations.cpp`, `structure_operations.cpp`).
3. Конструктор ничего не меняет. `execute()` применяет правку: менеджер зовёт
   его и при первом исполнении, и при каждом redo. Прежнее значение снимай
   внутри `execute()`, `undo()` возвращает ровно его (образец —
   `set_pivot_operation`).
4. Узел ищи по имени, `state_->scene.name_to_entity[params_.name]`, заново в
   каждом `execute()` и `undo()`. `ecs::entity` не храни: undo удаления заводит
   узлу новую сущность.
5. Отмечай несохранённое и в `execute()`, и в `undo()`:
   - документ — `state_->file.has_unsaved_changes = true`;
   - клип — `state_->anim.unsaved_clips[clip] = true`; для ключей ещё
     `track->mark_dirty()` и `state_->anim.need_apply_pose = true`;
   - машина состояний — `state_->fsm.has_unsaved_changes = true`;
   - объём, изменённый мимо флага `changed<model_component>`, — ещё и
     `dirty_models` (см. «Запись документа»).
6. Исполняй только `op_manager.execute(std::make_unique<<name>_operation>(…))`,
   а не `op->execute()`: менеджер запоминает контекст правки и очищает redo.
   Несколько шагов одним undo — неисполненные части в
   `std::vector<std::unique_ptr<base_operation>>`, обёрнутые в
   `composite_operation`. Части исполняются по порядку, а откатываются в
   обратном (образец — `keyframe_service::move_keyframe`).

## Запись документа

**Записывается только помеченный объём.** `file_service` пишет .voxm лишь для
сущностей из `state.file.dirty_models`. Туда их кладёт
`file_service::collect_dirty_models` по `world.changed<ecs::model_component>()` и
ещё не promoted `registry().requested<ecs::model_component>()`, а эти флаги ставят
только `model_system::modify(ent).set_model`, `set_voxel` и `fill`.
`set_pivot` заявляет изменение трансформа, а `set_source` не заявляет ничего,
поэтому такую правку клади в `state_->file.dirty_models.insert(ent)` сам — иначе
сохранение молча оставит старый .voxm.

**Флаг доходит не сразу, и сохранение это учитывает.** `changed` выставляет
обновление мира, поэтому правка из ImGui лежит в `requested` до ближайшего
обновления. `file_service::write_` зовёт `collect_dirty_models` сам и читает оба
набора, поэтому `Ctrl+S` из опроса событий и сохранение сразу за операцией видят
правку. Новый путь сохранения зовёт `write_`, а не пишет объёмы в обход него.

**Новый объект модели под старой ссылкой нужно отдать библиотеке через `adopt`.**
Расширение и обрезка ставят узлу новый `asset::model`, а `source` не меняют;
после записи `file_service::write_dirty_models_` делает
`library_->adopt(ref, model)`. Если пишешь объём мимо `file_service`, зови
`adopt` сам. Иначе в кеше `model_library` останется старый объект, и узел,
подключённый по той же ссылке, получит устаревший объём.

**На одну ссылку — один экземпляр объёма.** Объём по `asset_ref` бери только у
общей `asset::model_library` (`app::model_library_`, она приходит ссылкой в
конструктор) через `load`, `find` или `adopt`. Узлы и варианты грузи через
`ecs::vox_deserializer{world, parser, library}`. Вторая `model_library` или
прямой `voxm_deserializer` заведут копию, и правка одного узла разойдётся с
другими, которые ссылаются на тот же файл.

**Save As уносит объёмы префаба в его новую папку.** Объёмы префаба лежат в
`models/<имя префаба>/`, поэтому `file_service::save_as` переводит ссылки из
папки прежнего префаба в папку нового (`asset::rehomed_model_ref`) — и
`source` узлов, и `.voxm`-кандидаты вариантов. Ссылки на чужие папки и
кандидаты-префабы остаются как были. Объём узла переезжает тем же объектом
(`set_source` + `adopt` + `dirty_models`), невыбранный кандидат копируется
через `load` и `save`, а старая ссылка после удачной записи уходит из кеша
(`model_library::forget`) и в следующий раз читается с диска. Если запись
префаба не удалась, ссылки возвращаются назад. Новое место, где префаб хранит
ссылку на объём, добавляй в `plan_model_moves_` и `retarget_models_`, иначе
копия префаба продолжит править объём оригинала. Операции в истории хранят
прежние ссылки, и откат вернул бы узлу объём из старой папки, поэтому после
удачного Save As `save_as` очищает историю (`operation_manager::clear`).

**Переименование объёма — тот же перенос для одной ссылки.**
`file_service::rename_model` меняет только имя файла
(`asset::renamed_model_ref`), папка и расширение остаются. Объём принадлежит
ровно одному префабу, поэтому чужие префабы не просматриваются. Порядок: записать
объём под новой ссылкой, перевести ссылки (`retarget_models_`), записать префаб,
удалить старый файл и убрать старую ссылку из кеша, очистить историю. Если
запись префаба не удалась, ссылки возвращаются назад, а старый файл остаётся.
Имя, занятое другим узлом или кандидатом открытого префаба, — ошибка
`name_in_use`; файл на диске, на который никто не ссылается, перезаписывается
после подтверждения (`file_exists`). Новое место хранения ссылки добавляй ещё и
в `is_model_referenced_`. Кнопка — `Rename` в строке Volume дроера Model, только
в контексте объёма; модальное окно открывает флаг `ui.need_rename_model_for`.

**Объём правят только изнутри него.** Один .voxm делят все узлы и префабы,
которые на него ссылаются, поэтому точка вращения, обрезка и воксели меняются
только в контексте `edit_kind::model`. Виджет правки закрывай проверкой
`state.ctx.allows_volume_edit()`, а инструмент — веткой в `allows_tool`. Иначе
правка из префаба молча изменит чужие модели.

## Контекст правки и undo

**Узел правки — `state.edited_node()`, а не `scene.selected_name`.**
`operation_manager` запоминает `ctx.stack` при исполнении и возвращает его при
undo и redo, а выделение не трогает. Вход и выход (`ctx.enter`, `leave_to`) в
историю не попадают. Поэтому после undo крошки могут указывать на
невыделенный объём. `edited_node()` берёт имя из контекста и возвращает
выделение, только когда контекст узла не называет (префаб, клип, машина).
`selected_name` читай лишь там, где речь именно о выделении: выбор в префабе,
сокеты, ключи клипа.

## Выделение, буфер и вставка

**Правка формы объёма — это новый объём.** Размер `asset::model` неизменяем,
поэтому обрезка, поворот и отражение, стирание области и вставка строят новый
объём функцией движка (`asset::trimmed`, `reoriented`, `erased`, `pasted` в
`model_edit.cppm`) и ставят его узлу через `model_system::modify(ent).set_model`.
Операция хранит прежний `std::shared_ptr<asset::model>` и возвращает его в
`undo()`. Образцы — `trim_model_operation`, `reorient_model_operation`,
`operations/clip_operations.cpp`. Поворот и отражение идут относительно pivot:
он пересчитывается вместе с вокселями, и деталь остаётся на месте в узле.

Действия над целым объёмом — Flip, Rotate и Trim — живут в меню Edit → Volume
(`menu_bar::render_volume_menu_`), а не в дроере Model: дроер показывает
характеристики объёма, правка формы идёт через меню и инструменты. Действия над
выделением — Fill и Recolor (`asset::filled` с `fill_scope`, операция
`fill_voxels_operation`) — кнопки окна Selection. Каждое действие — своя кнопка
или пункт: поведение не переключается модификатором или режимом.

**Выделение — бокс в `state.volume.selection`.** Вместе с боксом лежат имя узла
и размер объёма, для которого он задан. В историю выделение не попадает.
`clipboard_service::sync` сбрасывает его, когда сменился узел правки или размер
объёма: расширение в отрицательную сторону сдвигает координаты, и старый бокс
указывал бы не туда. Код, который меняет размер объёма, сам выделение не чинит.

**Буфер переживает смену контекста и префаба.** `state.clipboard.clip` —
`asset::voxel_clip`: плотный блок вокселей и его угол относительно pivot
источника. `app_state::reset` буфер сохраняет. Вставка по умолчанию ложится в
`pivot цели + corner_from_pivot` (`asset::paste_origin`), поэтому фрагмент
попадает на то же место относительно сустава в объёме другого размера.
`pasted` расширяет объём до занятых вокселей фрагмента и сдвигает pivot.

**Вставка — отдельный контекст `edit_kind::paste`.** `clipboard_service::begin_paste`
кладёт его поверх контекста объёма, и пока он на вершине стека, доступно
только размещение фрагмента:
- `allows_tool` пропускает один `tools::place_paste`, `allows_volume_edit()`
  ложно, панели инструментов, палитры, свойств и дерева скрыты;
- `is_available` в этом контексте разрешает только `command::confirm` и
  `command::cancel`; меню Prefab, Edit, Animation и крошки выключены через
  `ImGui::BeginDisabled`.

Новую команду, пункт меню или панель, способные менять документ, закрывай тем
же способом, иначе правка пройдёт посреди вставки. Две страховки на случай
пропущенного пути: `app::handle_key_press` не исполняет команды, пока открыт
любой попап ImGui (иначе `Ctrl+V` за окном Save As начал бы вставку), а
`file_service::write_` отказывается писать, пока `state.paste.active()`, — на
диск не попадает объём-предпросмотр.

**Общий объём меняй через `replace_volume`.** `set_model` меняет указатель у
одного узла, а объём по одной ссылке держат все узлы, которые на неё ссылаются.
`replace_volume(engine, current, next)` ставит `next` каждой сущности с
указателем `current`; через него идут trim, expand, reorient, стирание, вставка
и её предпросмотр. Кеш `model_library` при этом обновляется только при записи
(`adopt`), поэтому кандидат варианта, загруженный до сохранения, получит
прежний объём.

**Предпросмотр вставки — подмена объёма узла.** `state.paste` хранит исходный
объём (`base`), фрагмент, смещение и режим. Гизмо (`gizmo_target::fragment`) и
панель меняют только эти поля и ставят `preview_stale`; объём узла
перестраивает `clipboard_service::sync` раз в кадр. Отмена возвращает `base`.
Подтверждение делает три шага строго в этом порядке: вернуть `base`, выйти из
контекста вставки, выполнить `paste_voxels_operation`. `operation_manager`
запоминает `ctx.stack` в момент `execute`, и операция, выполненная до выхода,
после undo вернула бы редактор в режим вставки без фрагмента.

`sync` заодно сверяет состояние с контекстом: если `state.paste` активно, а
контекст уже не `paste` (или узел пропал), вставка отменяется и объём
возвращается. Новый путь выхода из контекста достаточно не ломать этой сверкой.
Места, где раньше стояло `kind() == edit_kind::model` ради показа объёма,
спрашивают `ctx.shows_volume()`: он истинен и в контексте вставки.

Предпросмотр помечает узел в `dirty_models`, и после отмены метка остаётся:
ближайшее сохранение перепишет `.voxm` тем же содержимым.

## Анимация

**На одно мгновение — один ключ.** `animation_channel::add` и `replace`
сортируют ключи нестабильным `std::sort`, и `evaluate` при равном времени берёт
любой. Единственность в пределах `same_instant_tolerance_seconds` (`1e-3`,
`services/keyframe_service.cpp`) держит только редактор:
- `record_pose` переписывает ключ на том же времени через
  `modify_keyframe_operation`;
- `move_keyframe` убирает ключ, занявший место, через
  `remove_keyframe_operation` в одной `composite_operation` с переносом.

Правило живёт в одном месте — `keyframe_service::place_keyframe_`, — и все пути
идут через него: `record_pose`, `move_keyframe`, диалог «Add Keyframe…» и поле
Time в `keyframe_properties_panel`. Новый путь зовёт `set_keyframe` (добавить или
заменить) либо `modify_keyframe` (перенести или поправить), а не собирает
операции с ключами сам.

## UI и ImGui

**Жест — одна операция.** Пока тянут ползунок или манипулятор, правь документ
предпросмотром, а в историю клади одну операцию на весь жест: на отпускании
верни состояние, каким оно было до жеста, и выполни операцию с итоговым
значением. Иначе каждый кадр перетаскивания уходит в undo отдельным шагом.
Образцы: `gizmo::on_mouse_release`, `timeline_panel::update_key_drag_` и
`keyframe_properties_panel` (`keyframe_service::preview_keyframe` на кадрах
жеста, `modify_keyframe` на `ImGui::IsItemDeactivatedAfterEdit`). Начало и конец
жеста у полей `vec3f` отдаёт `imgui_drag_vec3f` в `drag_edit`.

**Структурную операцию исполняй после обхода.** Внутри цикла по виджетам только
запоминай намерение, а `op_manager.execute` зови после цикла. Образцы:
`entity_tree_panel::pending_move_`, `picked`/`dropped` в drawer варианта
(`ui/component_drawers.cpp`), `socket_to_remove` в `socket_panel`. Перенос узла
переписывает списки детей `hierarchy_component`, по которым идёт
`render_entity_node`. Операции варианта меняют список кандидатов и объём узла,
и ссылка на компонент, взятая до `execute`, после него висит.

**Действие чужой панели запрашивай флагом.** Модальное окно или переход,
которыми владеет другая панель или `app`, не открывай напрямую: ставь
`state.ui.need_*` или `state.anim.need_*`. Флаг разбирает владелец в своём
`render` (`need_add_model_for` → `edit_components_modal`, `need_enter_machine` →
`app::render`).
