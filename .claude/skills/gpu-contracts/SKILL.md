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

- **C++:** `uniform_buffer_object`, `directional_light_data`, `fog_data`,
  `corner_shading_data`, `cluster_data` —
  `engine/gfx/src/render/render_uniforms.cppm`; заполняет
  `renderer::update_uniform_buffer` (`render/renderer.cpp`).
- **GLSL:** `UniformBufferObject` с вложенными `CornerShadingData` и
  `ClusterData` в `shaders/voxel.frag` — полная копия. `voxel.vert` и
  `debug.vert` объявляют только `view` и `proj`.
- **Менять вместе:** порядок, тип и выравнивание каждого поля, включая поля
  вложенных `corner_shading` и `clusters`; смысл каждой компоненты
  `vec4`-параметров (`sky_params`, `lamp_params`, `glow_params`,
  `tonemap_params`, `blob_dims`) — он виден только там, где пишут, и там, где
  читают.
- **Сторож:** `static_assert(offsetof(...))` на каждое поле от `corner_shading`
  (640) до `light_wrap` (896), включая `cave_ambient` (656) и `clusters` (800),
  `sizeof == 960` под структурой и размеры вложенных `corner_shading_data` и
  `cluster_data`. Шейдер они не видят: сдвинул поле в C++ — сборка встала, это и
  есть момент сдвинуть его в `voxel.frag`. Сдвиг только в шейдере не ловит ничто.
- **Если разошлись:** неверные пиксели без единой ошибки; а если на чужое место
  попала граница цикла (`point_lights_count`, `clusters.cap`, `blob_dims`) —
  зависшее устройство.

Правила:

- Новое поле — только в конец, после `light_wrap`: префикс не сдвигается, и
  урезанные копии остаются верны. Добавь `static_assert` на его смещение и
  поправь `sizeof`.
- `vec3`, `vec4`, матрица, вложенная структура — `alignas(16)`; скаляр —
  `alignas(4)`. Скаляр сразу за `vec3` ложится в последние четыре байта его
  слота (так стоят `color` и `intensity`). Логическое поле — `uint32`, не
  `bool` (`fog.enabled`).
- Вершинным шейдерам нужны только `view`/`proj`. Понадобилось поле в вершинной
  стадии — скопируй префикс из `voxel.frag` целиком до этого поля.
- Новый режим отладки: значение в конец `enum class debug_view` и имя в
  `debug_view_names` (`render/render_settings.cppm`), ветка
  `ubo.debug_view == N` в `voxel.frag` с тем же числом.

## Тени и число каскадов

- **C++:** `shadow_map::cascade_count` (`render/shadow_map.cppm`);
  `directional_light_data`, `shadow_uniform_buffer_object`,
  `shadow_push_constant_data` (`render_uniforms.cppm`); `cull_plane_count`
  (`render/cull_pipeline.cppm`).
- **GLSL:** `const int SHADOW_CASCADES = 5` в `voxel.frag` и `shadow.vert`;
  `vec4 planes[36]` в `cull.comp` — это (каскады + 1) × 6;
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

- **C++:** `quad` (`resource/meshing.cppm`) — два слова, 8 байт
  (`static_assert(sizeof(quad) == 8)`); `quad::pack` и `quad::sway_flag`
  (`resource/mesh.cpp`).
- **GLSL:** `struct Quad` и разбор `data0`/`data1` в `main` — `voxel.vert`,
  `shadow.vert` и `grass.vert`; `SWAY_FLAG` в `voxel.vert` и `shadow.vert`.
- **Копия в тестах:** `unpack_min`, `unpack_max`, `unpack_normal`,
  `unpack_slot`, `unpack_sways` в
  `tests/gfx/mesher.test.cpp` — собственный повтор разбора `voxel.vert`
  литеральными сдвигами и масками, а не обращение к `quad`.
- **Менять вместе:** сдвиг и маску каждого поля во всех местах. Воксель — 8 бит
  (`0xFF` в шейдерах), каталог живёт в байте, `voxel_type_capacity`
  (`engine/core/src/voxels/voxels.cppm`); бит 22 — флаг качания, биты `31:23` свободны.
  Координата — 7 бит (0…127), протяжённость хранится минус один (1…128); дальше
  `& 0x7F` молча заворачивает. Шаг записи в буфере `Quads` — `sizeof(quad)`, в
  std430 это 8 байт: третье поле в `struct Quad` сдвинет все квады.
