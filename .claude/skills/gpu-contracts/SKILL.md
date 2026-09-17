---
name: gpu-contracts
description: Контракты данных между C++ и шейдерами voxelworld — кадровый uniform, буферы источников и пятен, упаковка квада, оси касательных, таблицы граней и порядок обхода углов, отсев индиректа, выключатель теней, раскладка счётчиков кластеров, палитра, наборы и привязки. Что менять вместе, что это сторожит, как выглядит поломка и как проверить. Читай перед правкой render_uniforms, mesh.cpp/meshing, light_buffer/light_grid/blob_buffer, palette_buffer, combined_buffer/cull_pipeline, spatial::cluster_grid и любого файла в shaders/.
---

# Контракты C++ ↔ GLSL

GLSL ничего не импортирует из C++: каждая структура, таблица, номер набора и
привязки ниже записаны минимум дважды и сверяются только руками. У std140 и
std430 нет диагностики на расхождение: ошибки не будет, будут неверные пиксели.
Здесь — что трогать вместе. Почему форматы такие — `docs/rendering.md` и
`docs/lighting.md`.

## Порядок работы

1. Найди свою правку в разделах ниже и открой **все** файлы из раздела до того,
   как править первый.
2. Поменяй все стороны одной правкой, включая копии в тестах.
3. Пройди «Как проверить правку» в конце.

## Кадровый uniform

- **C++:** `uniform_buffer_object`, `directional_light_data`, `fog_data` —
  `engine/gfx/src/render/render_uniforms.cppm`; заполняет
  `renderer::update_uniform_buffer` (`render/renderer.cpp`).
- **GLSL:** `UniformBufferObject` в `shaders/voxel.frag` — полная копия.
  `voxel.vert` и `debug.vert` держат урезанный префикс.
- **Менять вместе:** порядок, тип и выравнивание каждого поля; смысл каждой
  компоненты `vec4`-параметров (`ao_params`, `sky_params`, `lamp_params`,
  `glow_params`, `tonemap_params`, `cluster_params`, `cluster_dims`, `blob_dims`)
  — он виден только там, где пишут, и там, где читают.
- **Сторож:** `static_assert(offsetof(...))` и `sizeof == 848` под структурой.
  Шейдер они не видят: сдвинул поле в C++ — сборка встала, это и есть момент
  сдвинуть его в `voxel.frag`. Сдвиг только в шейдере не ловит ничто.
- **Если разошлись:** неверные пиксели без единой ошибки; а если на чужое место
  попала граница цикла (`point_lights_count`, `cluster_dims.z`, `blob_dims`) —
  зависшее устройство.

Правила:

- Новое поле — только в конец, после `blob_dims`: префикс не сдвигается, и
  урезанные копии остаются верны. Добавь `static_assert` на его смещение и
  поправь `sizeof`.
- `vec3`, `vec4`, матрица, вложенная структура — `alignas(16)`; скаляр —
  `alignas(4)`. Скаляр сразу за `vec3` ложится в последние четыре байта его
  слота (так стоят `color` и `intensity`). Логическое поле — `uint32`, не
  `bool` (`fog.enabled`).
- Урезанные копии верны не до конца. В `voxel.vert` после `ao_params` объявлен
  `point_lights_count` — на смещении 656, где в C++ `cave_ambient` (настоящий
  стоит на 736). В `debug.vert` полей `lightPos`/`lightColor` в C++ нет вовсе.
  Оба шейдера читают только `view`/`proj`. Понадобилось поле в вершинной
  стадии — скопируй префикс из `voxel.frag` целиком до этого поля.
- Новый режим отладки: значение в конец `enum class debug_view` и имя в
  `debug_view_names` (`render/render_settings.cppm`), ветка
  `ubo.debug_view == N` в `voxel.frag` с тем же числом.

## Тени и число каскадов

- **C++:** `shadow_map::cascade_count` (`render/shadow_map.cppm`);
  `directional_light_data`, `shadow_uniform_buffer_object`,
  `shadow_push_constant_data` (`render_uniforms.cppm`); `cull_plane_count`
  (`render/cull_pipeline.cppm`).
- **GLSL:** `const int SHADOW_CASCADES = 5` в `voxel.frag`, `voxel.vert`,
  `shadow.vert`; `vec4 planes[36]` в `cull.comp` — это (каскады + 1) × 6;
  `ShadowUniformBufferObject` и `ShadowPushConstants` в `shadow.vert`.
