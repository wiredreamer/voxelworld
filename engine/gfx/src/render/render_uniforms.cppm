module;

// Ради offsetof: страж раскладки ниже требует его, а `import std` макросов не даёт.
#include <cstddef>

export module vw.gfx:renderer.uniforms;

import std;

import vw.core;
import :render.shadow_map;
import :renderer.settings;

export namespace vw::gfx {

struct directional_light_data {
    alignas(16) mat4f light_space_matrices[shadow_map::cascade_count];

    // По каскадам: x — где он кончается по глубине вида, y — сколько мира
    // покрывает один его тексель тени. Сдвиг обязан меряться в текселях: щедрый
    // для ближнего каскада, он окажется долей текселя для дальнего, а акне видно
    // именно на дальнем.
    //
    // По vec4 на каскад, а не vec4 разбиений и vec4 размеров текселя: каскадов
    // теперь пять, а пять скаляров в vec4 не влезают.
    alignas(16) vec4f cascades[shadow_map::cascade_count];

    // x: полуширина полутени в текселях, y: сдвиг по нормали в текселях, z:
    // сколько этого сдвига добавляется ещё раз на единицу наклона.
    alignas(16) vec4f shadow_filter;

    alignas(16) vec3f direction;
    alignas(16) vec3f color;
    alignas(4) float32 intensity;

    // Шире на шестнадцать байт, и это не бесплатно: intensity как раз кончалась
    // ровно на границе шестнадцати байт, поэтому свободного выравнивания здесь не
    // было. Держаться обязано то, что C++ и std140 округляют структуру одинаково,
    // — сразу за ней начинается ambient_sky.
    alignas(4) float32 wrap;
};

struct fog_data {
    alignas(16) vec3f color;
    alignas(4) float32 near_distance;
    alignas(4) float32 far_distance;
    alignas(4) uint32 enabled;
};

struct uniform_buffer_object {
    alignas(16) float32 view[16]{};
    alignas(16) float32 projection[16]{};
    alignas(16) vec3f view_pos;

    alignas(16) directional_light_data directional_light;

    // Полусферный рассеянный свет: небо сверху, земля снизу; w обоих не
    // используется.
    alignas(16) vec4f ambient_sky;
    alignas(16) vec4f ambient_ground;

    // x: насколько проседает полностью закрытый угол, y: кривая затенения;
    // z: насколько поднимается торчащий угол, w: его кривая.
    alignas(16) vec4f ao_params;

    // rgb: чем освещено место, куда не доходит небо.
    alignas(16) vec4f cave_ambient;

    // x: кривая видимости неба от запечатанного к открытому, для рассеянного
    // света. y: то же для солнца, которому нужна круче.
    alignas(16) vec4f sky_params;

    // rgb: цвет лампы на силу, w: кривая, по которой идёт запечённый уровень.
    // Стоит между sky_params и tonemap_params и в этой структуре, и в voxel.frag,
    // и они обязаны совпадать — std140 не предупреждает о расходящемся блоке,
    // только выдаёт неверные пиксели.
    alignas(16) vec4f lamp_params;

    // x: сколько собственного свечения вокселя доходит до кадра. Отдельный vec4, а
    // не свободная полоса lamp_params, потому что это разные вещи: там свет с
    // перекрывающим, здесь поверхность, которой все перекрывающие безразличны.
    alignas(16) vec4f glow_params;

    // x: экспозиция, применяется до кривой. y: уровень света, отображаемый ровно
    // в единицу.
    alignas(16) vec4f tonemap_params;

    alignas(4) uint32 point_lights_count{0};

    alignas(4) uint32 debug_view{0};

    alignas(16) fog_data fog;

    // После тумана, и обязано быть ровно здесь: voxel.frag объявляет тот же блок,
    // а std140 о расхождении молчит — получаются неверные пиксели, а если не на
    // своё место попадёт граница цикла, то и зависшее устройство. Однажды этот
    // блок стоял до point_lights_count, тогда как в шейдере — после тумана, и
    // фрагмент читал счётчик цикла за концом структуры.
    //
    // Сами пятна уехали в собственный storage-буфер со списком на кластер: восемь
    // штук в uniform означали, что девятое тело в толпе стоит ни на чём. Вместе с
    // ними ушли 256 байт uniform.
    //
    // Остаётся один глобальный масштаб на все — чтобы приглушить весь эффект, не
    // трогая каждое тело в мире.
    alignas(4) float32 blob_strength{1.0f};

    // Фроксельная сетка; в конце блока по причине, изложенной ниже. x: z_scale,
    // y: z_bias, z: размер тайла в пикселях, w: число срезов.
    alignas(16) vec4f cluster_params{};

    // x: тайлов по горизонтали, y: по вертикали, z: предел списка одного кластера,
    // w: 1, когда фрагмент читает этот список, и 0, когда обходит все источники.
    alignas(16) vec4<uint32> cluster_dims{};

    // x: предел списка тел одного кластера, y: сколько их в буфере вообще — по
    // этому числу идёт некластеризованный путь.
    alignas(16) vec4<uint32> blob_dims{};
};

// voxel.frag объявляет этот блок второй раз, и они обязаны совпадать поле в поле.
// Не проверяет этого ничто: у std140 нет диагностики на расхождение, а выдаёт оно
// не предупреждение, а неверные пиксели — или зависшее устройство, когда не на
// своё место попадает граница цикла.
//
// Эти смещения шейдера не видят и потому не доказывают, что стороны согласны. Они
// делают громкой одну половину расхождения: сдвинь поле здесь — и сборка встанет,
// а это и есть момент пойти сдвинуть его там.
static_assert(offsetof(uniform_buffer_object, sky_params) == 672);
static_assert(offsetof(uniform_buffer_object, lamp_params) == 688);
static_assert(offsetof(uniform_buffer_object, glow_params) == 704);
static_assert(offsetof(uniform_buffer_object, tonemap_params) == 720);
static_assert(offsetof(uniform_buffer_object, point_lights_count) == 736);
static_assert(offsetof(uniform_buffer_object, debug_view) == 740);
static_assert(offsetof(uniform_buffer_object, fog) == 752);
static_assert(offsetof(uniform_buffer_object, blob_strength) == 784);
static_assert(offsetof(uniform_buffer_object, cluster_params) == 800);
static_assert(offsetof(uniform_buffer_object, cluster_dims) == 816);
static_assert(offsetof(uniform_buffer_object, blob_dims) == 832);
static_assert(sizeof(uniform_buffer_object) == 848);

struct shadow_push_constant_data {
    alignas(4) uint32 cascade_index = 0;
};

struct shadow_uniform_buffer_object {
    alignas(16) mat4f light_space_matrices[shadow_map::cascade_count];
};

struct push_constant_data {
    alignas(16) float32 matrix[16]{};
};

}  // namespace vw::gfx
