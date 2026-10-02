add_library(vw_net STATIC)

target_sources(vw_net
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS src FILES
            src/net.cppm
            src/socket/endpoint.cppm
            src/socket/socket.cppm
            src/http/http.cppm
    PRIVATE
        src/socket/endpoint.cpp
        src/socket/socket.cpp
        src/http/message.cpp
        src/http/server.cpp
)

target_link_libraries(vw_net PUBLIC vw_core)

if(WIN32)
    target_link_libraries(vw_net PRIVATE ws2_32)
endif()

target_compile_features(vw_net PUBLIC cxx_std_23)
vw_use_std_module(vw_net)
vw_set_warnings(vw_net)
