module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.asset;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

namespace {

// Отсев по направлению грани в cull.comp меряет наблюдателя мировой коробкой
// инстанса, а это верно ровно до тех пор, пока оси меша совпадают с мировыми.
// Повёрнутая модель смотрит своим +X куда угодно, и целое направление её граней
// пропадало, стоило камере зайти не с той стороны коробки. Поворот виден только
// здесь, поэтому ответ едет на устройство вместе с границами.
//
// Отражение и разворот на пол-оборота уходят вместе с поворотами: оси они
// сохраняют, но меняют местами стороны, а шейдер читает их таблицей.
auto is_axis_aligned(
    const mat4f& transform_matrix
) -> bool {
    constexpr float32 epsilon = 1e-4f;

    for (int32 row = 0; row < 3; ++row) {
        for (int32 col = 0; col < 3; ++col) {
            const float32 value = transform_matrix[row, col];
            const bool aligned  =
                row == col ? value > 0.0f : std::abs(value) <= epsilon;
            if (!aligned) {
                return false;
            }
        }
    }

    return true;
}

}  // namespace

// Результат освобождения отбрасывается везде в деструкторах: пул всё равно
// уходит следом, а поднимать отсюда ошибку некуда.
combined_buffer::~combined_buffer() {
    if (compute_descriptor_set_ && descriptor_pool_) {
        static_cast<void>(
            context_->get_device().freeDescriptorSets(descriptor_pool_, compute_descriptor_set_)
        );
    }
    if (descriptor_set_ && descriptor_pool_) {
        static_cast<void>(
            context_->get_device().freeDescriptorSets(descriptor_pool_, descriptor_set_)
        );
    }
}

combined_buffer::combined_buffer(
    vulkan_context& context,
    const buffer_chunk_size& chunk_size,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout,
    vk::DescriptorSetLayout compute_descriptor_set_layout,
    staging_buffer& staging,
    deletion_queue& deletion
)
    : context_(&context)
    , staging_(&staging)
    , deletion_(&deletion)
    , chunk_size_(chunk_size)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout)
    , compute_descriptor_set_layout_(compute_descriptor_set_layout) {
    // При фиксированном числе слотов крупнейшие классы сразу держат мегабайты ради
    // горстки мешей — у одного класса замерено 4,6 МБ на пять штук. Поэтому нижняя
    // граница задана бюджетом в байтах, а дальше работает рост.
    constexpr uint32 initial_bytes = 256 * 1024;
    const auto slot_bytes = chunk_size_.quad_count * static_cast<uint32>(sizeof(quad));
    mesh_capacity_ = std::clamp(initial_bytes / std::max(slot_bytes, 1u), 4u, default_mesh_capacity_);

    quad_buffer_ = std::make_unique<device_storage_buffer>(
        *context_, mesh_capacity_ * chunk_size_.quad_count * sizeof(quad)
    );
    instance_index_buffer_ = std::make_unique<device_storage_buffer>(
        *context_,
        instance_capacity_ * sizeof(uint32),
        vk::BufferUsageFlagBits::eVertexBuffer
    );
    model_matrix_buffer_ = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * sizeof(mat4f)
    );
    normal_matrix_buffer_ = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * sizeof(mat4f)
    );
    indirect_draw_buffer_ = std::make_unique<device_storage_buffer>(
        *context_,
        instance_capacity_ * faces_per_mesh * sizeof(draw_command),
        vk::BufferUsageFlagBits::eIndirectBuffer
    );
    aabb_buffer_ = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * 2 * sizeof(vec4f)
    );
    culled_indirect_buffer_ = std::make_unique<device_storage_buffer>(
        *context_,
        instance_capacity_ * faces_per_mesh * cull_pass_count * sizeof(draw_command),
        vk::BufferUsageFlagBits::eIndirectBuffer
    );
    count_buffer_ = std::make_unique<device_storage_buffer>(
        *context_,
        cull_pass_count * sizeof(uint32),
        vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst
    );
    visibility_buffer_ = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * sizeof(uint32)
    );

    descriptor_set_ = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts        = &descriptor_set_layout_,
        }),
        "allocate combined buffer descriptor set"
    ).front();

    update_descriptor_set_();

    if (compute_descriptor_set_layout_) {
        compute_descriptor_set_ = vk_must(
            context_->get_device().allocateDescriptorSets({
                .descriptorPool     = descriptor_pool_,
                .descriptorSetCount = 1,
                .pSetLayouts        = &compute_descriptor_set_layout_,
            }),
            "allocate compute descriptor set"
        ).front();

        update_compute_descriptor_set_();
    }
}

