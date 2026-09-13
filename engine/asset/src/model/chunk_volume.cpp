module vw.asset;

import std;
import vw.core;

namespace vw::asset {

auto chunk_volume::set_boundary_slice(int32 face_direction, const model& neighbor) -> void {
    constexpr int32 side = face_occupancy::side;

    if (neighbor.width() != side || neighbor.height() != side || neighbor.depth() != side) {
        return;
    }

    if (boundary_ == nullptr) {
        boundary_ = std::make_unique<model_boundary>();
    }

    auto& face = boundary_->faces[face_direction];

    // Большинство швов глубокого мира — порода против породы, а у объёма,
    // однородного насквозь, все шесть сторон дают одну и ту же плоскость. Дважды
    // читать ради этого таблицу страниц дороже, чем просто это сказать.
    switch (neighbor.scan_fill()) {
        case model_fill::solid:
            face.rows.fill(~uint64{0});
            break;
        case model_fill::air:
            face.clear();
            break;
        case model_fill::mixed:
            // Сосед лежит отсюда по `face_direction`, поэтому обращённая сюда его
            // сторона — противоположная. Отказать extract_face может только на
            // несовпадении размеров, а его проверили на входе.
            static_cast<void>(neighbor.extract_face(face_direction ^ 1, face));
            break;
    }

    boundary_->valid |= static_cast<uint8>(1U << face_direction);
}

auto chunk_volume::is_boundary_solid(int32 face_direction, int32 x, int32 y, int32 z) const
    -> bool {
    const auto& face = boundary_->faces[face_direction];
    switch (face_direction / 2) {
        case 0:
            return face.test(y, z);
        case 1:
            return face.test(x, z);
        default:
            return face.test(x, y);
    }
}

auto chunk_volume::boundaries_are_solid() const -> bool {
    if (boundary_ == nullptr || boundary_->valid != 0x3F) {
        return false;
    }

    // Плоскости фиксированы, 64x64. У всего остального нет соседей, к которым его
    // прижимают, а лишние биты читались бы воздухом.
    const auto& mdl = *voxels_;
    if (mdl.width() != face_occupancy::side || mdl.height() != face_occupancy::side ||
        mdl.depth() != face_occupancy::side) {
        return false;
    }

    return std::ranges::all_of(boundary_->faces, [](const face_occupancy& face) -> bool {
        return std::ranges::all_of(face.rows, [](uint64 row) -> bool {
            return row == ~uint64{0};
        });
    });
}

// Оба поднимают поколение модели, и это не бухгалтерия: меш есть функция света
// ровно в той же мере, что и вокселей, потому что уровни запечены в углы квадов.
// mesh_pool ключуется по model_identity и отбрасывает запрос на уже имеющуюся,
// поэтому свет, пришедший без новой идентичности, — это свет, который никогда не
// доходит до экрана: чанк остаётся с мешем, выданным мгновением раньше и
// построенным под тот свет, который этот вызов как раз заменяет.
//
// Симптом — отставание на одну правку. Ставишь лампу — не загорается ничего;
// ставишь рядом что угодно — и первая лампа зажигается, потому что та правка
// увеличила поколение по своим причинам, и меш перестроился со светом, всё это
// время лежавшим на чанке.
auto chunk_volume::set_sky_light(light_field light) -> void {
    sky_ = std::make_unique<light_field>(std::move(light));
    voxels_->invalidate();
}

auto chunk_volume::set_block_light(light_field light) -> void {
    block_ = std::make_unique<light_field>(std::move(light));
    voxels_->invalidate();
}

}  // namespace vw::asset
