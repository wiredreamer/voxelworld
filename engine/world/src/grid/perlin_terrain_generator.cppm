export module vw.world:terrain.perlin;
import :terrain.generator;

import std;

import vw.core;
import vw.asset;

export namespace vw::ecs {

class perlin_terrain_generator final : public terrain_generator {
public:
    struct params {
        uint32 seed       = 42;
        int32 voxel_scale = 8;

        // Мир стоит на полу фиксированной высоты, а не фиксированной толщины под
        // рельефом. Слои породы — и подземные этажи, которые появятся позже —
        // обязаны лежать на одной высоте везде, а дно, повторяющее холмы, положило
        // бы их под наклон. Значение кратно размеру чанка, поэтому ни одна колонка
        // не кончается наполовину заполненным чанком.
        //
        // Тысяча вокселей пробовалась первой и стоила 1,2 ГБ и кадра в 46 мс: это
        // пятнадцать чанков сплошной породы на колонку, на которые никто не
        // смотрит, а способа не трогать их у движка пока нет. Половина этого даёт
        // пещерам нужный запас, не платя за остальное.
        int32 world_bottom_y = -448;

        // Земля построена слоями, а не нарисована поверх одного поля высот: у
        // породы свой рельеф, почва лежит на нём, а гора — просто там, где порода
        // вышла из почвы. Почва сползает с крутизны и редеет с высотой, поэтому
        // голым вершинам не нужен отдельный случай.
        int32 soil_depth_max      = 5;
        float32 soil_frequency    = 0.012F;
        float32 soil_slope_limit  = 1.4F;
        int32 soil_altitude_start = 55;
        int32 soil_altitude_end   = 78;
        int32 snow_line           = 82;

        // Несколько вокселей породы прямо под землёй выветрены, а ниже порода
        // меняется с абсолютной глубиной — по стене пещеры видно, насколько
        // глубоко.
        int32 rock_skin     = 4;
        int32 rock_deep_y   = -64;
        int32 rock_bottom_y = -256;

        // Пещеры рождаются из шума, а шум пропускается через поле: низкочастотный
        // слой, который говорит, где подземелье вообще полое. Внутри поля — система
        // пещер в несколько этажей, между полями порода сплошная.
        //
        // Две прежние формы обе были замерены: решётка тоннелей давала одну сеть
        // ровного калибра на весь мир, а цепочки расставленных вручную залов
        // читались как комнаты, но не могли заполнить глубину. Эта сохраняет
        // кучность второй и берёт формы из шума, как первая.
        //
        // Один выключатель, а не порог, до которого шуму не дотянуться: у входов
        // своё поле, поэтому глушение одних только пятен всё равно оставляло дыры
        // в земле.
        bool caves = true;

        // Трёхмерное: поле — это тело полой породы в каком-то месте и на какой-то
        // глубине, а не колонка на всю высоту. Двумерное замерялось первым и
        // именно из-за него каждый чанк мира нёс геометрию — 3113 отрисованных
        // чанков из 3594 против 1187 до того, — а генератор был вчетверо десятков
        // раз медленнее.
        float32 cave_field_frequency = 0.0090F;
        float32 cave_field_squash    = 0.55F;
        float32 cave_field_threshold = 0.26F;
        float32 cave_field_falloff   = 0.30F;

        // Поля чаще встречаются в породе сразу под землёй: так ведёт себя карст, и
        // так у входов появляется, куда вести. С ровным полем два участка карты из
        // четырёх имели большую систему и ни одного отверстия.
        float32 cave_field_surface_bias = 0.10F;
        int32 cave_field_surface_reach  = 150;

        // Залы: трёхмерное поле, порезанное порогом у своей нулевой поверхности и
        // сплющенное по Y, чтобы зал был шире, чем выше, и по нему можно было
        // пройти.
        float32 cave_cheese_frequency = 0.0135F;
        float32 cave_cheese_squash    = 2.4F;
        float32 cave_cheese_width     = 0.090F;

        // Ходы: там, где два независимых поля оба подходят к нулю. Это пересечение
        // — кривая, а не поверхность, отчего и получается тоннель, а не полотно.
        float32 cave_tunnel_frequency = 0.019F;
        float32 cave_tunnel_width     = 0.045F;

        // Этажи. Залы расширяются полосами на фиксированных высотах, поэтому одно
        // поле держит уровни, которые потом соединяет ход, и уровни совпадают со
        // слоями породы, а не с холмами наверху.
        int32 cave_level_spacing    = 46;
        float32 cave_level_contrast = 0.55F;

        // Дневной свет. Участок с протекающей кровлей — сам себе поле на всю
        // глубину затухания: пещерный шум режется там независимо от того, дотянулось
        // ли туда пятно, поэтому дыра существует, и всё под ней соединено с небом.
        // Вариант, где вход лишь снимает штраф с уже существующего поля, замерялся
        // дважды и оставлял целые участки карты запечатанными.
        float32 cave_entrance_frequency = 0.0210F;
        float32 cave_entrance_threshold = 0.21F;
        float32 cave_entrance_falloff   = 0.12F;
        float32 cave_entrance_field     = 0.85F;

        // Насколько глубоко вход продолжает резать. Дыра, кончающаяся вместе с
        // затуханием, — это яма; дошедшая до глубин, где живут поля, — шахта, и она
        // соединяет с небом всё, что под ней.
        int32 cave_entrance_depth  = 110;
        float32 cave_entrance_lift = 0.11F;

