cmake_minimum_required(VERSION 3.20)
project(RobloxScratch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(cmake/FetchDependencies.cmake)

# ── Generate embedded font header (Roboto-Medium → C array) ──────────────────
message(STATUS "Generating embedded font header from Roboto-Medium.ttf ...")
file(READ "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf" FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_ARRAY "${FONT_HEX}")
file(WRITE "${CMAKE_SOURCE_DIR}/src/embedded/EmbeddedFont.h"
    "#pragma once\n#include <cstddef>\nstatic const unsigned char kRobotoFontData[] = {${FONT_ARRAY}};\nstatic const std::size_t kRobotoFontSize = sizeof(kRobotoFontData);\n"
)
message(STATUS "EmbeddedFont.h generated.")

# ── ImGui static library ──────────────────────────────────────────────────────
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
target_link_libraries(imgui PUBLIC SDL2::SDL2-static glad opengl32)

# ── Game client (standalone, no DLL needed) ───────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(RobloxScratch WIN32 ${SOURCES})
# Ship as "CreateToPlay.exe"
set_target_properties(RobloxScratch PROPERTIES OUTPUT_NAME "CreateToPlay")

target_include_directories(RobloxScratch PRIVATE
    src/
    ${CMAKE_BINARY_DIR}/_deps/glad-src/include
    ${CMAKE_BINARY_DIR}/_deps/glm-src
    ${CMAKE_BINARY_DIR}/_deps/stb-src
    ${CMAKE_BINARY_DIR}/_deps/bullet3-src/src
)

target_link_libraries(RobloxScratch PRIVATE
    SDL2::SDL2-static
    SDL2::SDL2main
    glad
    BulletDynamics
    BulletCollision
    LinearMath
    opengl32
    imgui
)

# Windows: static SDL2 needs these system libs + WinSock2 for networking
if(WIN32)
    target_link_libraries(RobloxScratch PRIVATE
        ws2_32 winmm imm32 version setupapi Cfgmgr32
    )
endif()

# ── Copy assets/ next to the exe after every build ───────────────────────────
add_custom_command(TARGET RobloxScratch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:RobloxScratch>/assets"
    COMMENT "Copying assets → $<TARGET_FILE_DIR:RobloxScratch>/assets"
)

# ── Install rules — Game component only (excludes Bullet/SDL cmake artifacts) ─
install(TARGETS RobloxScratch
    COMPONENT   Game
    RUNTIME     DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    COMPONENT   Game
    DESTINATION assets)

# ── CPack: produce CreateToPlay-0.1.0-win64.zip via  cpack -C Release  ────────
set(CPACK_PACKAGE_NAME        "CreateToPlay")
set(CPACK_PACKAGE_VERSION     "0.1.0")
set(CPACK_PACKAGE_FILE_NAME   "CreateToPlay-0.1.0-win64")
set(CPACK_GENERATOR           "ZIP")
set(CPACK_COMPONENTS_ALL      "Game")          # only our component, not Bullet/SDL
set(CPACK_STRIP_FILES         TRUE)            # strip debug symbols from Release exe
# Enable per-component install so non-Game installs (Bullet cmake files) are excluded
set(CPACK_ARCHIVE_COMPONENT_INSTALL   ON)
set(CPACK_COMPONENTS_GROUPING         ALL_COMPONENTS_IN_ONE)
include(CPack)

# ImGuizmo target is provided by FetchContent (ImGuizmo/CMakeLists.txt)
if(TARGET imguizmo AND TARGET imgui)
    target_link_libraries(imguizmo PUBLIC imgui)
endif()

# ── CreateToPlay Studio ───────────────────────────────────────────────────────
file(GLOB STUDIO_SOURCES "studio/*.cpp")
# Pull in shared low-level modules (no character / physics / network)
list(APPEND STUDIO_SOURCES
    src/core/Window.cpp
    src/renderer/Mesh.cpp
    src/renderer/Shader.cpp
    src/input/InputManager.cpp
)
add_executable(CreateToPlayStudio WIN32 ${STUDIO_SOURCES})
set_target_properties(CreateToPlayStudio PROPERTIES OUTPUT_NAME "CreateToPlayStudio")

target_include_directories(CreateToPlayStudio PRIVATE
    src/
    studio/
    ${imguizmo_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/glad-src/include
    ${CMAKE_BINARY_DIR}/_deps/glm-src
)
target_link_libraries(CreateToPlayStudio PRIVATE
    SDL2::SDL2-static SDL2::SDL2main
    glad opengl32 imgui imguizmo
)
if(WIN32)
    target_link_libraries(CreateToPlayStudio PRIVATE
        ws2_32 winmm imm32 version setupapi Cfgmgr32
    )
endif()

# ── Game server (standalone TCP relay, Linux/Railway) ─────────────────────────
if(UNIX)
    add_executable(GameServer server/main.cpp)
    target_include_directories(GameServer PRIVATE src/)
endif()
