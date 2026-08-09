#pragma once
#include <set>
#include <string>

namespace Input {

void init();
void updateInput();
bool isKey(const char* key);
bool justPressed(const char* key);
bool wasClicked();
bool isMouseDown();
int getMouseScreenX();
int getMouseScreenY();
float getWheelDelta();

} // namespace Input
