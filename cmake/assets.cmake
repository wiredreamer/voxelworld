set(VW_ASSETS_SRC_DIR ${CMAKE_SOURCE_DIR}/assets)

function(vw_setup_assets TARGET)
    add_custom_target(${TARGET}_assets
            COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${TARGET}>/assets
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${VW_ASSETS_SRC_DIR} $<TARGET_FILE_DIR:${TARGET}>/assets
            VERBATIM
            COMMENT "Staging assets for ${TARGET}"
    )

    add_dependencies(${TARGET} ${TARGET}_assets)
endfunction()
