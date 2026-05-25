include(FetchContent)

# ── SDL2 ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.3
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ── GLAD ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
)
set(GLAD_PROFILE   "core" CACHE STRING "" FORCE)
set(GLAD_API       "gl=3.3" CACHE STRING "" FORCE)
set(GLAD_GENERATOR "c"    CACHE STRING "" FORCE)
FetchContent_MakeAvailable(glad)

# ── GLM (header-only) ─────────────────────────────────────────────────────────
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        0.9.9.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ── stb (header-only) ─────────────────────────────────────────────────────────
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# ── Bullet3 ───────────────────────────────────────────────────────────────────
FetchContent_Declare(bullet3
    GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_TAG        3.25
    GIT_SHALLOW    TRUE
)
set(BUILD_BULLET2_DEMOS        OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS        OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS           OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS            OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS               OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(bullet3)

# ── Dear ImGui ────────────────────────────────────────────────────────────────
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)
