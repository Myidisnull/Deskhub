# One warning policy for every first-party target. DESKHUB_WERROR is ON in the
# presets used by `make test` and by CI, so a warning fails the build in both
# places; a plain `cmake -B ...` without a preset leaves it OFF so an ad-hoc
# local build is not blocked by an unrelated toolchain warning.

option(DESKHUB_WERROR "Treat compiler warnings as errors" OFF)

function(deskhub_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(DESKHUB_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(DESKHUB_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
