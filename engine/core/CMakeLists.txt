set(VW_LOG_MIN_LEVEL "trace" CACHE STRING "Log levels below this are compiled out")
set_property(CACHE VW_LOG_MIN_LEVEL PROPERTY STRINGS
    trace debug info warn error critical off)

add_library(vw_core STATIC)

target_sources(vw_core
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/core.cppm
            src/types/types.cppm
            src/types/color.cppm
            src/utils/timing.cppm
            src/math/vector.cppm
            src/math/matrix.cppm
            src/math/transform.cppm
            src/math/math.cppm
            src/spatial/spatial.cppm
            src/voxels/voxels.cppm
            src/voxels/catalog.cppm
            src/log/log.cppm
    PRIVATE
        src/math/math.cpp
        src/math/transform.cpp
        src/spatial/spatial.cpp
        src/voxels/voxels.cpp
        src/voxels/catalog.cpp
        src/log/log.cpp
)

target_compile_definitions(vw_core PRIVATE VW_LOG_MIN_LEVEL=${VW_LOG_MIN_LEVEL})
target_compile_features(vw_core PUBLIC cxx_std_23)
vw_use_std_module(vw_core)
vw_set_warnings(vw_core)
