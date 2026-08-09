#pragma once
#include <functional>

namespace Core {

using RngFunc = std::function<float()>;

class Mulberry32 {
public:
    Mulberry32(unsigned int seed) : s(seed) {}
    float operator()() {
        s += 0x6d2b79f5;
        unsigned int t = (unsigned int)((unsigned long long)(s ^ (s >> 15)) * (1 | s));
        t = (unsigned int)((unsigned long long)(t ^ (t >> 7)) * (61 | t) + t);
        return ((float)(t ^ (t >> 14))) / 4294967296.0f;
    }
private:
    unsigned int s;
};

inline Mulberry32 makeRng(unsigned int seed) { return Mulberry32(seed); }

template <typename T>
T pick(Mulberry32& rng, const T* arr, int count) {
    return arr[(int)(rng() * count)];
}

} // namespace Core
