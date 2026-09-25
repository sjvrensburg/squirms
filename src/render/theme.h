#pragma once
#include <raylib.h>

// A landscape theme, Worms 2 style: the soil/crust palette baked into the
// terrain texture plus the sky, backdrop hills, sea and weather it sits in.
enum class Weather { Leaves, Dust, Snow, Embers };

struct Theme {
    const char* name;
    Color skyTop, skyBottom;
    Color hillFar, hillNear;   // parallax backdrop silhouettes
    Color soilA, soilB;        // soil blotch colours
    Color rockA, rockB;        // rock pocket colours
    Color crustA, crustB;      // grass/snow/crust on up-facing surfaces (dark, light)
    Color edge;                // crater / surface outline
    Color waterTop, waterDeep;
    Weather weather;
    Color weatherColor;
    bool stars;                // twinkling stars in the sky (night themes)
};

constexpr int THEME_COUNT = 4;
const Theme& getTheme(int index);