- **Менять вместе:** число каскадов во всех местах, включая литерал 36.
- **`#define SHADOW_ENABLED 0` в `voxel.frag` убирает чтение, но не раскладку.**
  Рендерер пишет каскады всегда, поэтому `SHADOW_CASCADES`,
  `light_space_matrices`, `cascades` и `shadow_filter` остаются в
  `DirectionalLightData` и при выключенных тенях. Не вычищай их вместе с
  мёртвым кодом. Набор 2 (`shadowMapArray`) в раскладке конвейера тоже остаётся.
- **Сторож:** `static_assert(combined_buffer::cull_pass_count ==
  shadow_map::cascade_count + 1)` в `cull_pipeline.cpp`; смещения кадрового
  uniform (каскады стоят раньше `sky_params`). Литералы в шейдерах не сторожит
  ничто.
- **Если разошлись:** съезжает всё после `directional_light` (см. кадровый
  uniform), а `cull.comp` читает `eye` и `pass_count` мимо.
- Включая тени обратно: солнце уже гасится небесным светом (`sunReach` в
  `voxel.frag`). Второй окклюдер на том же свете затемнит навесы дважды —
  оставить нужно один.

## Квад: упаковка

- **C++:** `quad` (`resource/meshing.cppm`), `quad::pack` (`resource/mesh.cpp`).
- **GLSL:** `struct Quad` и разбор `data0`/`data1`/`data2` в `main` —
  `voxel.vert` и `shadow.vert`. Маски углов разбирает `voxel.frag`: по 2 бита на
  угол у `fragCornersMask` и `fragConvexMask`, по 4 — у неба в младших 16 битах
  `fragLightMask` и у блочного света в старших.
- **Копия в тестах:** `unpack_min`, `unpack_max`, `unpack_normal`,
  `unpack_slot`, `unpack_sky`, `unpack_lamp`, `unpack_ao`, `unpack_convex` в
  `tests/gfx/mesher.test.cpp`.
- **Менять вместе:** сдвиг и маску каждого поля во всех трёх местах. Ширина
  слота вокселя (10 бит, `0x3FF` в `voxel.vert`) — это `voxel_slot_capacity`
  (`engine/core/src/voxels/voxels.cppm`). Координата — 7 бит (0…127),
  протяжённость хранится минус один (1…128); дальше `& 0x7F` молча заворачивает.
- **Сторож:** тесты `[mesh]` через `unpack_*` — только C++-сторона.
  `greedy meshing output is stable` и `full-size greedy meshing output is
  stable` падают на любой смене упаковки: число квадов и дайджест обновляй
  только после проверки картинки. `slots are dense and within the quad's ten
  bits` в `tests/core/voxels.test.cpp`. Разбор в шейдерах не проверяет ничто.
- **Если разошлись:** квады не на месте или растянуты, чужой цвет, свет из
  соседнего угла.
- Почему так: `docs/rendering.md#квад`.

## Оси касательных

- **C++:** `tangent_u_axis`/`tangent_v_axis` для `quad::pack` и те же оси
  векторами `ao_tangent_u`/`ao_tangent_v`, по которым мешер считает углы, —
  оба в `mesh.cpp`.
- **GLSL:** `TANGENT_U_AXIS`/`TANGENT_V_AXIS` и `unpackMax` в `voxel.vert` и
  `shadow.vert`.
- **Копия в тестах:** `u_axis`/`v_axis` внутри `unpack_max`
  (`mesher.test.cpp`).
- **Сторож:** тесты `[mesh]` сверяют с `quad::pack` только копию в тесте.
- **Если разошлись:** протяжённости меняются местами, прямоугольник повёрнут на
  четверть оборота — дыры и нахлёсты на неквадратных гранях.

## Порядок углов и обход

Мешер нумерует углы по своим касательным (`c0…c3` в `compute_corner_darkness`,
`compute_corner_light`, `compute_corner_convexity`), шейдер — по порядку обхода.
Мост между ними — таблицы в `add_quad`.

- **C++:** `winding_to_corner` и `corner_to_ao` в `add_quad` (`mesh.cpp`). Все
  четыре маски — затенение, выпуклость, небо, блочный свет — проходят одну и ту
  же перестановку.
