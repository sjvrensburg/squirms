#include "net/input.h"
#include <cassert>
#include <cmath>

// Minimal raylib stubs exercise the actual transport boundary without a GPU.
static double now = 10;
static bool localHeld = false;
double GetTime() { return now; }
int GetScreenWidth() { return 1280; }
int GetScreenHeight() { return 720; }
bool IsKeyDown(int) { return localHeld; }
bool IsKeyPressed(int) { return localHeld; }
bool IsMouseButtonDown(int) { return localHeld; }
bool IsMouseButtonPressed(int) { return localHeld; }
Vector2 GetMousePosition() { return {100, 200}; }
float GetMouseWheelMove() { return 1; }

int main() {
    Input::online = Input::connected = true;
    Input::beginFrame(1, 4);
    Input::receive(3, 16, 16, 0, 0, .5f, .5f, 0); // stale turn
    Input::beginFrame(1, 4);
    assert(!Input::keyPressed(KEY_SPACE));
    Input::receive(4, 16, 16, 1, 1, .25f, .75f, 1);
    Input::beginFrame(1, 4);
    assert(Input::keyPressed(KEY_SPACE) && Input::keyDown(KEY_SPACE));
    assert(Input::mousePressed(0) && Input::mouseDown(0));
    assert(Input::mousePosition().x == 320 && Input::mousePosition().y == 540);
    assert(Input::mouseWheel() == 1);
    Input::beginFrame(1, 4);
    assert(!Input::keyPressed(KEY_SPACE) && Input::keyDown(KEY_SPACE));
    assert(!Input::mousePressed(0) && Input::mouseDown(0));
    assert(Input::mouseWheel() == 0);
    now += 1;
    Input::beginFrame(1, 4);
    assert(!Input::keyDown(KEY_SPACE) && !Input::mouseDown(0));
    assert(!Input::available());
    Input::receive(4, 16, 16, 0, 0, NAN, 0, 0);
    Input::beginFrame(1, 4);
    assert(!Input::keyPressed(KEY_SPACE));
    Input::receive(4, 16, 16, 0, 0, 0, 0, 0);
    Input::setTurn(0, 5); // turn changes between fixed ticks in one frame
    assert(!Input::keyDown(KEY_SPACE));
    Input::setTurn(1, 6);
    assert(!Input::keyDown(KEY_SPACE));
    Input::receive(6, ~0u, ~0u, 0, 0, 0, 0, 0);
    Input::beginFrame(1, 6);
    assert(!Input::keyPressed(KEY_F1) && !Input::keyPressed(KEY_ESCAPE));
    Input::connected = false;
    Input::beginFrame(1, 6);
    assert(!Input::keyDown(KEY_SPACE));
    Input::online = false; localHeld = true;
    Input::beginFrame(0, 7);
    assert(Input::keyDown(KEY_SPACE));
    Input::blocked = true;
    assert(!Input::keyDown(KEY_SPACE) && !Input::mousePressed(0));
}
