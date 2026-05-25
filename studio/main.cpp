#include <SDL.h>
#include "StudioApp.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    StudioApp app;
    if (!app.Init()) return 1;
    app.Run();
    app.Shutdown();
    return 0;
}