auto combined_buffer::allocate(
    entity e, vw::asset::model_identity model_id, const mesh& mesh_data,
    const mat4f& transform_matrix, const vw::spatial::aabb& bounds
) -> void {

    if (!mesh_allocations_.contains(model_id.index)) {
        allocate_mesh(model_id, mesh_data);
    } else {
        write_mesh(model_id, mesh_data);
    }

    auto& mesh_alloc = mesh_allocations_[model_id.index];

    const auto instance_index = static_cast<uint32>(entity_allocations_.size());
    if (instance_index >= instance_capacity_) {
        expand_instance_buffers_();
    }

    const auto instance_staged = staging_->stage_struct(instance_index);
    staging_->copy_to(
        instance_index_buffer_->get_buffer(),
        instance_index * sizeof(uint32),
        instance_staged,
        sizeof(uint32)
    );

    write_draw_command_(instance_index, mesh_alloc);

    const auto model_staged = staging_->stage_struct(transform_matrix);
    staging_->copy_to(
        model_matrix_buffer_->get_buffer(),
        instance_index * sizeof(mat4f),
        model_staged,
        sizeof(mat4f)
    );

    const auto normal_matrix =
        math::transpose_matrix(math::inverse_matrix(transform_matrix).value_or(transform_matrix));
    const auto normal_staged = staging_->stage_struct(normal_matrix);
    staging_->copy_to(
        normal_matrix_buffer_->get_buffer(),
        instance_index * sizeof(mat4f),
        normal_staged,
        sizeof(mat4f)
    );

    write_bounds_(instance_index, transform_matrix, bounds);

    const entity_allocation ent_alloc{
        .instance_index = instance_index,
        .model_index    = model_id.index,
    };
    entity_allocations_[e] = ent_alloc;

    instance_indexes_[instance_index] = e;

    mesh_alloc.ref_count++;
}

auto combined_buffer::write_bounds_(
    uint32 instance_index, const mat4f& transform_matrix, const vw::spatial::aabb& bounds
) -> void {
    // Четвёртая компонента минимума говорит шейдеру отсева, стоит ли модель по
    // мировым осям; у максимума она свободна.
    const std::array<vec4f, 2> aabb_data{
        vec4f{
            bounds.min.x, bounds.min.y, bounds.min.z,
            is_axis_aligned(transform_matrix) ? 1.0f : 0.0f
        },
        vec4f{bounds.max.x, bounds.max.y, bounds.max.z, 0.0f},
    };
    const auto aabb_staged = staging_->stage_struct(aabb_data);
    staging_->copy_to(
        aabb_buffer_->get_buffer(),
        instance_index * 2 * sizeof(vec4f),
        aabb_staged,
        2 * sizeof(vec4f)
    );
}