- **Решётка ветра.** `quad::sway_lattice` (`meshing.cppm`) и `SWAY_LATTICE` в
  `shaders/include/leaf_sway.glsl` — одно число, 8. Мешер не сливает листву
  через плоскости решётки, шейдер смешивает ветер между её узлами. Разойдутся —
  в кронах щели, в которые видно небо; сторож только у стороны C++ (`a block of
  one leaf tone merges up to the wind lattice and no further`).
- **Байты `31:24`.** У `data0` — номер материала (см. «Палитра»). У `data1`
  биты `25:23` свободны, `31:26` заняты под код состояния; все нулевые. Весов качания по углам
  больше нет: листва движется вся одинаково, а мешер оставляет грани на её стыке
  с остальным (`docs/rendering.md#качание-листвы`). Затенения, выпуклости и света в кваде нет: их считает фрагмент (см. «Занятость»
  и «Кеш освещённости»).
- **Сторож:** тесты `[mesh]` через `unpack_*` — только C++-сторона.
  `greedy meshing output is stable` и `full-size greedy meshing output is
  stable` падают на любой смене упаковки и слияния: число квадов и дайджест
  обновляй только после проверки картинки. Флаг и нули в свободных битах стережёт
  `every leaf quad sways, stays inside one lattice cell and carries nothing above
  the flag`, грани на стыке — `a
  leaf and the wood it touches both keep the face between them`, шов — `a leaf on
  the chunk seam keeps its face against wood and drops it against leaves`. Разбор в шейдерах не проверяет ничто.
- **Если разошлись:** квады не на месте или растянуты, чужой цвет, лист качается
  не тем углом.
- Почему так: `docs/rendering.md#квад`.

## Оси касательных

- **C++:** `quad::tangent_u_axis`/`tangent_v_axis` (`resource/meshing.cppm`)
  для `quad::pack` и те же оси векторами `ao_tangent_u`/`ao_tangent_v` в
  `mesh.cpp`, по которым мешер считает углы.
- **GLSL:** `TANGENT_U_AXIS`/`TANGENT_V_AXIS` и `unpackMax` в `voxel.vert` и
  `shadow.vert`.
- **Копия в тестах:** `voxel_vert_tangent_u_axis`/`voxel_vert_tangent_v_axis` в
  `mesher.test.cpp` — копия таблиц шейдера для `unpack_max`: упаковку мешера
  сверяет с контрактом `voxel.vert`, независимо от таблиц `quad`.
- **Сторож:** тесты `[mesh]` сверяют с `quad::pack` только копию в тесте.
- **Если разошлись:** протяжённости меняются местами, прямоугольник повёрнут на
  четверть оборота — дыры и нахлёсты на неквадратных гранях.

## Обход

Масок по углам в кваде нет, поэтому порядок углов мешера шейдеру больше не
нужен: остался только порядок обхода.

- **GLSL:** `FACE_VERTS` в `voxel.vert`, `shadow.vert` и `grass.vert` — какой
  конец коробки берёт каждая вершина обхода.
- **Индексы:** `combined_buffer_pool::ensure_index_pattern_` пишет
  `0,1,2,2,3,0` на квад, `combined_buffer::write_draw_command_` ставит
  `vertex_offset` = смещение квада × 4, шейдеры берут квад как
  `gl_VertexIndex / 4u`, угол — как `% 4u`.
- **Лицевая сторона:** обход в `FACE_VERTS` вместе с `frontFace =
  eCounterClockwise` и `cullMode` (`eBack` в `renderer::create_graphics_pipeline`,
  `eFront` в `renderer::create_shadow_pipeline`) решает, что отсекается.
- **Сторож:** нет. Отражённый обход — грань отсекается и пропадает.
  Отражённый обход — грань отсекается и пропадает.

## Номер грани: +X, −X, +Y, −Y, +Z, −Z

