function(vw_add_fuzzer name source)
    add_executable(${name} ${source})

    target_link_libraries(${name} PRIVATE vw_core vw_asset vw_net)
    target_compile_features(${name} PRIVATE cxx_std_23)
    vw_use_std_module(${name})

    target_compile_options(${name} PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    if(UNIX)
        target_link_options(${name} PRIVATE LINKER:--no-as-needed LINKER:-l:libstdc++.so.6)
    endif()

    vw_set_warnings(${name})
endfunction()

foreach(fuzzed_library vw_core vw_asset vw_net)
    target_compile_options(${fuzzed_library} PRIVATE -fsanitize=fuzzer-no-link)
endforeach()

vw_add_fuzzer(vox_parse_fuzzer vox_parse.fuzz.cpp)
vw_add_fuzzer(voxa_parse_fuzzer voxa_parse.fuzz.cpp)
vw_add_fuzzer(json_parse_fuzzer json_parse.fuzz.cpp)
vw_add_fuzzer(http_head_fuzzer http_head.fuzz.cpp)

add_test(NAME fuzz_vox_seed_corpus
    COMMAND vox_parse_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/assets/models
)
add_test(NAME fuzz_voxa_seed_corpus
    COMMAND voxa_parse_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/assets/animations
)
add_test(NAME fuzz_json_seed_corpus
    COMMAND json_parse_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/tests/data/json_test_suite/test_parsing
)
add_test(NAME fuzz_http_seed_corpus
    COMMAND http_head_fuzzer -runs=0 ${CMAKE_SOURCE_DIR}/tests/data/http_heads
)
