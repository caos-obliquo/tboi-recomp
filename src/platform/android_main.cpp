// Android entry point.
//
// SDLActivity loads libSDL2.so, then libmain.so, and calls the SDL_main symbol
// there. The desktop binary passes argv positionally, so the adapter presents
// the same shape: the white-box slice at 1280x720 with no frame budget, which
// is the play-until-close mode. No game data is read.
//
// The slice is the only mode Android can run today (issue #66): there is no
// archive picker and no touch input yet. Loading an archive would need a file
// picker plus the resolve path for user-supplied data, which is a separate
// change and is not faked here.

#include "isaec/platform/types.hpp"

#include <SDL_main.h>

int isaec_window_main(int argc, char** argv);

int SDL_main(int, char**) {
    static char arg0[] = "isaec_window";
    static char arg1[] = "slice";
    static char arg2[] = "1280";
    static char arg3[] = "720";
    char* argv[] = {arg0, arg1, arg2, arg3, nullptr};
    return isaec_window_main(4, argv);
}
