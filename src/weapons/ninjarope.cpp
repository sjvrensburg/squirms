#include "ninjarope.h"
#include "../entities/worm.h"

NinjaRope::NinjaRope(const Entities::Worm* worm, void* world) {
    (void)worm; (void)world;
}

NinjaRope::~NinjaRope() {
}

void NinjaRope::fire(float angle) {
    (void)angle;
    attached = true;
}

void NinjaRope::retract(float amount) {
    length -= amount;
    if (length < 0) length = 0;
}

void NinjaRope::extend(float amount) {
    length += amount;
}

void NinjaRope::detach() {
    attached = false;
    length = 0;
}
