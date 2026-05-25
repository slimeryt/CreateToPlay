#include <SDL.h>
#include "core/Engine.h"

// SDL renames main → SDL_main; SDL2main.lib provides WinMain that calls it.
// This keeps the exe windowless (no console) on Windows.
int main(int argc, char* argv[]) {
    Engine engine;
    if (!engine.Init()) return 1;
    engine.Run();
    engine.Shutdown();
    return 0;
}