Номера 0…5 — значения `vw::face_direction`
(`engine/core/src/voxels/face_direction.cppm`), и значат они одно и то же везде:
`offset_of(face)` и таблицы `per_face` мешера; порядок квадов и
`mesh::face_counts`; команда на грань `instance * 6 + face` в
`write_draw_command_`; `NORMALS` в `voxel.vert`; ось грани `normal_id >> 1` в
`unpackMax`; `faces_away` в `cull.comp` (0 → `eye.x <= bmin.x * eye.w`, …);
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
  - `eye` — однородная точка, её пишет `camera::culling_eye`: `w = 1` — положение
    глаза (перспектива), `w = 0` — направление на камеру (ортогональная
    проекция). `faces_away` умножает грань коробки на `eye.w`; убрать умножение
    — и ортогональный вид потеряет грани по одну сторону от начала координат.
  - Проход 0 — камера, проход `1 + каскад` — тень. Выход пишется в
    `pass * instance_count * 6 + slot`, счёт — в `counts[pass]`;
    `render_shadow_pass` читает команды со смещения
    `pass_index * max_draws * sizeof(draw_command)`, счёт — с
    `pass_index * sizeof(uint32)`.
  - Привязки набора 1: 0 входные команды, 1 AABB, 2 выход, 3 счётчики,
    4 видимость.
  - `DrawCommand` и `draw_command` повторяют `vk::DrawIndexedIndirectCommand`.
- **Сторож:** `static_assert` на смещения и размер у обеих структур:
  `cull_frustum_ubo` (`planes` 0, `eye` 576, `pass_count` 592, `pad` 596,
  `sizeof == 608` при пяти каскадах) в `cull_pipeline.cppm` и `draw_command`
  (0, 4, 8, 12, 16, `sizeof == 20` и равенство
  `sizeof(vk::DrawIndexedIndirectCommand)`) в `combined_buffer.cppm`. Сдвинул
  поле в C++ — сборка встала, это и есть момент сдвинуть его в `cull.comp`.
  Сдвиг только в шейдере не ловит ничто. Слои валидации в Debug ловят привязки и
  размеры; смысл полей — ничто.
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
  `CullParams` в `light_cull.comp`, поля с теми же именами; `list` — значение
  `cull_list` (там же: 0 источники, 1 тела), шейдер сравнивает его с
  `cull_list_sources`.
- **Сторож:** `static_assert` на смещения и размер у всех трёх структур:
  `light_cull_ubo` (`z_scale` 64, `near_depth` 80, `screen_width` 96, `cap` 112,
  `list` 124, `orthographic` 128, `sizeof == 144`), `point_light_data` (`position` 0, `color` 16,
  `intensity` 32, `range` 36, `sizeof == 48`) и `blob_data` (`position_radius`
  0, `params` 16, `cull_a` 32, `cull_b` 48, `sizeof == 64`). Сдвиг только в
  шейдере не ловит ничто: позицию, `range` и `cull_*` косвенно сверяет
  `--verify-lights=N`; цвет, силу и `params` — только картинка.
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
- **Срез:** `z_scale`, `z_bias`, `tile_size`, `slices` пишутся дважды — в
  `clusters` кадрового uniform (`update_uniform_buffer`) и в одноимённые поля
  `light_cull_ubo` (`write_params_`); `slice_of` ↔ `clusterOf`, `z_range_of` ↔
  `slab_near`/`slab_far` в `light_cull.comp`.
- **Проекция размаха:** `projected_span` в `spatial.cpp` и в `light_cull.comp`
  — четыре деления на глубину, а при `cluster_grid::orthographic`
  (`params.orthographic`) размах возвращается как есть. Флаг пишет
  `renderer::get_cluster_grid` из `camera::is_orthographic`. Ветка только в
  одной из сторон — и списки расходятся с эталоном на всей сетке; ловит
  `--verify-lights=N --orthographic=ВЫСОТА`.
- **Глубина вида положительна перед камерой:** `viewDepth = -(view * p).z` в
  `voxel.vert`, `to_view_depth` в `light_cull.comp` и в `light_grid::dispatch`.
- **Список кластера** — `indices[cluster * cap + n]`. `cap` пишется дважды:
  `clusters.cap`/`blob_dims.x` кадрового uniform и `cap` параметров отсева — оба
  из `cluster_settings` одного кадра. Разойдутся — фрагмент читает чужие ячейки.
- **Счётчиков `cluster_count + 1`** (`light_grid::counts_size_`): последний —
  счёт переполнений. `light_cull.comp` пишет его в `counts[overflow_slot]`, где
  `overflow_slot` = `params.cluster_count`, а `light_grid::harvest_` при вычитке
  делит буфер на `cluster_readback::cluster_counts` и `overflow_count`. О
  хвостовом слоте знают только эти трое: `spatial::check_clusters` и
  `cluster_probe::account_` в testbed получают две части порознь. Счётчик за
  пределом не зажимается, поэтому фрагмент берёт `min(count, cap)` — не убирай.
