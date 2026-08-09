#pragma once
#include <string>

class World;
namespace Entities { class Worm; }

class NinjaRope {
public:
    void* joint = nullptr;
    float length = 0;
    bool attached = false;
    
    NinjaRope(const Entities::Worm* worm, void* world);
    ~NinjaRope();
    void fire(float angle);
    void retract(float amount);
    void extend(float amount);
    void detach();
};