// Команда рисует меш, а не класс размера, в который он попал. Всё за quad_count —
// остатки того, кто держал слот раньше, и не читается никогда.
auto combined_buffer::write_draw_command_(
    uint32 instance_index, const mesh_allocation& mesh_alloc
) -> void {
    // По команде на направление грани. Каждый меш индексирует один и тот же общий
    // шаблон с начала; gl_VertexIndex наводит на серию квадов этого направления
    // vertex_offset.
    std::array<draw_command, faces_per_mesh> commands{};
    uint32 offset = mesh_alloc.quad_offset;

    for (uint32 face = 0; face < faces_per_mesh; ++face) {
        const auto count = mesh_alloc.face_counts[face];
        commands[face]   = draw_command{
              .index_count    = count * 6,
              .instance_count = 1,
              .first_index    = 0,
              .vertex_offset  = static_cast<int32>(offset * 4),
              .first_instance = instance_index,
        };
        offset += count;
    }

    const auto staged = staging_->stage_struct(commands);
    staging_->copy_to(
        indirect_draw_buffer_->get_buffer(),
        instance_index * faces_per_mesh * sizeof(draw_command),
        staged,
        sizeof(commands)
    );
}

auto combined_buffer::allocate_mesh(
    vw::asset::model_identity model_id, const mesh& mesh_data
) -> void {

    const auto quad_count = static_cast<uint32>(mesh_data.quads.size());
    uint32 quad_offset    = quad_used_;

    if (!free_slots_.empty()) {
        quad_offset = free_slots_.back().quad_offset;
        free_slots_.pop_back();
    } else {
        quad_used_ += chunk_size_.quad_count;
    }

    if (quad_used_ > mesh_capacity_ * chunk_size_.quad_count) {
        expand_mesh_buffers_();
    }

    const auto quads_staged = staging_->stage_vector(mesh_data.quads);
    staging_->copy_to(
        quad_buffer_->get_buffer(),
        quad_offset * sizeof(quad),
        quads_staged,
        quad_count * sizeof(quad)
    );

    mesh_allocation new_mesh_alloc{};
    new_mesh_alloc.quad_offset = quad_offset;
    new_mesh_alloc.quad_count  = quad_count;
    new_mesh_alloc.generation  = model_id.generation;
    new_mesh_alloc.ref_count   = 0;
    new_mesh_alloc.face_counts = mesh_data.face_counts;

    mesh_allocations_[model_id.index] = new_mesh_alloc;
}

auto combined_buffer::write_mesh(
    vw::asset::model_identity model_id, const mesh& mesh_data
) -> void {
    auto& mesh_alloc = mesh_allocations_[model_id.index];
    if (mesh_alloc.generation == model_id.generation) {
        return;
    }

    const auto quad_count = static_cast<uint32>(mesh_data.quads.size());

    const auto quads_staged = staging_->stage_vector(mesh_data.quads);
    staging_->copy_to(
        quad_buffer_->get_buffer(),
        mesh_alloc.quad_offset * sizeof(quad),
        quads_staged,
        quad_count * sizeof(quad)
    );

    mesh_alloc.quad_count  = quad_count;
    mesh_alloc.generation  = model_id.generation;
    mesh_alloc.face_counts = mesh_data.face_counts;

    // Перестроение меша меняет, какая часть слота жива, поэтому команду надо
    // переписать каждому инстансу, рисующему этот меш.
    for (const auto& [ent, ent_alloc] : entity_allocations_) {
        if (ent_alloc.model_index == model_id.index) {
            write_draw_command_(ent_alloc.instance_index, mesh_alloc);
        }
    }
}

auto combined_buffer::write_transform(
    entity ent, const mat4f& transform_matrix, const vw::spatial::aabb& bounds
) -> void {
    auto& [instance_index, model_index] = entity_allocations_[ent];
    const auto model_staged = staging_->stage_struct(transform_matrix);
    staging_->copy_to(
        model_matrix_buffer_->get_buffer(),
        instance_index * sizeof(mat4f),
        model_staged,
        sizeof(mat4f)
    );

    const auto normal_matrix =
        math::transpose_matrix(math::inverse_matrix(transform_matrix).value_or(transform_matrix));
    const auto normal_staged = staging_->stage_struct(normal_matrix);
    staging_->copy_to(
        normal_matrix_buffer_->get_buffer(),
        instance_index * sizeof(mat4f),
        normal_staged,
        sizeof(mat4f)
    );

    write_bounds_(instance_index, transform_matrix, bounds);
}

