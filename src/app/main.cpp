#include <SDL3/SDL_main.h>

#include "app/app.hpp"

int main(int, char**) {
    trace::App app;
    const bool ok = app.init();
    if (ok)
        app.run();
    app.shutdown();
    return ok ? 0 : 1;
}
