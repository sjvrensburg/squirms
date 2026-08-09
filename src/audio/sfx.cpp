#include <raylib.h>
#include "sfx.h"
#include <initializer_list>

namespace Audio {

static bool initialized = false;
static Sound sounds[5];

static const char* pathFor(char key) {
    switch (key) {
        case 'f': return "assets/sfx/fire.wav";
        case 'e': return "assets/sfx/explode.wav";
        case 'b': return "assets/sfx/bounce.wav";
        case 'h': return "assets/sfx/hurt.wav";
        case 'r': return "assets/sfx/rope.wav";
        default:  return nullptr;
    }
}

static int indexFor(char key) {
    switch (key) {
        case 'f': return 0;
        case 'e': return 1;
        case 'b': return 2;
        case 'h': return 3;
        case 'r': return 4;
        default:  return -1;
    }
}

void init() {
    if (initialized) return;
    InitAudioDevice();
    for (char key : {'f', 'e', 'b', 'h', 'r'}) {
        sounds[indexFor(key)] = LoadSound(pathFor(key));
    }
    initialized = true;
}

void playSound(const char* type) {
    if (!initialized || !type || !type[0]) return;
    int idx = indexFor(type[0]);
    if (idx < 0) return;
    if (IsSoundValid(sounds[idx])) {
        PlaySound(sounds[idx]);
    }
}

} // namespace Audio