auto combined_buffer::write_visibility(
    std::span<const uint32> flags
) -> void {
    if (flags.empty()) {
        return;
    }

    const auto bytes = flags.size() * sizeof(uint32);
    const auto staged = staging_->stage(flags.data(), bytes);
    staging_->copy_to(visibility_buffer_->get_buffer(), 0, staged, bytes);
}

auto combined_buffer::free(
    entity ent
) -> std::optional<entity> {
    auto& ent_alloc = entity_allocations_[ent];

    auto& mesh_alloc = mesh_allocations_[ent_alloc.model_index];
    mesh_alloc.ref_count--;

    if (mesh_alloc.ref_count <= 0) {
        free_slots_.push_back({.quad_offset = mesh_alloc.quad_offset});
        mesh_allocations_.erase(ent_alloc.model_index);
    }

    std::optional<entity> swapped_entity;

    const auto last_index = static_cast<uint32>(entity_allocations_.size() - 1);
    bool need_swap =
        ent_alloc.instance_index != last_index && last_index < entity_allocations_.size();
    if (need_swap) {
        entity last_ent      = instance_indexes_[last_index];
        auto& last_ent_alloc = entity_allocations_[last_ent];

        const auto& last_mesh_alloc = mesh_allocations_[last_ent_alloc.model_index];
        write_draw_command_(ent_alloc.instance_index, last_mesh_alloc);

        last_ent_alloc.instance_index = ent_alloc.instance_index;
        instance_indexes_[ent_alloc.instance_index] = last_ent;

        swapped_entity = last_ent;
    }

    entity_allocations_.erase(ent);
    return swapped_entity;
}
auto combined_buffer::get_entity_allocation(
    entity ent
) -> const entity_allocation& {
    return entity_allocations_[ent];
}

auto combined_buffer::get_quad_buffer() const -> vk::Buffer {
    return quad_buffer_->get_buffer();
}

auto combined_buffer::expand_mesh_buffers_() -> void {
    const auto old_bytes = (mesh_capacity_ * chunk_size_.quad_count) * sizeof(quad);

    // В полтора раза, а не вдвое. Буфер остаётся там, где его оставил прошлый рост,
    // поэтому удвоение оставляет пустым от нуля до половины — замерено 47% пустоты
    // на классе, который держит большую часть мира. Платой становятся лишние копии
    // во время стриминга сцены, а они вне кадрового пути.
    mesh_capacity_ += (mesh_capacity_ + 1) / 2;

    auto new_quad_buffer = std::make_unique<device_storage_buffer>(
        *context_, mesh_capacity_ * chunk_size_.quad_count * sizeof(quad)
    );

    staging_->replace_buffer(quad_buffer_->get_buffer(), new_quad_buffer->get_buffer());

    // После replace_buffer, иначе копирование нацелилось бы само на себя
    staging_->copy_buffer(
        quad_buffer_->get_buffer(), 0, new_quad_buffer->get_buffer(), 0, old_bytes
    );

    deletion_->retire(std::exchange(quad_buffer_, std::move(new_quad_buffer)));

    // Геометрия теперь читается через дескрипторный набор, а не привязывается
    // вершинным буфером на каждую отрисовку, поэтому набору надо сообщить о новом.
    update_descriptor_set_();
}