- **GLSL:** `FACE_VERTS` в `voxel.vert` и `shadow.vert` — какой конец коробки
  берёт каждая вершина обхода; `corner_uvs` в `voxel.vert`; в `voxel.frag` маски
  читаются в порядке обхода как `a00, a10, a11, a01` (так же `x…`, `s…`, `l…`)
  и смешиваются по `fragUV`.
- **Индексы:** `combined_buffer_pool::ensure_index_pattern_` пишет
  `0,1,2,2,3,0` на квад, `combined_buffer::write_draw_command_` ставит
  `vertex_offset` = смещение квада × 4, шейдеры берут квад как
  `gl_VertexIndex / 4u`, угол — как `% 4u`.
- **Лицевая сторона:** обход в `FACE_VERTS` вместе с `frontFace =
  eCounterClockwise` и `cullMode` (`eBack` в `renderer::create_graphics_pipeline`,
  `eFront` в `renderer::create_shadow_pipeline`) решает, что отсекается.
- **Копия в тестах:** `face_verts` в `mesher.test.cpp` — копия `FACE_VERTS`.
- **Сторож:** `packed occlusion matches the model at every corner` и такие же
  для `sky light`, `block light`, `convexity` сверяют упакованный угол с моделью
  через `face_verts`. Поменял `FACE_VERTS`, не тронув копию, — тесты проверяют
  старый порядок и остаются зелёными. `packed convexity…` видит только +Y
  (`detail::convex_face`), где две таблицы складываются в тождество:
  перестановку выпуклости он не поймает. Расширяешь выпуклость на другие грани —
  расширь и тест.
- **Если разошлись:** таблица, повёрнутая или отражённая для одной грани из
  шести, кладёт затенение на чужую сторону этой грани — светлый шов там, где
  сходятся две грани. Отражённый обход — грань отсекается и пропадает.

## Номер грани: +X, −X, +Y, −Y, +Z, −Z

Номера 0…5 значат одно и то же везде: `ao_normal` (`mesh.cpp`); порядок квадов и
`mesh::face_counts`; команда на грань `instance * 6 + face` в
`write_draw_command_`; `NORMALS` в `voxel.vert`; ось грани `normal_id >> 1` в
`unpackMax`; `faces_away` в `cull.comp` (0 → `eye.x <= bmin.x`, …);
`face_normal` в `mesher.test.cpp`.

- **Сторож:** нет.
- **Если разошлись:** отсев по направлению выкидывает грани, смотрящие в камеру,
  — стороны мира пропадают под одними углами и возвращаются под другими;
  нормаль и свет чужой грани.

## Отсев индиректа (`cull.comp`)

- **C++:** `cull_frustum_ubo` и `cull_pipeline::update_frustums`; push constant
  и `dispatch((instance_count + 63) / 64, 1, 1)` в `cull_pipeline::dispatch`
  (`render/cull_pipeline.cpp`); `draw_command` (`resource/combined_buffer.cppm`);
  `write_bounds_`, `update_compute_descriptor_set_`, `write_visibility`
  (`combined_buffer.cpp`); чтение результата в `renderer::render_world` и
  `renderer::render_shadow_pass`.
- **GLSL:** `CullFrustumData`, `DrawCommand`, `AABB`, буферы набора 1, блок
  `PC` с `instance_count`, `local_size_x = 64`.
- **Менять вместе:**
  - `AABB.min_point.w` — флаг «оси модели совпадают с мировыми»
    (`is_axis_aligned` в `combined_buffer.cpp`). `faces_away` применяется только
    при нём и только в проходе 0. `max_point.w` не читается.
  - Проход 0 — камера, проход `1 + каскад` — тень. Выход пишется в
    `pass * instance_count * 6 + slot`, счёт — в `counts[pass]`;
    `render_shadow_pass` читает команды со смещения
    `pass_index * max_draws * sizeof(draw_command)`, счёт — с
    `pass_index * sizeof(uint32)`.
  - Привязки набора 1: 0 входные команды, 1 AABB, 2 выход, 3 счётчики,
    4 видимость.
  - `DrawCommand` и `draw_command` повторяют `vk::DrawIndexedIndirectCommand`.
- **Сторож:** слои валидации в Debug ловят привязки и размеры; смысл полей — ничто.
- **Если разошлись:** модели пропадают или мигают при повороте камеры, тень
  рисует список чужого прохода.

## Набор источников и пятен

Одна раскладка на шесть привязок
(`renderer::create_point_lights_descriptor_set_layout`): в `voxel.frag` это
`set = 3`, в `light_cull.comp` — `set = 1`.

