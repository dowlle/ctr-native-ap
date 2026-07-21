cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CTR_NATIVE_VERSION "0.1.0-beta.6.1")

# Archipelago integration: OFF = clean vanilla build; ON = AP mod build.
# Build clean:  cmake -S . -B build
# Build mod:    cmake -S . -B build-ap -DCTR_AP=ON   (outputs ctr_native_ap.exe)
option(CTR_AP "Build with Archipelago integration" OFF)

if(NOT CMAKE_C_COMPILER_TARGET MATCHES "i.86" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "i.86")
    set(CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
    # The AP mod enables CXX (apclientpp/asio); it must be -m32 too or its
    # objects are 64-bit and fail to link against the 32-bit engine.
    set(CMAKE_CXX_FLAGS "-m32 ${CMAKE_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "-m32 ${CMAKE_EXE_LINKER_FLAGS}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    string(TIMESTAMP CTR_NATIVE_BUILD_TIMESTAMP "%Y%m%d%H%M%S" UTC)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty-${CTR_NATIVE_BUILD_TIMESTAMP}")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse -Wno-error -g")

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    BUILD=926
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)

# Archipelago mod build: enable the AP code path and emit a distinct binary
# so the clean and mod exes never clobber each other.
if(CTR_AP)
    target_compile_definitions(ctr_native PRIVATE CTR_AP)
    set_target_properties(ctr_native PROPERTIES OUTPUT_NAME "ctr_native_ap")

    # Archipelago networking (apclientpp). Compiled as an isolated C++17 static
    # library so the unity C build stays C; the only scope where exceptions /
    # RTTI / -mbig-obj live. wss:// (TLS) + permessage-deflate compression are
    # enabled: the transport is chosen from the URI scheme at runtime, so plain
    # ws:// (e.g. ws://127.0.0.1) keeps working with no behaviour change.
    enable_language(CXX)
    set(AP_VENDOR ${CMAKE_SOURCE_DIR}/ap/vendor)

    # TLS + compression backends. Static archives are preferred to match the
    # -static-libstdc++ / -static-libgcc posture of the shipped exe.
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(ZLIB_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)

    add_library(ap_net STATIC ap/ap_net.cpp ap/ap_seedcfg.cpp)
    set_target_properties(ap_net PROPERTIES
        LINKER_LANGUAGE CXX CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_include_directories(ap_net PRIVATE
        ${AP_VENDOR}/apclientpp
        ${AP_VENDOR}/wswrap/include
        ${AP_VENDOR}/websocketpp
        ${AP_VENDOR}/asio/include
        ${AP_VENDOR}/json/include
        ${CMAKE_SOURCE_DIR}/ap)
    target_link_libraries(ap_net PUBLIC OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)
    target_compile_definitions(ap_net PRIVATE
        CTR_AP
        AP_NO_SCHEMA
        ASIO_STANDALONE _WEBSOCKETPP_CPP11_THREAD_=1 _WEBSOCKETPP_CPP11_STL_=1)
    # wss:// TLS + permessage-deflate stay enabled on every target (release
    # requirement); the Windows networking headers only apply to the mingw target.
    if(WIN32)
        target_compile_definitions(ap_net PRIVATE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
    endif()
    target_compile_options(ap_net PRIVATE -O1 -fexceptions -frtti)
    # -Wa,-mbig-obj is a COFF-only assembler flag (mingw); ELF has no such limit.
    if(WIN32)
        target_compile_options(ap_net PRIVATE -Wa,-mbig-obj)
    endif()

    # A C target linking a C++ archive: CMake promotes the link to C++ (g++),
    # pulling in libstdc++. Keep it static to match the -static exe.
    # Winsock (ws2_32) + crypt32 (the Windows system-CA lookup wswrap does on
    # wss:// connections) are Windows-only; standalone-asio needs pthread on Linux
    # (Linux CA validation comes from the static OpenSSL linked into ap_net).
    target_link_libraries(ctr_native ap_net)
    if(WIN32)
        target_link_libraries(ctr_native ws2_32 crypt32)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(ctr_native Threads::Threads)
    endif()
    target_link_options(ctr_native PRIVATE -static-libstdc++)
endif()
target_compile_options(ctr_native PRIVATE -O2)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ctr_native PRIVATE -Wenum-conversion -Wenum-compare -Wswitch-enum -Wformat -Wparentheses)
endif()
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ctr_native PRIVATE -Wall -Wstrict-aliasing=2)
    ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
endif()
target_link_libraries(ctr_native SDL3::SDL3)
target_link_options(ctr_native PRIVATE -static-libgcc)
if(WIN32)
    target_link_options(ctr_native PRIVATE -static)
endif()

# Release hygiene (Linux): split debug info into a .debug sidecar so the
# binary that goes through the gate is byte-identical to what players download.
if(UNIX)
    option(CTR_SPLIT_DEBUG "Strip the binary, keeping symbols in a .debug sidecar" ON)
    set(CTR_STRIP_FLAG "--strip-all" CACHE STRING
        "objcopy strip flavour: --strip-all or --strip-debug (see issue #92 acceptance test)")
    if(CTR_SPLIT_DEBUG)
        add_custom_command(TARGET ctr_native POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} --only-keep-debug "$<TARGET_FILE:ctr_native>" "$<TARGET_FILE:ctr_native>.debug"
            COMMAND ${CMAKE_OBJCOPY} ${CTR_STRIP_FLAG} "$<TARGET_FILE:ctr_native>"
            COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink="$<TARGET_FILE:ctr_native>.debug" "$<TARGET_FILE:ctr_native>"
            COMMENT "Split debug info -> $<TARGET_FILE_NAME:ctr_native>.debug")
    endif()
endif()