- **Диспетчеризация:** `dispatch(shape_count, slices, 1)` в
  `light_grid::record_` ↔ `gl_WorkGroupID.x` — фигура, `.y` — срез.
- **`clusters.enabled`** (`cluster_settings::enabled`): 0 — фрагмент без сетки
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

- **C++:** `palette_buffer` (`resource/palette_buffer.cpp`) — запись
  `palette_entry` на тип в порядке `voxel_registry::all()`: `color` — цвет через
  `palette_gamma`, `w` не читается. Рядом — запись `material_entry` на строку
  `material_table::all()`: `look.x` — `glow / 255`, остальное свободно.
- **GLSL:** `PaletteBuffer` (`set = 4`, привязка 0) из `PaletteEntry`
  (`vec4 color`) в `voxel.vert` и `grass.vert` по индексу
  `(data1 >> 14) & 0xFF`, то есть по вокселю; `MaterialBuffer` (привязка 1) из
  `MaterialEntry` (`vec4 look`) по индексу `data0 >> 24`. `look.x` уходит в
  `fragGlow` (location 2, component 3), и `voxel.frag` умножает его на
  `glow_params.x`.
- **Номер материала в кваде** пишет `quad::pack` сдвигом `quad::material_shift`
  (24); биты `31:26` второго слова (`quad::state_shift`) заняты под код
  состояния и пока нулевые. Таблицу на GPU и номер в кваде строят из одного
  реестра — `material_table{registry}` и `default_material_table()`; с другим
  реестром в рендерере строки разойдутся.
- **Менять вместе:** номер в `all()` обязан совпадать со слотом (сторож —
  `slots are dense and within the quad's ten bits`); раскладку обеих записей —
  по 16 байт (сторож — `static_assert` в `palette_buffer.cpp`); привязки набора
  в `renderer::create_palette_descriptor_set_layout`. Номер материала в кваде
  стережёт `a quad carries the material of its voxel and an empty state code`,
  чтение в шейдере — ничто.
- **Если разошлись:** чужие цвета, светится не то.

## Адрес углов в нормальной матрице

- **C++:** `normal_matrix_of` в `resource/combined_buffer.cpp` — столбец 3
  нормальной матрицы (`normal[row, 3]`, float 12…15 в памяти) равен
  `(0, 0, packed_size, word_offset)` из `instance_corners`
  (`resource/model_occupancy.cppm`). Трава кладёт те же два числа в
  `grass_instance::light.xy`.
- **GLSL:** `normalMatrix[3]` → `fragInstanceLight` (location 8) в `voxel.vert`;
  `grass.vert` собирает `vec4(0, 0, inst.light.xy)`. `voxel.frag` читает только
  `.z` и `.w`: `z > 0.5` — свой объём (`modelVolumeOf`), `z > −0.5` — мировая
  сетка, иначе углов нет.
- **Менять вместе:** смысл `z` и `w` с обеих сторон и у травы; номер location в
  трёх шейдерах. `x` и `y` свободны и нулевые: света инстанса больше нет, его
  читает фрагмент из кеша. Нормаль берёт из матрицы только `mat3` — столбец 3 в
  неё не попадает.
- **Сторож:** нет.
- **Если разошлись:** углы модели читаются из чужого объёма (рябь по
  поверхности) или из мира (затенение не там, где грани).
- Почему так: `docs/rendering.md#объёмы-моделей`.

## Занятость

- **C++:** раскладка — `spatial::occupancy_clipmap_layout`, адресация —
  `occupancy_window_origin`, `occupancy_slot_of`, `occupancy_slot_index`,
  `occupancy_brick_bit`, обход — `march_occupancy`
  (`engine/core/src/spatial/occupancy.cppm`); упаковка блока —
  `asset::pack_occupancy_bricks`; `occupancy_params` и запись текстур —
  `resource/occupancy_clipmap.cppm/.cpp`; `occupancy_view_push` —
  `render/occupancy_view.cppm`.
- **GLSL:** `shaders/include/occupancy.glsl` — `OccupancyParams`,
  `occupancyBricks[3]`, `occupancyKnows`, `occupancyAt`, `occupancyBrickBit`,
  `marchOccupancy`; `OccupancyViewPush` в `occupancy_view.frag`. Включающий файл
  обязан до `#include` задать `OCCUPANCY_SET`.