| Привязка | GLSL | Пишет C++ |
|---|---|---|
| 0 | `PointLights` | `light_buffer::update_descriptor_set_` |
| 1, 2 | `ClusterCounts`, `ClusterIndices` | `light_grid::rebuild_list_` через `counts_binding_of` / `indices_binding_of` для `cull_list::sources` |
| 3 | `Blobs` | `blob_buffer::write_binding_` |
| 4, 5 | `BlobCounts`, `BlobIndices` | то же для `cull_list::blobs` |

- `point_light_data` (`resource/light_buffer.cppm`) ↔ `PointLightData` в
  `voxel.frag` и `light_cull.comp`: std430, 48 байт с хвостовым выравниванием.
- `blob_data` ↔ `BlobData` там же: `position_radius` (xyz ноги, w радиус пятна),
  `params` (x fall, y strength, z height, w reach), `cull_a` (xyz верх колонки,
  w радиус капсулы), `cull_b` (xyz низ). Пишет `blob_buffer::update`;
  `cull_a`/`cull_b` читают `light_cull.comp` и эталон в `light_grid::dispatch`,
  остальное — `blobShadow` в `voxel.frag`.
- `light_cull_ubo` (`light_buffer.cppm`, пишет `light_grid::write_params_`) ↔
  `CullParams` в `light_cull.comp`; `cull_dims.w` — значение `cull_list`
  (0 источники, 1 тела).
- **Сторож:** `static_assert` нет ни на одной из этих структур. Позицию,
  `range` и `cull_*` косвенно сверяет `--verify-lights=N`; цвет, силу и
  `params` — только картинка.
- **Если разошлись:** свет не там или его нет, пятна не под телами; сдвинутый
  `range` даёт пустые или переполненные списки кластеров.
- Буферы растут по кадру в полёте, и дескриптор переписывается только у
  записываемого кадра — `docs/rendering.md#кадры-в-полёте`.
- Почему так: `docs/lighting.md#динамические-источники`,
  `docs/lighting.md#пятна-под-телами`.

## Кластерная сетка

Эталон — `spatial::cluster_grid`, `scatter_slice` и `cluster_lights` в
`engine/core/src/spatial/spatial.cppm`. `clusterOf` в `voxel.frag` и `main` в
`light_cull.comp` — их переводы; при расхождении прав C++.

- **Номер кластера** `((slice * tiles_y) + tile_y) * tiles_x + tile_x`:
  `cluster_grid::cluster_index`, `clusterOf`, `main` в `light_cull.comp`.
- **Срез:** `cluster_params` = (`z_scale`, `z_bias`, `tile_size`, `slices`)
  пишут и `update_uniform_buffer`, и `write_params_`; `slice_of` ↔ `clusterOf`,
  `z_range_of` ↔ `slab_near`/`slab_far` в `light_cull.comp`.
- **Глубина вида положительна перед камерой:** `viewDepth = -(view * p).z` в
  `voxel.vert`, `forwards` в `light_cull.comp`, `to_view` в
  `light_grid::dispatch`.
- **Список кластера** — `indices[cluster * cap + n]`. `cap` пишется дважды:
  `cluster_dims.z`/`blob_dims.x` кадрового uniform и `cull_dims.x` параметров
  отсева — оба из `cluster_settings` одного кадра. Разойдутся — фрагмент читает
  чужие ячейки.
- **Счётчиков `cluster_count + 1`** (`light_grid::counts_size_`): последний —
  счёт переполнений. `light_cull.comp` пишет его в `counts[cull_dims.z]`, где
  `cull_dims.z` = `cluster_count`; его же ждут `cluster_readback::counts`,
  `spatial::check_clusters` и `cluster_probe::account_` в testbed. Счётчик за
  пределом не зажимается, поэтому фрагмент берёт `min(count, cap)` — не убирай.
- **Диспетчеризация:** `dispatch(shape_count, slices, 1)` в
  `light_grid::record_` ↔ `gl_WorkGroupID.x` — фигура, `.y` — срез.
- **`cluster_dims.w`** (`cluster_settings::enabled`): 0 — фрагмент без сетки
  обходит все `point_lights_count` источников и `blob_dims.y` тел. Оба пути
  держатся, чтобы картинку можно было сверить (`--no-clusters`).
