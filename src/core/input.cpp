#include <raylib.h>
#include "input.h"

namespace Input {

static std::set<std::string> keys;
static bool mouseDown = false;
static int mouseX = 0, mouseY = 0;
static bool mouseClicked = false;
static float mouseWheel = 0;

void init() {
    keys.clear();
    mouseDown = false;
    mouseX = mouseY = 0;
    mouseClicked = false;
    mouseWheel = 0;
}

bool isKey(const char* key) {
    int code = -1;
    if (strcmp(key, "w") == 0 || strcmp(key, "arrowup") == 0) code = KEY_W;
    else if (strcmp(key, "a") == 0 || strcmp(key, "arrowleft") == 0) code = KEY_A;
    else if (strcmp(key, "s") == 0 || strcmp(key, "arrowdown") == 0) code = KEY_S;
    else if (strcmp(key, "d") == 0 || strcmp(key, "arrowright") == 0) code = KEY_D;
    else if (strcmp(key, " ") == 0 || strcmp(key, "enter") == 0) code = KEY_SPACE;
    else if (strcmp(key, "backspace") == 0) code = KEY_BACKSPACE;
    else if (strcmp(key, "escape") == 0) code = KEY_ESCAPE;
    else if (strcmp(key, "1") == 0) code = KEY_ONE;
    else if (strcmp(key, "2") == 0) code = KEY_TWO;
    else if (strcmp(key, "3") == 0) code = KEY_THREE;
    else if (strcmp(key, "q") == 0) code = KEY_Q;
    
    return code >= 0 && IsKeyDown(code);
}

bool justPressed(const char* key) {
    int code = -1;
    if (strcmp(key, "w") == 0 || strcmp(key, "arrowup") == 0) code = KEY_W;
    else if (strcmp(key, "a") == 0 || strcmp(key, "arrowleft") == 0) code = KEY_A;
    else if (strcmp(key, "s") == 0 || strcmp(key, "arrowdown") == 0) code = KEY_S;
    else if (strcmp(key, "d") == 0 || strcmp(key, "arrowright") == 0) code = KEY_D;
    else if (strcmp(key, " ") == 0 || strcmp(key, "enter") == 0) code = KEY_SPACE;
    else if (strcmp(key, "backspace") == 0) code = KEY_BACKSPACE;
    else if (strcmp(key, "escape") == 0) code = KEY_ESCAPE;
    else if (strcmp(key, "1") == 0) code = KEY_ONE;
    else if (strcmp(key, "2") == 0) code = KEY_TWO;
    else if (strcmp(key, "3") == 0) code = KEY_THREE;
    else if (strcmp(key, "q") == 0) code = KEY_Q;
    
    return code >= 0 && IsKeyPressed(code);
}

bool wasClicked() {
    if (IsMouseButtonPressed(0)) {
        mouseClicked = true;
        return true;
    }
    return false;
}

bool isMouseDown() {
    return mouseDown;
}

int getMouseScreenX() {
    return mouseX;
}

int getMouseScreenY() {
    return mouseY;
}

float getWheelDelta() {
    return mouseWheel;
}

void updateInput() {
    // Update mouse state from raylib
    mouseX = GetMouseX();
    mouseY = GetMouseY();
    mouseDown = IsMouseButtonDown(0);
    mouseWheel = (float)GetMouseWheelMove();
}

} // namespace Input
