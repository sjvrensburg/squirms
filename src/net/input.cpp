#include "input.h"
#include <algorithm>
#include <cmath>

namespace Input {
bool online = false;
bool connected = false;
bool blocked = false;
namespace {
int team = 0, turn = -1;
unsigned held = 0, pending = 0, presses = 0, buttons = 0, pendingClicks = 0, clicks = 0;
Vector2 mouse{};
float pendingWheel = 0, wheel = 0;
double receivedAt = -1;
// This wire format is shared with web/online.js. WASD aliases the arrows.
unsigned bit(int key) {
    switch (key) {
        case KEY_LEFT: case KEY_A: return 1u << 0;
        case KEY_RIGHT: case KEY_D: return 1u << 1;
        case KEY_UP: case KEY_W: return 1u << 2;
        case KEY_DOWN: case KEY_S: return 1u << 3;
        case KEY_SPACE: return 1u << 4;
        case KEY_ENTER: return 1u << 5;
        case KEY_BACKSPACE: return 1u << 6;
        case KEY_TAB: return 1u << 7;
        case KEY_H: return 1u << 8;
        case KEY_ONE: return 1u << 9;
        case KEY_TWO: return 1u << 10;
        case KEY_THREE: return 1u << 11;
        case KEY_FOUR: return 1u << 12;
        case KEY_FIVE: return 1u << 13;
        default: return 0;
    }
}
bool remote() { return online && team == 1; }
}
void reset() {
    held = pending = presses = buttons = pendingClicks = clicks = 0;
    pendingWheel = wheel = 0;
    receivedAt = -1;
    turn = -1;
}
void setTurn(int activeTeam, int activeTurn) {
    if (turn != activeTurn || team != activeTeam || !connected) {
        reset();
        team = activeTeam;
        turn = activeTurn;
    }
}
void beginFrame(int activeTeam, int activeTurn) {
    setTurn(activeTeam, activeTurn);
    if (GetTime() - receivedAt > 0.6) held = buttons = 0;
    presses = pending;
    clicks = pendingClicks;
    wheel = pendingWheel;
    pending = pendingClicks = 0;
    pendingWheel = 0;
}
void receive(int stamp, unsigned keys, unsigned edges, unsigned mouseButtons,
             unsigned mouseClicks, float x, float y, float scroll) {
    if (!online || !connected || team != 1 || stamp != turn ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scroll)) return;
    held = keys & 0x3fffu;
    pending |= edges & 0x3fffu;
    buttons = mouseButtons & 7u;
    pendingClicks |= mouseClicks & 7u;
    mouse = {std::clamp(x, 0.0f, 1.0f) * GetScreenWidth(),
             std::clamp(y, 0.0f, 1.0f) * GetScreenHeight()};
    pendingWheel = std::clamp(pendingWheel + scroll, -4.0f, 4.0f);
    receivedAt = GetTime();
}
bool available() { return remote() ? connected && GetTime() - receivedAt <= 0.6 : !blocked; }
bool keyDown(int key) { return remote() ? (held & bit(key)) != 0 : !blocked && IsKeyDown(key); }
bool keyPressed(int key) {
    if (online && key == KEY_F1) return false; // no terrain editing in an online match
    return remote() ? (presses & bit(key)) != 0 : !blocked && IsKeyPressed(key);
}
bool mouseDown(int button) { return remote() ? (buttons & (1u << button)) != 0 : !blocked && IsMouseButtonDown(button); }
bool mousePressed(int button) { return remote() ? (clicks & (1u << button)) != 0 : !blocked && IsMouseButtonPressed(button); }
Vector2 mousePosition() { return remote() ? mouse : GetMousePosition(); }
float mouseWheel() { return remote() ? wheel : (blocked ? 0 : GetMouseWheelMove()); }
}
