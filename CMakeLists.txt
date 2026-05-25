cmake_minimum_required(VERSION 3.20)
project(RobloxScratch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(cmake/FetchDependencies.cmake)

# ── ImGui static library (sources fetched above) ──────────────────────────────
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_BINARY_DIR}/_deps/glad-src/include
)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_link_libraries(imgui PUBLIC SDL2::SDL2 glad opengl32)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(RobloxScratch ${SOURCES})

target_include_directories(RobloxScratch PRIVATE
    src/
    ${CMAKE_BINARY_DIR}/_deps/glad-src/include
    ${CMAKE_BINARY_DIR}/_deps/glm-src
    ${CMAKE_BINARY_DIR}/_deps/stb-src
    ${CMAKE_BINARY_DIR}/_deps/bullet3-src/src
)

target_link_libraries(RobloxScratch PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    glad
    BulletDynamics
    BulletCollision
    LinearMath
    opengl32
    imgui
)

# WinSock2 for networking on Windows
if(WIN32)
    target_link_libraries(RobloxScratch PRIVATE ws2_32)
endif()

# Copy SDL2.dll next to the exe
add_custom_command(TARGET RobloxScratch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:RobloxScratch>
)

# Copy assets folder next to the exe
add_custom_command(TARGET RobloxScratch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RobloxScratch>/assets
)

# Copy Roboto font (bundled with ImGui) into assets/fonts/
add_custom_command(TARGET RobloxScratch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:RobloxScratch>/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
        $<TARGET_FILE_DIR:RobloxScratch>/assets/fonts/Roboto-Medium.ttf
)
