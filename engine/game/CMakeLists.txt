add_library(vw_game STATIC)

target_sources(vw_game
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/game.cppm
            src/input/input.cppm
            src/input/mapper.cppm
            src/input/bindings_file.cppm
            src/input/input_system.cppm
            src/player/player_control.cppm
            src/combat/projectile.cppm
            src/spawn/surface_placement.cppm
    PRIVATE
        src/game.cpp
        src/input/mapper.cpp
        src/input/bindings_file.cpp
        src/input/input_system.cpp
        src/player/player_control.cpp
        src/combat/projectile.cpp
        src/spawn/surface_placement.cpp
)

target_link_libraries(vw_game PUBLIC vw_core vw_asset vw_ecs vw_world)
target_compile_features(vw_game PUBLIC cxx_std_23)
vw_use_std_module(vw_game)
vw_set_warnings(vw_game)
