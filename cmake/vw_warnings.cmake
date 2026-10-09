include(CheckCXXCompilerFlag)

option(VW_WARNINGS_AS_ERRORS "Turn warnings into errors" OFF)

check_cxx_compiler_flag(
    -Wimport-implementation-partition-unit-in-interface-unit
    VW_HAS_PARTITION_IMPORT_WARNING
)

function(vw_set_warnings target)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(flags /W4 /permissive- /wd4324)
        if(VW_WARNINGS_AS_ERRORS)
            list(APPEND flags /WX)
        endif()
    else()
        set(flags
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
            -Wformat=2
            -Wimplicit-fallthrough

            -Wno-missing-designated-field-initializers
        )
        if(VW_HAS_PARTITION_IMPORT_WARNING)
            list(APPEND flags -Wno-import-implementation-partition-unit-in-interface-unit)
        endif()
        if(VW_WARNINGS_AS_ERRORS)
            list(APPEND flags -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${flags})
endfunction()
