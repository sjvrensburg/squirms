#pragma once
#include <raylib.h>

// Gameplay input only. Online matches run on the host; team 2's input is
// received over an ordered WebRTC channel and checked against the turn id.
namespace Input {
extern bool online;
extern bool connected;
extern bool blocked;
void setTurn(int team, int turn);
void beginFrame(int team, int turn);
void reset();
void receive(int turn, unsigned keys, unsigned pressed, unsigned buttons,
             unsigned clicks, float x, float y, float wheel);
bool available();
bool keyDown(int key);
bool keyPressed(int key);
bool mouseDown(int button);
bool mousePressed(int button);
Vector2 mousePosition();
float mouseWheel();
}
