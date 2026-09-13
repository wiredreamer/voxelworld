export module vw.world:terrain.column;
import :terrain.generator;

import std;

import vw.core;

export namespace vw::ecs {

// Колонку сначала генерируют, потом освещают — для чего нужны все восемь её
// соседей — и только затем размещают. Размещение до того, как лёг свет, мешило бы
// каждый чанк дважды.
enum class column_phase : uint8 { empty, terrain, lighting, complete };

// Одна вертикальная стопка чанков в процессе генерации, до того как чанки
// доходят до сетки.
class gen_column {
public:
    explicit gen_column(int32 cx, int32 cz) : coord_{cx, cz} {}

    [[nodiscard]] auto get_coord() const -> vec2i {
        return coord_;
    }

    [[nodiscard]] auto get_phase() const -> column_phase {
        return phase_;
    }

    [[nodiscard]] auto has_chunk_data(int32 y) const -> bool {
        return chunks_.contains(y);
    }

    [[nodiscard]] auto get_chunk_data(int32 y) -> chunk_data* {
        const auto it = chunks_.find(y);
        return it != chunks_.end() ? &it->second : nullptr;
    }

    [[nodiscard]] auto get_chunk_data(int32 y) const -> const chunk_data* {
        const auto it = chunks_.find(y);
        return it != chunks_.end() ? &it->second : nullptr;
    }

    auto create_chunk(int32 y, chunk_data data) -> chunk_data& {
        auto [it, inserted] = chunks_.emplace(y, std::move(data));
        return it->second;
    }

    // Упорядочено по убыванию Y, поэтому любой обход колонки — генерация,
    // границы, размещение — идёт с неба вниз. Из девяти чанков колонки
    // наблюдатель обычно видит один, поверхностный, и теперь он мешится первым.
    // Небесный свет пойдёт тем же путём, а нужная ему карта высот уже лежит в
    // column_profile::surface.
    using chunk_map = std::map<int32, chunk_data, std::greater<>>;

    [[nodiscard]] auto get_all_chunk_data() -> chunk_map& {
        return chunks_;
    }

    auto set_phase(column_phase phase) -> void {
        phase_ = phase;
    }

private:
    vec2i coord_;
    column_phase phase_ = column_phase::empty;
    chunk_map chunks_;
};

// Генерация колонок идёт на своих воркерах и потому никогда не попадает в
// кадровый перцентиль. Счётчики живут по воркеру и сливаются под тем же замком
// очереди, который воркер уже держит.
struct column_gen_worker_stats {
    uint64 columns = 0;
    uint64 chunks  = 0;
    uint64 nanos   = 0;
    std::vector<uint32> micros;
};

struct column_gen_stats {
    uint64 columns     = 0;
    uint64 chunks      = 0;
    float32 total_ms   = 0.0F;
    float32 mean_us    = 0.0F;
    float32 p50_us     = 0.0F;
    float32 p99_us     = 0.0F;
    float32 max_us     = 0.0F;
    uint32 queue_depth = 0;
    uint32 queue_peak  = 0;
};

}  // namespace vw::ecs
