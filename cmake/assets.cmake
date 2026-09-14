set(VW_ASSETS_SRC_DIR ${CMAKE_SOURCE_DIR}/assets)

function(vw_setup_assets TARGET)
    file(GLOB_RECURSE ASSET_VOX CONFIGURE_DEPENDS ${VW_ASSETS_SRC_DIR}/*.vox)
    file(GLOB_RECURSE ASSET_VOXA CONFIGURE_DEPENDS ${VW_ASSETS_SRC_DIR}/*.voxa)
    file(GLOB_RECURSE ASSET_VOXM CONFIGURE_DEPENDS ${VW_ASSETS_SRC_DIR}/*.voxm)
    set(ASSET_FILES ${ASSET_VOX} ${ASSET_VOXA} ${ASSET_VOXM})

    foreach(ASSET ${ASSET_FILES})
        file(RELATIVE_PATH REL_PATH ${VW_ASSETS_SRC_DIR} ${ASSET})
        get_filename_component(ASSET_NAME ${REL_PATH} NAME)
        get_filename_component(REL_DIR ${REL_PATH} DIRECTORY)

        add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Copying asset ${ASSET_NAME}"
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET}>/assets/${REL_DIR}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASSET} $<TARGET_FILE_DIR:${TARGET}>/assets/${REL_PATH}
                VERBATIM
        )
    endforeach()
endfunction()