- **Менять вместе:**
  - числа раскладки: окно 16 × 8 × 16, 16³ и 16³ чанков
    (`window_chunks_at`), сдвиг чанка 6, маска текселя `(511, 255, 511)`, 255 и
    127 (`OCCUPANCY_FINE_MASK`, `OCCUPANCY_TEXTURE_MASK`,
    `OCCUPANCY_COARSE_MASK`), первый бит известности уровня (0, 2048, 6144),
    80 слов `uvec4` — в шейдере это литералы; окно уровня 0 не куб, и номер
    слота — `x + ширина · (y + высота · z)` с обеих сторон;
  - порядок битов в блоке `x | y << 1 | z << 2` — упаковщик, эталон и шейдер;
  - порядок текселей при записи: `x` быстрее всех, затем `y`, затем `z` — так
    пишет `pack_occupancy_bricks` и так читает `copyBufferToImage`;
  - слово известности: бит `n` лежит в `valid[n >> 7][(n >> 5) & 3]`, разряд
    `n & 31` (`write_params_` ↔ `occupancyKnows`);
    бит стоит у слота, чьи блоки записаны **и не все нулевые**: чанк из воздуха
    шейдеру неизвестен, чтобы луч шагал через него разом
    (`docs/rendering.md#что-известно-а-что-нет`);
  - обход: размеры шага (64, нулевой блок грубого уровня, две клетки, клетка),
    порядок проверки грубых уровней от самого грубого, нижняя граница уровня
    (`finestLevel`), сдвиг `1e-3`, предел в 192 шага и правило «неизвестный чанк
    пуст». `marchOccupancy` — построчный перевод
    `march_occupancy`; при расхождении прав C++;
  - `occupancy_view_push` (112 байт): `eye` — точка в вокселях от угла
    `base_chunk`, `w` — дальность; `corners` — направления в углы кадра в порядке
    верх-лево, верх-право, низ-право, низ-лево; `tonemap`; `base_chunk`.
- **Наборы:** у вида занятости `set = 0`: 0 — `OccupancyParams`, 1 — три
  `usampler3D`. Набор на кадр в полёте: параметры у каждого кадра свои, текстуры
  общие.
- **Сторож:** `static_assert` на `occupancy_params` (`valid` 48, размер 1328) и на
  `occupancy_view_push` (16, 80, 96, размер 112); тесты `[occupancy]` в
  `tests/core/occupancy_march.test.cpp` и `tests/asset/occupancy_bricks.test.cpp`
  — только C++. Шейдер не сверяет ничто, кроме глаза: `--debug-view=occupancy`
  обязан дать те же силуэты, что обычный кадр.
- **Если разошлись:** в виде занятости мир зеркален или собран из перемешанных
  кубов 64³ (адресация слотов), изрыт дырами по сетке 2 × 2 × 2 (порядок битов),
  либо чанки пропадают целиком (слово известности).
- Почему так: `docs/rendering.md#занятость-на-gpu`.

## Кеш освещённости

- **C++:** `light_cache` (`resource/light_cache.cppm/.cpp`): `shapes` — сторона,
  сдвиг клетки и число проходов на каскад, блок 8 текселей, `light_cache_push`,
  `wrap_of`; хвост кадрового uniform `light_grid` (880) и `light_wrap[4]` (896),
  пишет `update_uniform_buffer`.
- **GLSL:** `shaders/include/light_cascades.glsl` — `LightPush`, три хранимых
  образа, `CELL_SHIFT`, `SIDE_MASK`; `shaders/light_cache.comp` — `LightBricks`,
  `BRICK_TEXELS`; `shaders/light_sources.comp` — `LightSources`; `voxel.frag` —
  `lightCascades[3]` (`set = 6`), `cascadeLight`, `cachedLight`, `LIGHT_CELL`,
  `LIGHT_SPAN`, `LIGHT_EDGE`.