auto combined_buffer::expand_instance_buffers_() -> void {
    const auto instance_count = entity_allocations_.size();

    instance_capacity_ *= 2;

    auto new_model_matrix_buffer = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * sizeof(mat4f)
    );
    auto new_normal_matrix_buffer = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * sizeof(mat4f)
    );
    auto new_indirect_draw_buffer = std::make_unique<device_storage_buffer>(
        *context_,
        instance_capacity_ * faces_per_mesh * sizeof(draw_command),
        vk::BufferUsageFlagBits::eIndirectBuffer
    );
    auto new_instance_index_buffer = std::make_unique<device_storage_buffer>(
        *context_,
        instance_capacity_ * sizeof(uint32),
        vk::BufferUsageFlagBits::eVertexBuffer
    );
    auto new_aabb_buffer = std::make_unique<device_storage_buffer>(
        *context_, instance_capacity_ * 2 * sizeof(vec4f)
    );

    staging_->replace_buffer(
        model_matrix_buffer_->get_buffer(), new_model_matrix_buffer->get_buffer()
    );
    staging_->replace_buffer(
        normal_matrix_buffer_->get_buffer(), new_normal_matrix_buffer->get_buffer()
    );
    staging_->replace_buffer(
        indirect_draw_buffer_->get_buffer(), new_indirect_draw_buffer->get_buffer()
    );
    staging_->replace_buffer(
        instance_index_buffer_->get_buffer(), new_instance_index_buffer->get_buffer()
    );
    staging_->replace_buffer(
        aabb_buffer_->get_buffer(), new_aabb_buffer->get_buffer()
    );

    // После replace_buffer, иначе копирования нацелились бы сами на себя
    staging_->copy_buffer(
        model_matrix_buffer_->get_buffer(), 0,
        new_model_matrix_buffer->get_buffer(), 0,
        instance_count * sizeof(mat4f)
    );
    staging_->copy_buffer(
        normal_matrix_buffer_->get_buffer(), 0,
        new_normal_matrix_buffer->get_buffer(), 0,
        instance_count * sizeof(mat4f)
    );
    staging_->copy_buffer(
        indirect_draw_buffer_->get_buffer(), 0,
        new_indirect_draw_buffer->get_buffer(), 0,
        instance_count * faces_per_mesh * sizeof(draw_command)
    );
    staging_->copy_buffer(
        instance_index_buffer_->get_buffer(), 0,
        new_instance_index_buffer->get_buffer(), 0,
        instance_count * sizeof(uint32)
    );
    staging_->copy_buffer(
        aabb_buffer_->get_buffer(), 0,
        new_aabb_buffer->get_buffer(), 0,
        instance_count * 2 * sizeof(vec4f)
    );

    deletion_->retire(std::exchange(model_matrix_buffer_, std::move(new_model_matrix_buffer)));
    deletion_->retire(std::exchange(normal_matrix_buffer_, std::move(new_normal_matrix_buffer)));
    deletion_->retire(std::exchange(indirect_draw_buffer_, std::move(new_indirect_draw_buffer)));
    deletion_->retire(std::exchange(instance_index_buffer_, std::move(new_instance_index_buffer)));
    deletion_->retire(std::exchange(aabb_buffer_, std::move(new_aabb_buffer)));

    deletion_->retire(std::exchange(
        culled_indirect_buffer_,
        std::make_unique<device_storage_buffer>(
            *context_,
            instance_capacity_ * faces_per_mesh * cull_pass_count * sizeof(draw_command),
            vk::BufferUsageFlagBits::eIndirectBuffer
        )
    ));
    deletion_->retire(std::exchange(
        count_buffer_,
        std::make_unique<device_storage_buffer>(
            *context_,
            cull_pass_count * sizeof(uint32),
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst
        )
    ));

    // Переносить нечего: видимость переписывается целиком каждый кадр.
    deletion_->retire(std::exchange(
        visibility_buffer_,
        std::make_unique<device_storage_buffer>(
            *context_, instance_capacity_ * sizeof(uint32)
        )
    ));

    update_descriptor_set_();
    if (compute_descriptor_set_ != nullptr) {
        update_compute_descriptor_set_();
    }
}

