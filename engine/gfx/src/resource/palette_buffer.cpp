module vw.gfx;

import std;
import vulkan;
import vw.core;
import vw.ecs;
import vw.world;
import vw.platform;
import :vk;

namespace vw::gfx {

namespace {

// Простая степень, а не 2,4 настоящей кривой sRGB. Здесь верны две вещи, и тянут
// они в разные стороны.
//
// Шейдер умножает альбедо на свет, а свет линеен, поэтому строго говоря палитру
// надо декодировать: байт, делённый на 255 и поданный в это умножение напрямую,
// завышает каждый цвет, а солнце затем выталкивает произведение за единицу.
//
// Но палитру Apollo подбирали на глаз, на экране, как готовые пиксели. Правильное
// декодирование сильно и неравномерно её затемняет: чтобы вернуть тёмно-зелёный
// 0x25562e туда, где его нарисовали, нужно в восемь раз больше света, а почти белому
// 0xebede9 — на десятую долю больше. Разброс в семь раз не покрыть ничем, поэтому
// «правильно» и «похоже на палитру» — это две настройки, а не одна.
//
// На 1,5 это и остановилось, по экрану. Это компромисс, и выглядеть он должен
// компромиссом: тёмные сохраняют глубину, не проваливаясь в пол, а освещённые грани
// не упираются в потолок. Срез, который декодирование отчасти должно было починить,
// теперь относится к тоновой кривой, поэтому ниже по потоку от этого числа не
// зависит ничто — до тех пор, пока в сумму не войдут цветные источники: сложение их
// поверх нелинейного альбедо начинает сдвигать оттенок, и вопрос вернётся уже с
// картинкой.
constexpr float32 palette_gamma = 1.5f;

[[nodiscard]] auto decode(uint8 channel) -> float32 {
    return std::pow(static_cast<float32>(channel) / 255.0f, palette_gamma);
}

}  // namespace

palette_buffer::palette_buffer(
    vulkan_context& context,
    vk::DescriptorPool descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout,
    const block_registry& registry
)
    : context_(&context)
    , descriptor_pool_(descriptor_pool)
    , descriptor_set_layout_(descriptor_set_layout) {
    // Ровно по числу блоков в реестре, а не по всему, что влезает в слот: записи
    // разложены по слотам, слот квада приходит оттуда же, и за конец списка
    // обратиться неоткуда. Нулевая запись — заглушка для блока вне каталога, и
    // она кричаще-розовая намеренно.
    //
    // Альфа несёт то, насколько ярко блок рисует сам себя. Раньше там стояла
    // константная единица, которую никто не читал, поэтому слагаемое собственного
    // свечения не стоит ни второго буфера, ни второго обращения, ни лишнего байта:
    // вершинный шейдер и так выбирает этот vec4 и выбрасывал четвёртую составляющую.
    const std::span<const block_type> blocks = registry.all();

    std::vector<vec4f> palette_data;
    palette_data.reserve(blocks.size());
    for (const block_type& block : blocks) {
        const color clr = block.material.clr;

        palette_data.push_back(vec4f{
            decode(clr.r()), decode(clr.g()), decode(clr.b()),
            static_cast<float32>(block.material.glow) / 255.0f
        });
    }

    const std::size_t palette_bytes = palette_data.size() * sizeof(vec4f);

    buffer_ = std::make_unique<storage_buffer>(*context_, palette_bytes);
    buffer_->copy_from(palette_data.data(), palette_bytes);

    descriptor_set_ = vk_must(
        context_->get_device().allocateDescriptorSets({
            .descriptorPool     = descriptor_pool_,
            .descriptorSetCount = 1,
            .pSetLayouts        = &descriptor_set_layout_,
        }),
        "allocate palette descriptor set"
    ).front();

    const vk::DescriptorBufferInfo buffer_info{
        .buffer = buffer_->get_buffer(),
        .offset = 0,
        .range  = vk::WholeSize,
    };

    context_->get_device().updateDescriptorSets(
        vk::WriteDescriptorSet{
            .dstSet          = descriptor_set_,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &buffer_info,
        },
        nullptr
    );
}

palette_buffer::~palette_buffer() {
    if (descriptor_set_ && descriptor_pool_) {
        static_cast<void>(
            context_->get_device().freeDescriptorSets(descriptor_pool_, descriptor_set_)
        );
    }
}

auto palette_buffer::get_descriptor_set() const -> vk::DescriptorSet {
    return descriptor_set_;
}

}  // namespace vw::gfx