- **Менять вместе:**
  - число каскадов, сторона и клетка: `cascade_count`/`shapes` ↔
    `CELL_SHIFT`/`SIDE_MASK`/`LAST_CASCADE` в compute ↔
    `LIGHT_CELL`/`LIGHT_SPAN`/`LIGHT_EDGE` во фрагменте. Массив `light_wrap` в
    uniform длиной 4, занято три;
  - каскад `k` читает занятость уровня `k`: клетка каскада равна блоку этого
    уровня, а правило «сплошная» считает его биты. Сменил клетку — смени уровень;
  - запись очереди `ivec4`: xyz — блок в единицах блоков своего каскада, w —
    каскад в младших трёх битах и флаги: 8 — проход засевает небо, 16 —
    последний проход блока, 32 — обнулить `R` (слот отдан новому блоку), 64 —
    рядом есть излучатели. В C++ это `seeds_from_sky`, `last_pass`,
    `wipes_sources`, `near_sources`, в compute — те же имена заглавными; одна
    рабочая группа на запись (`dispatch(блоков, 1, 1)`, `local_size` 8³);
  - запись списка излучателей `ivec4` для `light_sources.comp`: xyz — клетка в
    текселях своего каскада, w — каскад в младших трёх битах, уровень 0–15 со
    сдвигом 8 (`source_level_shift` ↔ `LEVEL_SHIFT`); рабочая группа — 64 записи,
    число записей едет в `base_chunk.w`;
  - `light_cache_push` (64 байта): `base_chunk` — чанк камеры, `window[k]` —
    угол окна каскада в его текселях. Блок объявлен один раз, в
    `include/light_cascades.glsl`, вместе с образами, `CELL_SHIFT`, `SIDE_MASK`
    и `FULL_LEVEL`;
  - каналы: `A` — уровень неба, `B` — доля клетки под небом, `G` — уровень
    излучателей, `R` — излучение клетки; `R` пишет только `light_sources.comp`
    и обнуляет флаг 32, остальное пишет `light_cache.comp`, читает
    `cachedLight` (`.a` и `.g`). Очистка и «открыто» за последним каскадом —
    `(0, 0, 0, 1)`: `make_ready` ↔ `LIGHT_OPEN_SKY` ↔ `OPEN_SKY`;
  - `SOURCE_STRIDE` (1,25) в compute — тот же множитель, что `round_reach`
    (0,8) у динамических источников, записанный обратным числом;
  - шаг заливки `клетка / 15` в compute и запас пометки `flood_reach_voxels`
    (16) в C++: свет уходит на пятнадцать вокселей, и блок дальше запаса от
    правки не пересчитывается;
  - `light_wrap[k].xyz` — доля охвата каскада, на которую сдвинут угол чанка
    камеры: без неё тороидальная выборка читает чужой тексель. `w` свободны;
  - `LIGHT_EDGE` (96, 192, 480 вокселей) обязан быть меньше гарантированной
    половины окна: окно — половина охвата вокруг угла блока, камера не дальше
    четырёх текселей от него;
  - образы живут в раскладке `eGeneral` всегда: и хранимый, и выбираемый
    дескриптор объявлены с ней.
- **Наборы:** compute — `set = 0` занятость, `set = 1`: 0–2 каскады, 3 очередь
  блоков, 4 список излучателей (на кадр в полёте); раскладка у обоих конвейеров
  одна. Фрагмент — `set = 6`, один набор на все кадры.
- **Сторож:** `static_assert` на `light_cache_push` (16, размер 64) и на
  смещения 880, 896, размер 960 кадрового uniform. Шейдеры не сверяет ничто:
  правку заливки проверяет глаз по режимам просмотра `sky light` и `block light`.
- **Если разошлись:** свет сдвинут на долю каскада или повторяется плиткой
  (`light_wrap`), блоки 8³ пятнами не на своих местах (очередь), свет обрезан
  квадратом вокруг камеры (`LIGHT_EDGE`), тени не досчитываются до края (число
  проходов меньше, чем клеток в пятнадцати шагах).
- Почему так: `docs/lighting.md#кеш-освещённости`.

## Углы из занятости

- **C++:** `uniform_buffer_object::occupancy_eye` (848: xyz — глаз в вокселях от
  угла чанка камеры, w свободна) и `occupancy_base` (864: чанк камеры);
  `world_push_constant_data::grid` (16: xyz — тот же угол в мировых единицах,
  w — единица на размер вокселя), пишет `renderer::grid_push_`;
  `instance_corners` (`resource/model_occupancy.cppm`) и `normal_matrix_of` в
  `resource/combined_buffer.cpp`;
  `model_occupancy_buffer` и `model::build_bit_rows`.
