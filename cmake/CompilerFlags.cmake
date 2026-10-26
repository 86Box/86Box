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
    add_compile_options(-m64 "$<$<NOT:$<CONFIG:Optimized>>:-march=x86-64>" -msse2 -mfpmath=sse -mstackrealign)
elseif(ARCH STREQUAL "arm64")
    if(APPLE)
        add_compile_options("$<$<NOT:$<CONFIG:Optimized>>:-march=armv8.5-a+simd>")
    else()
        add_compile_options("$<$<NOT:$<CONFIG:Optimized>>:-march=armv8-a>")
    endif()
elseif(ARCH STREQUAL "loongarch64")
    # LSX is a hard requirement for the new dynarec on LoongArch64
    # (codegen_backend_loongarch64_defs.h gates on __loongarch_sx).
    # -march=la464 targets 3A5000-class cores; use la664 for 3A6000-class.
    add_compile_options("$<$<NOT:$<CONFIG:Optimized>>:-march=la464>" -mabi=lp64d -mlsx)
endif()

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(CMAKE_BUILD_TYPE STREQUAL "Optimized" OR
   (_multi_config AND "Optimized" IN_LIST CMAKE_CONFIGURATION_TYPES))
    include(NativeCPU)
    foreach(lang C CXX)
        resolve_native_cpu_flags("${lang}" _native_flags)
        list(JOIN _native_flags " " _native_flags)
        # Configuration flags also reach the local LTO link.
        string(APPEND CMAKE_${lang}_FLAGS_OPTIMIZED " ${_native_flags}")
        if(lang STREQUAL "C")
            string(APPEND CMAKE_ASM_FLAGS_OPTIMIZED " ${_native_flags}")
        endif()
        if(_native_flags_COMPILE_ONLY)
            list(JOIN _native_flags_COMPILE_ONLY " " _native_compile_flags)
            add_compile_options("$<$<AND:$<CONFIG:Optimized>,$<COMPILE_LANGUAGE:${lang}>>:SHELL:${_native_compile_flags}>")
        endif()
    endforeach()
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
    "$<$<CONFIG:Optimized>:-O3>"
    "$<$<CONFIG:Optimized>:-ffp-contract=fast>"
    "$<$<CONFIG:Optimized>:-flto=auto>"
)

add_link_options(
    "$<$<CONFIG:Optimized>:-flto=auto>"
)

add_compile_definitions(CMAKE)
add_compile_definitions("$<$<CONFIG:Debug>:DEBUG>")

if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
