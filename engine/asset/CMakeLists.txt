add_library(vw_asset STATIC)

target_sources(vw_asset
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/asset.cppm
            src/model/model.cppm
            src/model/model_identity.cppm
            src/model/model_occupancy.cppm
            src/model/model_links.cppm
            src/model/model_volume.cppm
            src/model/model_edit.cppm
            src/model/light_channel.cppm
            src/model/light_field.cppm
            src/model/chunk_volume.cppm
            src/anim/anim.cppm
            src/anim/animation_keyframe.cppm
            src/anim/animation_channel.cppm
            src/anim/animation_clip.cppm
            src/anim/animation_fsm.cppm
            src/serial/serial.cppm
            src/serial/version.cppm
            src/serial/text_format.cppm
            src/serial/asset_ref.cppm
            src/serial/vox_format.cppm
            src/serial/voxm_format.cppm
            src/serial/model_library.cppm
            src/serial/voxa_format.cppm
            src/serial/voxf_format.cppm
            src/serial/asset_storage.cppm
    PRIVATE
        src/model/model.cpp
        src/model/model_edit.cpp
        src/model/chunk_volume.cpp
        src/anim/anim.cpp
        src/serial/serial.cpp
        src/serial/voxm.cpp
        src/serial/voxf.cpp
        src/serial/asset_ref.cpp
        src/serial/model_library.cpp
)

target_link_libraries(vw_asset PUBLIC vw_core)
target_compile_features(vw_asset PUBLIC cxx_std_23)
vw_use_std_module(vw_asset)
vw_set_warnings(vw_asset)
