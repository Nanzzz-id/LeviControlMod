cmake_minimum_required(VERSION 3.22)
project(LeviControlMod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(LeviControlMod SHARED
    src/main.cpp
    src/renderer.cpp
)

target_include_directories(LeviControlMod PRIVATE
    include/
)

target_link_libraries(LeviControlMod
    android
    log
    EGL
    GLESv3
)

target_compile_options(LeviControlMod PRIVATE
    -O2
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
)

target_link_options(LeviControlMod PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,--allow-shlib-undefined
    -Wl,--unresolved-symbols=ignore-all
)
