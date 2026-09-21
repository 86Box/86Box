include_guard(GLOBAL)

# These flags are GCC/Clang style and are not valid for MSVC's `cl` frontend.
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang|IntelLLVM")
    message(WARNING "Compiler '${CMAKE_C_COMPILER_ID}' is not supported; skipping GCC/Clang-style default flags")
    return()
endif()

# Common warnings and behavior flags.
add_compile_options(
    -fomit-frame-pointer
    -Wall
    -fno-strict-aliasing
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-int>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=int-conversion>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=strict-prototypes>"
    "$<$<COMPILE_LANGUAGE:C>:-Werror=old-style-definition>"
)

if(ARCH STREQUAL "x86_64")
    add_compile_options(-m64 -march=x86-64 -msse2 -mfpmath=sse -mstackrealign)
elseif(ARCH STREQUAL "arm64")
    if(APPLE)
        add_compile_options(-march=armv8.5-a+simd)
    else()
        add_compile_options(-march=armv8-a)
    endif()
endif()

# Configuration-specific flags.
add_compile_options(
    "$<$<CONFIG:Release>:-g0>"
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:Debug>:-ggdb>"
    "$<$<CONFIG:Debug>:-Og>"
    "$<$<CONFIG:UltraDebug>:-O0>"
    "$<$<CONFIG:UltraDebug>:-ggdb>"
    "$<$<CONFIG:UltraDebug>:-g3>"
    "$<$<CONFIG:Optimized>:-march=native>"
    "$<$<CONFIG:Optimized>:-mtune=native>"
    "$<$<CONFIG:Optimized>:-O3>"
    "$<$<CONFIG:Optimized>:-ffp-contract=fast>"
    "$<$<CONFIG:Optimized>:-flto>"
)

add_link_options(
    "$<$<CONFIG:Optimized>:-flto>"
)

add_compile_definitions(CMAKE)
add_compile_definitions("$<$<CONFIG:Debug>:DEBUG>")

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
