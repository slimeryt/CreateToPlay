#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "core/Engine.h"

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    Engine engine;
    engine.Init();
    engine.Run();
    engine.Shutdown();
    return 0;
}