- **GLSL:** `WorldPush.grid` и выход `fragGridPos` (location 9, `centroid`) в
  `voxel.vert`; `fragInstanceLight` — теперь `vec4` (location 8); `FLAT_ONLY`
  (бит 8 маски выпуклости) и `FACE_SHIFT` (номер грани в битах 9–11) в
  `voxel.vert` и `voxel.frag`; `ModelOccupancy` (привязка 2 набора занятости),
  `modelVolumeOf`, `modelSolid`, `modelPatch` в `occupancy.glsl`; хвост
  `UniformBufferObject`, `cornersFromOccupancy`, `cornersAcross`, `cornerLevel`
  в `voxel.frag`; `occupancyBricksAround` и `occupancyPatch` в `occupancy.glsl`.
  `voxel.vert` и `grass.vert` отдают location 7 (`fragConvexMask`), 8 и 9;
  location 4–6 свободны. Трава кладёт адрес объёма пучка
  (`model_occupancy_buffer::keep_copy`) и позицию в вокселях пучка.
- **Менять вместе:**
  - `fragGridPos` считается **до** `leafSway`; перенос ниже сдвигает клетку у
    качающейся листвы;
  - правило угла живёт только в `cornerLevel` (два занятых ребра дают 3);
    порядок битов слоя — бит `j * 3 + i`, `i` вдоль `u`, `j` вдоль
    `v`, оси `u = (ось + 1) % 3`, `v = (ось + 2) % 3`;
  - склейка блоков: байт `(вдоль u) + 2 · (вдоль v)` слова `packed`, внутри байта —
    порядок битов блока из раздела «Занятость»;
  - световой столбец: `z = 0` — занятость мира, `z > 0` — свой объём с размерами
    `w | h << 8 | d << 16`, `z < 0` — углов нет; `w` — смещение объёма в
    словах. Пишут `allocate` и `write_transform`;
  - `fragGridPos` у инстанса со своим объёмом — локальная позиция вершины, у
    остальных — мировая в сетке; ветка в `voxel.vert` и чтение в `voxel.frag`
    смотрят на один и тот же знак `z`;
  - раскладка объёма: слов в строке `(ширина + 31) >> 5`, слово
    `смещение + (y + высота · z) · слов_в_строке + (x >> 5)` — `build_bit_rows` ↔
    `modelSolid`;
  - за краем модели: 0 для AO, 1 для выпуклости (`beyond` в `modelPatch`);
  - `CORNER_REACH` обязан не превышать гарантированную половину окна уровня 0
    (`occupancy_window_origin`): 480 по горизонтали и 224 по высоте; сейчас
    `CORNER_REACH` — `(416, 224, 416)`, по горизонтали он ещё и не заходит за
    начало LOD.
- **Сторож:** `static_assert` на смещения 848 и 864 и размер 880 у кадрового
  uniform, 16 и размер 32 у `world_push_constant_data`; раскладку объёма модели
  держит тест `bit rows of a model of any size say which voxels are there`. Ядро
  во фрагменте не сверяет ничто: запечённых углов, с которыми его сравнивали,
  больше нет. Правку проверяет глаз по видам `ambient occlusion` и `convexity`.
- **Если разошлись:** AO на чужой стороне грани или сдвинут на клетку; персонаж
  без AO либо с полосами от рельефа под ним; тёмные точки на кронах в ветер.
- Почему так: `docs/rendering.md#затенение-углов-во-фрагменте`.

## Тоновая кривая и композит

- **GLSL:** `displayFromScene` и `sceneFromDisplay` в
  `shaders/include/tonemap.glsl`; включают его `composite.frag` (прямая),
  `voxel.frag` (обратная в `shown` для режимов `debug_view`) и `debug.frag`
  (обратная для цвета примитива).
- **C++:** `display_from_scene` и `scene_from_display` в
  `render/render_settings.cppm`; обратной рендерер переводит цвет очистки
  (`render_world_pass`) и цвет тумана (`update_uniform_buffer`).
- **Параметры** — `(exposure, max(white_point, 0.01))`, одна функция
  `renderer::tonemap_push_`: в `tonemap_params` кадрового uniform, в
  push-константу отладочных конвейеров (`DebugPush`, 16 байт, фрагментный шаг) и,
  отдельно, в `post_process::draw_composite`.
- **`post_push_constants`** (`render/post_process.cppm`, 16 байт) ↔ `PostPush` в
  `bloom_down.frag`, `bloom_up.frag`, `composite.frag`. У bloom `xy` — размер
  текселя источника, `z` — «источник несёт долю свечения в альфе» (первый шаг);
  у композита `xy` — параметры кривой, `z` — сила bloom, ноль выключает выборку.
