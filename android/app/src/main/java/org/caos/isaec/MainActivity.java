package org.caos.isaec;

import org.libsdl.app.SDLActivity;

/**
 * Loads SDL2 and the engine's main library, then hands control to SDL_main in
 * src/platform/android_main.cpp. No game data is bundled: the slice runs on
 * procedural geometry only.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "main"};
    }
}
