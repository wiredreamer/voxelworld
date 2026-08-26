export module vw.world:light.baker;

import std;

import vw.core;
import vw.ecs;
import :model;
import :light.column;

export namespace vw::ecs {

// Одна колонка, готовая к освещению: модели чанков своей колонки и восьми соседних,
// снизу вверх от общего для всех пола, с индексом (dz + 1) * 3 + (dx + 1), так что
// середина — 4.
//
// Модели держатся через shared_ptr на всё время задачи: она идёт на воркере, и
// ничто не мешает сетке отпустить чанк в это время. Пустая запись — это
// несуществующая колонка, и заливка читает её как породу.
struct light_request {
    vec2i coord;
    int32 bottom_y = 0;
    std::array<std::vector<std::shared_ptr<asset::model>>, 9> around;
};

// По два поля на каждый чанк средней колонки, в том же порядке, в каком они были в
// запросе. Оба канала возвращаются одной задачей, потому что оба залиты за один
// проход по одному набору буферов и потому что чанк, чей свет изменился, мешится
// заново ровно один раз, сколько бы каналов ни сдвинулось.
struct light_result {
    vec2i coord;
    int32 bottom_y = 0;
    std::vector<asset::light_field> sky;
    std::vector<asset::light_field> block;
};

struct light_worker_stats {
    uint64 columns      = 0;
    uint64 rows_nanos   = 0;
    uint64 flood_nanos  = 0;
    uint64 bake_nanos   = 0;
    std::vector<uint32> micros;
};

struct light_stats {
    uint64 columns     = 0;
    float32 rows_ms    = 0.0F;
    float32 flood_ms   = 0.0F;
    float32 bake_ms    = 0.0F;
    float32 mean_us    = 0.0F;
    float32 p50_us     = 0.0F;
    float32 p99_us     = 0.0F;
    float32 max_us     = 0.0F;
    uint32 queue_depth = 0;
    uint32 queue_peak  = 0;
};

// Освещает колонки на потоках-воркерах и отдаёт готовые поля через очередь — та же
// схема, что у chunk_loader. Это отдельный этап, а не часть генерации, потому что
// колонку можно осветить только когда существуют все восемь её соседей, а поток,
// сгенерировавший её, о них ничего не знает.
class light_baker {
public:
    explicit light_baker(const block_registry& blocks, uint32 workers = 0);
    ~light_baker();

    light_baker(const light_baker&)                    = delete;
    auto operator=(const light_baker&) -> light_baker& = delete;
    light_baker(light_baker&&)                         = delete;
    auto operator=(light_baker&&) -> light_baker&      = delete;

    auto request(light_request job) -> bool;

    [[nodiscard]] auto try_pop_completed() -> std::optional<light_result>;
    [[nodiscard]] auto is_pending(vec2i coord) const -> bool;
    [[nodiscard]] auto pending_count() const -> uint32;
    [[nodiscard]] auto get_stats() const -> light_stats;

private:
    // Кэш строк целых колонок здесь строился и был убран обратно. Причина в
    // арифметике. Задача строит свою среднюю колонку целиком — 64 страничные
    // колонки — а восемь соседних лишь на глубину юбки, ещё 80, итого 144; девять
    // целых колонок дали бы 576. Кэшу, через который проходит всё, нужно три
    // попадания из четырёх просто чтобы выйти в ноль, а замерено было 40%: строки
    // ушли с 2,8 до 10,9 секунды на тысяче колонок. Кэшировать только середину,
    // которая и так строится целиком, хуже быть не может и дало 22% со строк —
    // восемь процентов задачи — за 25 МБ резидентной памяти. Для этапа, никогда не
    // касающегося кадра, память того не стоит.
    auto worker_() -> void;
    auto merge_worker_stats_(light_worker_stats& worker) -> void;

    std::vector<std::thread> threads_;
    std::queue<light_request> queue_;
    std::queue<light_result> completed_;
    mutable std::mutex mutex_;
    mutable std::mutex completed_mutex_;
    std::condition_variable cv_;
    bool running_ = true;
    std::unordered_set<vec2i> pending_;

    light_worker_stats totals_;
    uint32 queue_peak_ = 0;

    // Снимается с реестра, который дал мир. Раньше запекатель строил себе свой
    // и был вторым источником истины: расширенный каталог до него бы не доехал, и
    // светились бы не те блоки.
    asset::emission_table emission_;
};

}  // namespace vw::ecs
