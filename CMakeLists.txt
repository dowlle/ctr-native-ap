cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)

set(CMAKE_C_STANDARD 99)
set(CTR_NATIVE_VERSION "0.1.0-beta.5")

# Archipelago integration: OFF = clean vanilla build; ON = AP mod build.
# Build clean:  cmake -S . -B build
# Build mod:    cmake -S . -B build-ap -DCTR_AP=ON   (outputs ctr_native_ap.exe)
option(CTR_AP "Build with Archipelago integration" OFF)

if(NOT CMAKE_C_COMPILER_TARGET MATCHES "i.86" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "i.86")
    set(CMAKE_C_FLAGS "-m32 ${CMAKE_C_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "-m32 ${CMAKE_EXE_LINKER_FLAGS}")
endif()

set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc ${CMAKE_EXE_LINKER_FLAGS}")

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

set(GAME_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse -Wno-error -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -g")

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
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
target_include_directories(ctr_native PRIVATE ${GAME_INCLUDES})
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
    # RTTI / -mbig-obj live. ws:// only -> header-only deps + ws2_32, no TLS.
    enable_language(CXX)
    set(AP_VENDOR ${CMAKE_SOURCE_DIR}/ap/vendor)
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
    target_compile_definitions(ap_net PRIVATE
        CTR_AP
        WSWRAP_NO_SSL WSWRAP_NO_COMPRESSION AP_NO_SCHEMA
        ASIO_STANDALONE _WEBSOCKETPP_CPP11_THREAD_=1 _WEBSOCKETPP_CPP11_STL_=1
        _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
    target_compile_options(ap_net PRIVATE -Wa,-mbig-obj -O1 -fexceptions -frtti)

    # A C target linking a C++ archive: CMake promotes the link to C++ (g++),
    # pulling in libstdc++. Keep it static to match the -static exe.
    target_link_libraries(ctr_native ap_net ws2_32)
    target_link_options(ctr_native PRIVATE -static-libstdc++)
endif()
target_compile_options(ctr_native PRIVATE -O2)
target_link_libraries(ctr_native SDL3::SDL3)
if(WIN32)
    target_link_options(ctr_native PRIVATE -static -static-libgcc -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase -Wl,--disable-reloc-section)
else()
    target_link_options(ctr_native PRIVATE -static-libgcc -Wl,--image-base,0x400000 -no-pie -Wl,--allow-multiple-definition)
endif()