auto combined_buffer::update_descriptor_set_() -> void {
    const vk::DescriptorBufferInfo model_buffer_info{
        .buffer = model_matrix_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo normal_buffer_info{
        .buffer = normal_matrix_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo quad_buffer_info{
        .buffer = quad_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const std::array descriptor_writes{
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_set_,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &model_buffer_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_set_,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &normal_buffer_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_set_,
            .dstBinding      = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &quad_buffer_info,
        },
    };

    context_->get_device().updateDescriptorSets(descriptor_writes, nullptr);
}

auto combined_buffer::update_compute_descriptor_set_() -> void {
    const vk::DescriptorBufferInfo indirect_info{
        .buffer = indirect_draw_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo aabb_info{
        .buffer = aabb_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo culled_info{
        .buffer = culled_indirect_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo count_info{
        .buffer = count_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const vk::DescriptorBufferInfo visibility_info{
        .buffer = visibility_buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    const std::array writes{
        vk::WriteDescriptorSet{
            .dstSet          = compute_descriptor_set_,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &indirect_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = compute_descriptor_set_,
            .dstBinding      = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &aabb_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = compute_descriptor_set_,
            .dstBinding      = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &culled_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = compute_descriptor_set_,
            .dstBinding      = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &count_info,
        },
        vk::WriteDescriptorSet{
            .dstSet          = compute_descriptor_set_,
            .dstBinding      = 4,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &visibility_info,
        },
    };

    context_->get_device().updateDescriptorSets(writes, nullptr);
}

auto combined_buffer::get_instance_count() const -> uint32 {
    return static_cast<uint32>(entity_allocations_.size());
}

auto combined_buffer::get_draw_command_count() const -> uint32 {
    return static_cast<uint32>(entity_allocations_.size()) * faces_per_mesh;
}

auto combined_buffer::get_instance_index_buffer() const -> vk::Buffer {
    return instance_index_buffer_->get_buffer();
}

auto combined_buffer::get_indirect_draw_buffer() const -> vk::Buffer {
    return indirect_draw_buffer_->get_buffer();
}

auto combined_buffer::get_aabb_buffer() const -> vk::Buffer {
    return aabb_buffer_->get_buffer();
}

auto combined_buffer::get_culled_indirect_buffer() const -> vk::Buffer {
    return culled_indirect_buffer_->get_buffer();
}

auto combined_buffer::get_count_buffer() const -> vk::Buffer {
    return count_buffer_->get_buffer();
}

auto combined_buffer::get_model_matrix_buffer() const -> vk::Buffer {
    return model_matrix_buffer_->get_buffer();
}

auto combined_buffer::get_normal_matrix_buffer() const -> vk::Buffer {
    return normal_matrix_buffer_->get_buffer();
}

auto combined_buffer::is_empty() const -> bool {
    return entity_allocations_.empty();
}

auto combined_buffer::get_stats() const -> const combined_buffer_stats& {
    stats_.chunk_size        = chunk_size_;
    stats_.mesh_capacity     = mesh_capacity_;
    stats_.mesh_count        = static_cast<uint32>(mesh_allocations_.size());
    stats_.instance_capacity = instance_capacity_;
    stats_.instance_count    = static_cast<uint32>(entity_allocations_.size());
    stats_.quad_load_min     = 0.f;
    stats_.quad_load_max     = 0.f;
    stats_.quad_load_avg     = 0.f;

    if (mesh_allocations_.empty()) {
        return stats_;
    }

    float32 load_avg_sum = 0.0f;

    for (const auto& mesh_alloc : mesh_allocations_ | std::views::values) {
        float32 load = 0.0f;
        if (chunk_size_.quad_count > 0) {
            load = static_cast<float32>(mesh_alloc.quad_count) /
                static_cast<float32>(chunk_size_.quad_count);
        }
        if (load < stats_.quad_load_min || stats_.quad_load_min == 0.0f) {
            stats_.quad_load_min = load;
        }
        if (load > stats_.quad_load_max) {
            stats_.quad_load_max = load;
        }
        load_avg_sum += load;
    }

    stats_.quad_load_avg = load_avg_sum / static_cast<float32>(mesh_allocations_.size());

    return stats_;
}
}  // namespace vw::gfx