        // Как пещера затухает на подходе к поверхности и насколько выше пола мира
        // она останавливается.
        int32 cave_surface_margin = 7;
        int32 cave_surface_fade   = 22;
        int32 bedrock_thickness   = 3;

        // Шум берётся на сетке с таким шагом в вокселях и читается обратно
        // трилинейной интерполяцией. По вокселю это были бы четыре трёхмерные
        // выборки на каждый воксель мира; при шаге четыре — одна на шестьдесят
        // четыре, а ход шириной в два вокселя интерполяцию всё ещё переживает.
        int32 cave_sample_stride = 4;

        float32 continent_frequency = 0.003F;
        float32 terrain_frequency   = 0.02F;
        int32 octaves               = 4;
        float32 lacunarity          = 2.0F;
        float32 persistence         = 0.5F;

        int32 plains_height    = 20;
        int32 hills_height     = 35;
        int32 mountains_height = 55;

        float32 ridge_frequency = 0.015F;
        float32 ridge_weight    = 0.6F;

        float32 warp_frequency = 0.01F;
        float32 warp_strength  = 30.0F;
    };

    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool);
    perlin_terrain_generator(asset::model_identity_pool& identity_pool, asset::page_pool& pool,
                             params p);

    auto generate(terrain_context& ctx) -> void override;

    [[nodiscard]] auto surface_height_at(int32 wx, int32 wz) const -> int32;

private:
    [[nodiscard]] auto noise2d(float64 x, float64 y) const -> float64;

    [[nodiscard]] auto noise3d(float64 x, float64 y, float64 z) const -> float64;

    // Насколько поле пещер хочет видеть эту породу полой: 0 вне поля и 1 глубоко
    // внутри. Сплющено по Y, поэтому поле шире, чем глубже, и читается как область
    // карты, а не как шахта.
    [[nodiscard]] auto cave_field_at(int32 wx, int32 wy, int32 wz, int32 depth) const -> float32;

    // Положительно там, где порода становится воздухом. Залы и ходы — два слагаемых
    // одной величины, поэтому оба решает одно интерполированное число.
    [[nodiscard]] auto cave_openness_at(
        int32 wx, int32 wy, int32 wz, float32 field, int32 surface, float32 leak
    ) const -> float32;

    // Насколько протекает кровля над этой колонкой: 0 для сплошной земли и 1 для
    // входа.
    [[nodiscard]] auto cave_entrance_leak_at(int32 wx, int32 wz) const -> float32;

    [[nodiscard]] auto octave_noise(float64 x, float64 y) const -> float64;
    [[nodiscard]] auto ridged_noise(float64 x, float64 y) const -> float64;
    [[nodiscard]] auto continent_at(float64 nx, float64 nz) const -> float64;

    // Рельеф породы, без лежащей сверху почвы.
    [[nodiscard]] auto stone_height_at(int32 wx, int32 wz) const -> int32;
    [[nodiscard]] auto soil_depth_at(int32 wx, int32 wz, int32 stone, float32 slope) const -> int32;

    [[nodiscard]] auto rock_block_at(int32 wy) const -> block_id;
    [[nodiscard]] auto block_at(int32 wy, int32 stone_top, int32 surface_top) const -> block_id;

    // stone_height_at — это примерно двадцать вызовов noise2d, и одни и те же
    // (x, z) раньше пересчитывались по разу на каждый чанк колонки. Здесь они
    // берутся один раз. Точные экстремумы заодно заменяют оценку по пяти выборкам,
    // которая решала вертикальный размах колонки и могла срезать рельеф между
    // ними.
    struct column_profile {
        static constexpr int32 size   = 64;
        static constexpr int32 apron  = 1;
        static constexpr int32 stride = size + (2 * apron);
        static constexpr int32 page   = 8;
        static constexpr int32 pages  = size / page;

        // Высоты породы несут юбку в один воксель, поэтому наклон, решающий,
        // сколько осядет почвы, — это разность по уже взятым выборкам, а не ещё
        // четыре вычисления шума на колонку вокселей.
        std::array<int32, stride * stride> stone{};
        std::array<int32, size * size> surface{};

        // На след страницы (8 x 8 вокселей): самая низкая порода и самая высокая
        // поверхность над ней. Страница ниже первой — сплошная порода, страница
        // выше второй — воздух; ни ту, ни другую не нужно обходить по вокселям.
        std::array<int32, pages * pages> page_min_stone{};
        std::array<int32, pages * pages> page_max_surface{};

        int32 min_stone   = 0;
        int32 max_surface = 0;

        [[nodiscard]] static auto stone_index(int32 x, int32 z) -> int32 {
            return ((x + apron) * stride) + (z + apron);
        }
    };

    [[nodiscard]] auto sample_column_(int32 cx, int32 cz) const -> column_profile;

    auto carve_caves_(asset::model_writer& writer, terrain_context& ctx, int32 chunk_y,
                      const column_profile& profile) const -> void;

    auto generate_chunk(terrain_context& ctx, int32 chunk_y, const column_profile& profile) -> void;

    static auto fade(float64 t) -> float64;
    static auto lerp(float64 t, float64 a, float64 b) -> float64;
    static auto grad(int32 hash, float64 x, float64 y) -> float64;

    asset::model_identity_pool* identity_pool_;
    asset::page_pool* page_pool_;
    params params_;
    std::array<int32, 512> perm_;
};

}  // namespace vw::ecs