- **Альфа образа сцены** — доля свечения: пишет `voxel.frag` (`glowShare`),
  читает первый шаг `bloom_down.frag` как `rgb * a`. Конвейер, пишущий в сцену
  что-то кроме вокселей, обязан либо не писать альфу, либо писать ноль.
- **Наборы:** у bloom `set = 0` — источник; у композита `set = 0` — сцена,
  `set = 1` — уровень bloom либо та же сцена, когда bloom выключен.
- **Сторож:** `a display colour survives the trip into the scene and back` в
  `tests/gfx/tonemap.test.cpp` — только пара на CPU. Совпадение GLSL с C++ не
  сверяет ничто.
- **Если разошлись:** небо и туман не того цвета, что задан; отладочные линии
  и тепловые карты тусклее или ярче; свечение от того, что не светится.
- Почему так: `docs/rendering.md#кадр-в-hdr`.

## Наборы и вершинный вход

| Конвейер | Набор | C++ | GLSL |
|---|---|---|---|
| мировой | 0 | `uniform_buffer_object` | `UniformBufferObject` |
| | 1 | `combined_buffer::update_descriptor_set_`: 0 модели, 1 нормали (столбец 3 — свет инстанса), 2 квады | `ModelMatrices`, `NormalMatrices`, `Quads` |
| | 2 | `shadow_map_descriptor_sets_` | `shadowMapArray`, только при `SHADOW_ENABLED` |
| | 3 | набор источников и пятен | см. выше |
| | 4 | `palette_buffer` | `PaletteBuffer` |
| | 5 | `occupancy_clipmap::get_descriptor_set` | `OccupancyParams`, `occupancyBricks[3]`, `ModelOccupancy` |
| | 6 | `light_cache::get_sampled_set` | `lightCascades[3]` |
| | push | `world_push_constant_data` (32 байта, вершинный шаг): ветер, затем сетка | `WorldPush` |
| теневой | 0 | `shadow_uniform_buffer_object` | `ShadowUniformBufferObject` |
| | 1 | тот же набор квадов | те же три буфера |
| | push | `shadow_push_constant_data` (32 байта): ветер, затем номер каскада | `ShadowPushConstants` |
| оба | location 2 | `quad::get_attribute_descriptions`: `eR32Uint`, по инстансу | `in uint inInstanceIndex` |
| вид занятости | 0 | `occupancy_clipmap::get_descriptor_set` | `OccupancyParams`, `occupancyBricks[3]` |
| | push | `occupancy_view_push` (112 байт, фрагментный шаг) | `OccupancyViewPush` |
| травы | 0, 2, 3, 4, 5, 6 | те же наборы, что у мирового, но привязаны с раскладкой травы | `grass.vert` + общий `voxel.frag` |
| | 1 | `grass_renderer::ensure_frame_buffers_`: 0 инстансы, 1 квады | `Instances`, `Quads` |
| | push | `grass_push_constants` (48 байт, вершинный шаг) | `GrassPush` |

Номера наборов в `bindDescriptorSets` у `render_world` и `render_shadow_pass`
обязаны совпадать с `set = N` в шейдерах. Здесь расхождение ловят слои
валидации.

Трава: `grass_instance` (`place`, `light` — по `vec4`, 32 байта, `static_assert` в
`grass_renderer.cppm`) повторяет `GrassInstance` в `grass.vert`, а
`grass_push_constants` (`wind`, `eye`, `shape`) — `GrassPush`. Выходы `grass.vert`
обязаны совпадать с входами `voxel.frag` по location, как у `voxel.vert`: меняешь
вход фрагментного шейдера — правь оба вершинных. `shape.y` — половина
`grass_tuft_footprint`, `shape.z` — `grass_tuft_max_height`: шейдер центрирует модель и
считает высоту для ветра по ним. Почему так — `docs/rendering.md#трава`.

Ветер: `wind` в `WorldPush` и `ShadowPushConstants` — `xy` направление, `z` размах
листвы в вокселях, `w` время, умноженное на скорость; пишет `renderer::wind_push_`.
У травы `GrassPush.wind.w` — время без скорости, скорость в `shape.w`. Формула порыва
одна в `grass.vert` и в `leafSway` (`voxel.vert`, `shadow.vert`): правишь одну —
правь все три, иначе трава и кроны качаются врозь. `static_assert` на размеры обеих
push-структур стоят в `render_uniforms.cppm`; шейдер они не видят. Почему так —
`docs/rendering.md#ветер`.

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