- **Сторож:** тесты `[cluster]` в `tests/core/cluster_grid.test.cpp` — только
  эталон; `--verify-lights=N` сверяет списки GPU с эталоном. Чтение во фрагменте
  не сверяет никто: режимы отладки `light_complexity` и `blob_complexity`
  показывают занятость, белым — переполнение.
- **Если разошлись:** свет обрезан по границам тайлов или срезов, пятна
  пропадают с расстоянием, чтение за концом буфера индексов.
- Почему так: `docs/rendering.md#кластерный-свет`.

## Палитра

- **C++:** `palette_buffer` (`resource/palette_buffer.cpp`) — `vec4` на тип в
  порядке `voxel_registry::all()`: rgb — цвет через `palette_gamma`,
  a — `material.glow / 255`.
- **GLSL:** `PaletteBuffer` (`set = 4`) в `voxel.vert` по индексу
  `(data1 >> 14) & 0x3FF`, то есть по слоту вокселя; `voxel.frag` умножает
  `fragColor.a` на `glow_params.x`.
- **Менять вместе:** номер в `all()` обязан совпадать со слотом (сторож —
  `slots are dense and within the quad's ten bits`); альфа — свечение, а не
  прозрачность.
- **Если разошлись:** чужие цвета, светится не то.

## Наборы и вершинный вход

| Конвейер | Набор | C++ | GLSL |
|---|---|---|---|
| мировой | 0 | `uniform_buffer_object` | `UniformBufferObject` |
| | 1 | `combined_buffer::update_descriptor_set_`: 0 модели, 1 нормали, 2 квады | `ModelMatrices`, `NormalMatrices`, `Quads` |
| | 2 | `shadow_map_descriptor_sets_` | `shadowMapArray`, только при `SHADOW_ENABLED` |
| | 3 | набор источников и пятен | см. выше |
| | 4 | `palette_buffer` | `PaletteBuffer` |
| теневой | 0 | `shadow_uniform_buffer_object` | `ShadowUniformBufferObject` |
| | 1 | тот же набор квадов | те же три буфера |
| оба | location 2 | `quad::get_attribute_descriptions`: `eR32Uint`, по инстансу | `in uint inInstanceIndex` |

Номера наборов в `bindDescriptorSets` у `render_world` и `render_shadow_pass`
обязаны совпадать с `set = N` в шейдерах. Здесь расхождение ловят слои
валидации.

## Как проверить правку

1. **Сборка.** Шейдеры компилирует `glslc` в таргете `vw_compile_shaders`
   (`cmake/shaders.cmake`), он входит в обычную сборку — навык `cmake-build`.
   Только шейдеры — `cmake --build build/release --target vw_compile_shaders`
   или агент `shader-check`. Компиляция ловит синтаксис, а не расхождение с C++.
2. **Тесты.** `gfx_tests` (мешер, упаковка, углы) и `core_tests` (эталон
   кластеров, слоты): `ctest --test-dir build/tests --output-on-failure`.
   `gfx_tests` существует только при `VW_BUILD_GFX=ON`.
3. **Слои валидации.** Debug-сборка testbed: в `testbed.log` не должно быть строк
   `Validation layer:`. Слои ловят наборы, привязки, размеры буферов и push
   constant, но не смещения внутри std140/std430.
4. **Списки кластеров.** `testbed.exe --scene=clustered-lights --bench
   --bench-frames=600 --verify-lights=30 --bench-out=verify.txt`, затем то же с
   `--scene=blob-shadows`. В подразделе `verify` блоков `clusters (sources)` и
   `clusters (bodies)` должно быть `frames_disagreed` 0; расхождение ещё и
   попадает в лог строкой `verify-lights (…)`. Значение — только через `=`:
   голый `--verify-lights` читается как 0 и не сверяет ничего. Условия запуска
   (каталог экзешника, окно на переднем плане) — навык `render-bench`.
5. **Картинка.** Сцена с `--no-clusters` и без должна выглядеть одинаково.
   Режимы `debug_view` показывают затенение, нормали, небо, выпуклость, блочный
   свет, пятна и занятость кластеров по отдельности. Шов на одной грани из шести
   — порядок углов; грань, пропадающая под определённым углом камеры, — номер
   грани или флаг `min_point.w`.
6. **Дайджесты.** Меняешь упаковку квада осознанно — обнови число квадов и
   дайджест в `mesher.test.cpp` только после шага 5.
