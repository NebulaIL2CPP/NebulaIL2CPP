cmake_minimum_required(VERSION 3.22.1)
project(NebulaIL2CPP LANGUAGES C CXX ASM)

if(NOT ANDROID)
    message(FATAL_ERROR "NebulaIL2CPP is an Android NDK project")
endif()
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported (got ${ANDROID_ABI})")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The library is often loaded by an injector with a restricted linker
# namespace. Embed the NDK C++ runtime so a separate libc++_shared.so is not
# required at injection time.
set(ANDROID_STL c++_static CACHE STRING
    "Use the static NDK C++ runtime" FORCE)
set(CMAKE_ANDROID_STL_TYPE c++_static CACHE STRING
    "Use the static NDK C++ runtime" FORCE)

set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver ON CACHE BOOL "" FORCE)
add_subdirectory(external/Dobby EXCLUDE_FROM_ALL)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

add_library(Nebula SHARED
    src/Entry.cpp
    src/Core/Log.cpp
    src/Core/Runtime.cpp
    src/Hook/Hook.cpp
    src/Il2Cpp/Il2Cpp.cpp
    src/Mods/ModManager.cpp
    src/Mods/ExampleMod.cpp
    src/Mods/SunMod.cpp
    src/UI/Overlay.cpp
    src/UI/SampleMenu.cpp
    src/UI/VulkanOverlay.cpp
    src/Config/Config.cpp
)

target_include_directories(Nebula PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/json/include"
)
target_compile_options(Nebula PRIVATE
    -Wall -Wextra -Wpedantic
    -fvisibility=hidden
)
target_link_options(Nebula PRIVATE
    "-Wl,--exclude-libs,ALL"
    "-static-libstdc++"
)
target_link_libraries(Nebula PRIVATE
    dobby_static
    imgui
    android
    EGL
    GLESv3
    vulkan
    log
    dl
)

set_target_properties(Nebula PROPERTIES
    OUTPUT_NAME "Nebula"
)
