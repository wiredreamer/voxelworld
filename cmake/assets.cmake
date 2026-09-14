set(VW_ASSETS_SRC_DIR ${CMAKE_SOURCE_DIR}/assets)

# Stages the whole asset tree next to the executable, before the executable is
# considered built.
#
# The staging used to walk a glob of extensions and copy file by file, and that
# leaves litter: an asset that moves or gets renamed is copied to its new place
# while the old copy stays behind for good. Splitting prefabs out of models/ put
# a stale .vox next to every live one. Wiping the directory first is the only
# way a copy step ends with exactly what the source holds.
#
# The same trap as with the shaders: a POST_BUILD command runs only when the
# target relinks, so editing a model alone never delivered it and the game went
# on loading the previous one. A custom target the executable depends on runs
# whenever the executable is built.
#
# Everything under assets/ is staged, not a list of extensions: the directory is
# the shipping set by definition, and a new format (.voxf next) then needs no
# line here to arrive.
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
