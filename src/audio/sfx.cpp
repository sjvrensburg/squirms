// Procedural sound effects for Squirms.
//
// Every effect is synthesized at startup into a mono 22050 Hz float buffer,
// post-processed (DC block, click-free fades, peak normalization), converted
// to a 16-bit raylib Wave and uploaded with LoadSoundFromWave. Each effect
// then gets a small pool of Sound aliases so overlapping plays work.
#include <raylib.h>
#include "sfx.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Audio {
namespace {

// ---------------------------------------------------------------------------
// DSP toolkit
// ---------------------------------------------------------------------------
constexpr int   SR  = 22050;
constexpr float TAU = 2.0f * PI; // PI comes from raylib.h

using Buf = std::vector<float>;

Buf make(float secs) { return Buf(static_cast<size_t>(secs * SR) + 1, 0.0f); }

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// 0..1 progress of t through [t0, t1].
inline float prog(float t, float t0, float t1) { return clamp01((t - t0) / (t1 - t0)); }
// Exponential sweep from a to b as x goes 0..1 (clamped).
inline float sweep(float a, float b, float x) { return a * std::pow(b / a, clamp01(x)); }
inline float expd(float t, float tau) { return t <= 0.0f ? 1.0f : std::exp(-t / tau); }
inline float att(float t, float a) { return t >= a ? 1.0f : (t <= 0.0f ? 0.0f : t / a); }
inline float hump(float x) { return (x <= 0.0f || x >= 1.0f) ? 0.0f : std::sin(PI * x); }
inline float semis(float s) { return std::pow(2.0f, s / 12.0f); }

// Small deterministic PRNG (xorshift32).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float>(u() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                                   // [-1,1)
    float range(float a, float b) { return a + (b - a) * uni(); }
};

// Topology-preserving-transform state-variable filter (stable under fast
// cutoff modulation, which we do a lot of).
struct SVF {
    float ic1 = 0.0f, ic2 = 0.0f;
    float lpo = 0.0f, bpo = 0.0f, hpo = 0.0f;
    void run(float x, float fc, float q) {
        if (fc < 10.0f) fc = 10.0f;
        if (fc > SR * 0.45f) fc = SR * 0.45f;
        const float g = std::tan(PI * fc / SR);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lpo = v2;
        bpo = k * v1;              // unity-gain-at-centre band-pass
        hpo = x - k * v1 - v2;
    }
    float lp(float x, float fc, float q = 0.707f) { run(x, fc, q); return lpo; }
    float bp(float x, float fc, float q = 1.0f)   { run(x, fc, q); return bpo; }
    float hp(float x, float fc, float q = 0.707f) { run(x, fc, q); return hpo; }
};

// Phase-accumulating oscillator with polyBLEP-antialiased saw/square.
struct Osc {
    float ph = 0.0f;
    static float blep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    void adv(float f) {
        ph += f / SR;
        ph -= std::floor(ph);
    }
    float sine(float f) { const float v = std::sin(TAU * ph); adv(f); return v; }
    float tri(float f) { const float v = 1.0f - 4.0f * std::fabs(ph - 0.5f); adv(f); return v; }
    float saw(float f) {
        const float dt = f / SR;
        const float v = 2.0f * ph - 1.0f - blep(ph, dt);
        adv(f);
        return v;
    }
    float square(float f, float pw = 0.5f) {
        const float dt = f / SR;
        float v = ph < pw ? 1.0f : -1.0f;
        v += blep(ph, dt);
        float p2 = ph + 1.0f - pw;
        p2 -= std::floor(p2);
        v -= blep(p2, dt);
        adv(f);
        return v;
    }
};

// Add a pitched sinusoidal chirp (bubble / blip) starting at t0.
void addChirp(Buf& b, float t0, float dur, float f0, float f1, float amp, float tau) {
    Osc o;
    const int s0 = static_cast<int>(t0 * SR);
    const int n = static_cast<int>(dur * SR);
    for (int i = 0; i < n && s0 + i < static_cast<int>(b.size()); ++i) {
        if (s0 + i < 0) continue;
        const float t = static_cast<float>(i) / SR;
        const float env = att(t, 0.003f) * expd(t, tau) * (1.0f - prog(t, dur * 0.8f, dur));
        b[s0 + i] += amp * env * o.sine(sweep(f0, f1, t / dur));
    }
}

enum class Tone { Sine, Tri, Soft, Square, Bell };

// Add a musical note with a short attack, optional exponential decay (tau<=0
// means sustain), a 30 ms release after `dur`, and optional vibrato.
void addNote(Buf& b, float t0, float dur, float f, float amp, Tone tone, float tau = 0.0f,
             float vibHz = 0.0f, float vibDepth = 0.0f) {
    Osc o, o2;
    float lpState = 0.0f;
    const float rel = 0.03f;
    const int s0 = static_cast<int>(t0 * SR);
    const int n = static_cast<int>((dur + rel) * SR);
    const float lpCoef = 1.0f - std::exp(-TAU * std::fmin(f * 5.0f, 5000.0f) / SR);
    for (int i = 0; i < n && s0 + i < static_cast<int>(b.size()); ++i) {
        if (s0 + i < 0) continue;
        const float t = static_cast<float>(i) / SR;
        float env = att(t, 0.004f) * (tau > 0.0f ? expd(t, tau) : 1.0f);
        if (t > dur) env *= 1.0f - (t - dur) / rel;
        const float vib = 1.0f + vibDepth * prog(t, 0.08f, 0.3f) * std::sin(TAU * vibHz * t);
        const float ff = f * vib;
        float v = 0.0f;
        switch (tone) {
            case Tone::Sine:   v = o.sine(ff); break;
            case Tone::Tri:    v = o.tri(ff); break;
            case Tone::Soft: {
                // square-ish lead softened by a one-pole low-pass
                const float raw = 0.6f * o.square(ff) + 0.5f * o2.sine(ff);
                lpState += lpCoef * (raw - lpState);
                v = lpState;
                break;
            }
            case Tone::Square: v = o.square(ff, 0.5f); break;
            case Tone::Bell:
                v = o.sine(ff) + 0.35f * o2.sine(ff * 2.0f) * expd(t, 0.05f);
                break;
        }
        b[s0 + i] += amp * env * v;
    }
}

// ---------------------------------------------------------------------------
// Sound designs
// ---------------------------------------------------------------------------
Buf sExplode() {
    Buf b = make(1.2f);
    Rng r(0xE1);
    SVF body, rumble;
    Osc thump;
    float crack = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float bodyEnv = att(t, 0.004f) * (0.7f * expd(t, 0.18f) + 0.3f * expd(t, 0.55f));
        const float bodyS = body.lp(r.bi(), sweep(4000.0f, 110.0f, t / 0.9f), 0.9f) * bodyEnv;
        const float rum = rumble.lp(r.bi(), 160.0f, 1.2f) * att(t, 0.01f) * expd(t, 0.45f);
        const float th = thump.sine(sweep(120.0f, 30.0f, t / 0.4f)) * att(t, 0.003f) * expd(t, 0.22f);
        if (t < 0.6f && r.uni() < 0.004f * (1.0f - t / 0.6f)) crack = r.range(0.3f, 1.0f);
        const float cr = crack * r.bi();
        crack *= 0.992f;
        const float s = 1.6f * bodyS + 4.5f * rum + 0.9f * th + 0.25f * cr;
        b[i] = std::tanh(1.8f * s);
    }
    return b;
}

Buf sExplodeSmall() {
    Buf b = make(0.5f);
    Rng r(0xE2);
    SVF body;
    Osc thump;
    float crack = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float bodyS = body.lp(r.bi(), sweep(7000.0f, 350.0f, t / 0.35f), 0.9f) *
                            att(t, 0.002f) * expd(t, 0.09f);
        const float th = thump.sine(sweep(220.0f, 60.0f, t / 0.15f)) * att(t, 0.002f) * expd(t, 0.07f);
        if (t < 0.25f && r.uni() < 0.006f) crack = r.range(0.3f, 1.0f);
        const float cr = crack * r.bi() * expd(t, 0.12f);
        crack *= 0.985f;
        b[i] = std::tanh(1.6f * (1.4f * bodyS + 0.8f * th + 0.3f * cr));
    }
    return b;
}

Buf sFire() {
    Buf b = make(0.5f);
    Rng r(0xF1);
    SVF whoosh, pop;
    Osc thoonk;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float th = thoonk.sine(sweep(170.0f, 55.0f, t / 0.1f)) * att(t, 0.002f) * expd(t, 0.05f);
        const float p = pop.lp(r.bi(), 2000.0f) * expd(t, 0.006f);
        const float wEnv = att(t, 0.04f) * expd(t - 0.06f, 0.14f);
        const float w = whoosh.bp(r.bi(), sweep(350.0f, 2800.0f, t / 0.35f), 1.2f) * wEnv;
        b[i] = 0.9f * th + 1.2f * p + 3.0f * w;
    }
    return b;
}

Buf sThrow() {
    const float T = 0.25f;
    Buf b = make(T);
    Rng r(0x7A);
    SVF f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float x = t / T;
        const float env = x < 0.3f ? x / 0.3f : std::pow(clamp01((1.0f - x) / 0.7f), 1.5f);
        b[i] = f.bp(r.bi(), 600.0f + 1800.0f * hump(x), 1.5f) * env;
    }
    return b;
}

Buf sBounce() {
    Buf b = make(0.08f);
    Rng r(0xB0);
    Osc a, h;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float f = sweep(700.0f, 380.0f, t / 0.05f);
        b[i] = a.sine(f) * expd(t, 0.018f) + 0.35f * h.sine(f * 2.7f) * expd(t, 0.008f) +
               0.5f * r.bi() * expd(t, 0.0015f);
    }
    return b;
}

Buf sSplash() {
    Buf b = make(0.8f);
    Rng r(0x5A);
    SVF band, hiss, low;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float bnd = band.bp(r.bi(), sweep(2500.0f, 700.0f, t / 0.4f), 0.8f) *
                          att(t, 0.006f) * expd(t, 0.12f);
        const float hs = hiss.hp(r.bi(), 3000.0f) * att(t, 0.02f) * expd(t, 0.25f);
        const float lo = low.lp(r.bi(), 400.0f) * expd(t, 0.08f);
        b[i] = 2.5f * bnd + 0.5f * hs + 2.0f * lo;
    }
    Rng br(0x5B);
    for (int k = 0; k < 9; ++k) {
        const float t0 = 0.12f + 0.58f * k / 9.0f + br.range(-0.02f, 0.03f);
        const float f0 = br.range(500.0f, 1100.0f);
        const float dur = br.range(0.025f, 0.045f);
        addChirp(b, t0, dur, f0, f0 * 1.6f, 0.35f * (1.0f - 0.6f * t0 / 0.8f), dur * 0.5f);
    }
    return b;
}

// Cute rising "hup" blip.
void addHup(Buf& b, float t0, float dur, float f0, float f1) {
    Osc sq, sn;
    SVF lp;
    const int s0 = static_cast<int>(t0 * SR);
    const int n = static_cast<int>(dur * SR);
    for (int i = 0; i < n && s0 + i < static_cast<int>(b.size()); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float f = sweep(f0, f1, t / (dur * 0.9f));
        const float env = att(t, 0.006f) * std::pow(1.0f - prog(t, dur * 0.35f, dur), 1.2f);
        const float v = 0.6f * lp.lp(sq.square(f), 2500.0f) + 0.6f * sn.sine(f);
        b[s0 + i] += env * v;
    }
}

Buf sJump() {
    Buf b = make(0.15f);
    addHup(b, 0.0f, 0.15f, 280.0f, 760.0f);
    return b;
}

Buf sBackflip() {
    Buf b = make(0.25f);
    addHup(b, 0.0f, 0.11f, 350.0f, 750.0f);
    addHup(b, 0.12f, 0.13f, 450.0f, 950.0f);
    return b;
}

Buf sSelect() {
    Buf b = make(0.06f);
    Osc a, h;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float f = t < 0.02f ? 988.0f : 1318.5f;
        b[i] = (a.sine(f) + 0.25f * h.tri(f * 2.0f)) * att(t, 0.002f) * expd(t, 0.025f);
    }
    return b;
}

Buf sMenu() {
    Buf b = make(0.12f);
    Rng r(0x3E);
    Osc s, bl;
    SVF pl, sw;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float pluck = pl.lp(s.saw(523.25f), sweep(5000.0f, 600.0f, t / 0.1f), 1.2f) *
                            att(t, 0.002f) * expd(t, 0.04f);
        const float blip = bl.sine(1046.5f) * expd(t, 0.02f);
        const float swoosh = sw.bp(r.bi(), sweep(1500.0f, 5000.0f, t / 0.08f), 1.5f) * hump(t / 0.08f);
        b[i] = pluck + 0.4f * blip + 0.6f * swoosh;
    }
    return b;
}

Buf sTick() {
    Buf b = make(0.03f);
    Rng r(0x71);
    SVF hp;
    Osc a, c;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        b[i] = hp.hp(r.bi(), 2500.0f) * expd(t, 0.0025f) + 0.6f * a.sine(3200.0f) * expd(t, 0.004f) +
               0.3f * c.sine(1600.0f) * expd(t, 0.006f);
    }
    return b;
}

Buf sShotgun() {
    Buf b = make(0.4f);
    Rng r(0x56);
    SVF body, tail;
    Osc thump;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float crack = r.bi() * expd(t, 0.006f);
        const float bd = body.lp(r.bi(), sweep(4000.0f, 500.0f, t / 0.15f), 0.8f) * expd(t, 0.06f);
        const float th = thump.sine(sweep(140.0f, 45.0f, t / 0.08f)) * expd(t, 0.035f);
        const float tl = tail.lp(r.bi(), 900.0f) * att(t, 0.01f) * expd(t, 0.14f);
        b[i] = std::tanh(1.6f * (crack + 2.5f * bd + 0.9f * th + 1.5f * tl));
    }
    return b;
}

Buf sHurt() {
    const float T = 0.25f;
    Buf b = make(T);
    Rng r(0x40);
    Osc src;
    SVF f1, f2, f3, fric;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float f0 = sweep(260.0f, 150.0f, t / T) * (1.0f + 0.02f * std::sin(TAU * 7.0f * t));
        const float s = src.saw(f0);
        // "oo" -> "uh" formant glide
        const float v = f1.bp(s, sweep(330.0f, 450.0f, t / 0.15f), 6.0f) +
                        0.5f * f2.bp(s, sweep(870.0f, 1000.0f, t / 0.15f), 8.0f) +
                        0.2f * f3.bp(s, 2400.0f, 10.0f);
        const float env = att(t, 0.015f) * (t < 0.12f ? 1.0f : expd(t - 0.12f, 0.05f));
        const float f = fric.hp(r.bi(), 3000.0f) * hump(prog(t, 0.14f, T)) * 0.15f;
        b[i] = v * env + f;
    }
    return b;
}

Buf sDie() {
    const float T = 0.3f;
    Buf b = make(T);
    Rng r(0xD1);
    Osc pop, buzz;
    SVF wah, click;
    float jitter = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float p = pop.sine(sweep(900.0f, 200.0f, t / 0.03f)) * att(t, 0.001f) * expd(t, 0.015f) +
                        click.hp(r.bi(), 1500.0f) * expd(t, 0.002f);
        jitter += 0.02f * (r.bi() - jitter * 0.5f);
        const float f = sweep(180.0f, 70.0f, t / 0.25f) * (1.0f + 0.12f * jitter);
        const float pw = 0.3f + 0.1f * std::sin(TAU * 13.0f * t);
        const float bl = wah.lp(buzz.square(f, pw), sweep(1400.0f, 250.0f, prog(t, 0.02f, 0.25f)), 3.0f) *
                         att(t - 0.015f, 0.01f) * (1.0f - prog(t, 0.12f, T));
        b[i] = 0.8f * p + bl;
    }
    return b;
}

Buf sHoly() {
    const float T = 2.0f;
    Buf b = make(T);
    Rng r(0x11);
    const float notes[] = {220.0f, 277.18f, 329.63f, 440.0f, 554.37f, 659.25f};
    const float det[] = {semis(-0.07f), 1.0f, semis(0.07f)};
    constexpr int NN = 6;
    Osc saws[NN][3], sines[NN], shimmer;
    float vibPh[NN];
    for (int n = 0; n < NN; ++n) {
        vibPh[n] = r.uni();
        for (auto& o : saws[n]) o.ph = r.uni();
    }
    SVF fa, fb, flp;
    // "ha-le-lu-jaaah" vowel formant targets (F1, F2)
    struct V { float t, f1, f2; };
    const V vow[] = {{0.0f, 750, 1200}, {0.3f, 500, 1800}, {0.55f, 350, 850},
                     {0.8f, 750, 1200}, {2.0f, 750, 1200}};
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        float s = 0.0f, pure = 0.0f;
        for (int n = 0; n < NN; ++n) {
            const float vib = 1.0f + 0.006f * prog(t, 0.25f, 0.8f) *
                                         std::sin(TAU * (5.2f * t + vibPh[n]));
            const float f = notes[n] * vib;
            for (int d = 0; d < 3; ++d) s += saws[n][d].saw(f * det[d]);
            pure += sines[n].sine(f);
        }
        float F1 = 750, F2 = 1200;
        for (int k = 0; k < 4; ++k) {
            if (t >= vow[k].t && t < vow[k + 1].t) {
                const float x = clamp01((t - vow[k].t) / 0.12f);
                const float sm = x * x * (3.0f - 2.0f * x);
                const V& a = vow[k > 0 ? k - 1 : 0];
                F1 = a.f1 + (vow[k].f1 - a.f1) * sm;
                F2 = a.f2 + (vow[k].f2 - a.f2) * sm;
            }
        }
        const float choir = fa.bp(s, F1, 3.0f) + 0.8f * fb.bp(s, F2, 4.0f) + 0.35f * flp.lp(s, 3500.0f);
        const float sh = shimmer.sine(1760.0f * (1.0f + 0.004f * std::sin(TAU * 6.0f * t)));
        const float a = clamp01(t / 0.35f);
        const float env = a * a * (3.0f - 2.0f * a) * (1.0f - std::pow(prog(t, 1.45f, T), 1.5f));
        b[i] = env * (choir + 0.35f * pure + 0.15f * sh * prog(t, 0.3f, 0.8f));
    }
    return b;
}

Buf sBaa() {
    const float T = 0.5f;
    Buf b = make(T);
    Osc pulse, saw;
    SVF f1, f2, f3, lp;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float lfo = std::sin(TAU * 21.0f * t);
        const float base = t < 0.05f ? sweep(300.0f, 400.0f, t / 0.05f) : sweep(400.0f, 360.0f, prog(t, 0.3f, T));
        const float f = base * (1.0f + 0.035f * prog(t, 0.03f, 0.1f) * lfo);
        const float src = pulse.square(f, 0.3f) + 0.5f * saw.saw(f);
        const float v = f1.bp(src, 650.0f, 5.0f) + 0.7f * f2.bp(src, 1900.0f, 8.0f) +
                        0.3f * f3.bp(src, 2800.0f, 10.0f) + 0.2f * lp.lp(src, 500.0f);
        const float trem = 1.0f - 0.3f * (0.5f + 0.5f * lfo);
        const float env = att(t, 0.03f) * (1.0f - prog(t, T - 0.12f, T));
        b[i] = v * trem * env;
    }
    return b;
}

Buf sTeleport() {
    const float T = 0.5f;
    Buf b = make(T);
    const float steps[] = {0, 4, 7, 12, 16, 19, 24, 28, 31, 36};
    for (int k = 0; k < 10; ++k)
        addNote(b, 0.045f * k, 0.04f, 523.25f * semis(steps[k]), 0.5f, Tone::Bell, 0.06f);
    Rng r(0x7E);
    Osc sw;
    SVF hp;
    float gate = 0.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float trem = 0.5f + 0.5f * std::sin(TAU * 30.0f * t);
        b[i] += 0.25f * sw.sine(sweep(300.0f, 2600.0f, t / T)) * trem;
        if (r.uni() < 0.002f) gate = 1.0f;
        gate *= 0.995f;
        b[i] += 0.3f * gate * hp.hp(r.bi(), 6000.0f);
        b[i] *= att(t, 0.01f) * (1.0f - prog(t, T - 0.1f, T));
    }
    return b;
}

Buf sCrate() {
    Buf b = make(0.35f);
    addNote(b, 0.00f, 0.08f, 1046.5f, 0.6f, Tone::Bell, 0.05f);
    addNote(b, 0.07f, 0.08f, 1318.5f, 0.6f, Tone::Bell, 0.05f);
    addNote(b, 0.14f, 0.18f, 1568.0f, 0.7f, Tone::Bell, 0.09f);
    addNote(b, 0.00f, 0.08f, 1046.5f, 0.15f, Tone::Soft, 0.05f);
    addNote(b, 0.07f, 0.08f, 1318.5f, 0.15f, Tone::Soft, 0.05f);
    addNote(b, 0.14f, 0.18f, 1568.0f, 0.15f, Tone::Soft, 0.09f);
    return b;
}

Buf sTurn() {
    Buf b = make(0.4f);
    addNote(b, 0.00f, 0.09f, 659.25f, 0.6f, Tone::Soft, 0.12f);
    addNote(b, 0.10f, 0.09f, 783.99f, 0.6f, Tone::Soft, 0.12f);
    addNote(b, 0.20f, 0.17f, 1046.5f, 0.7f, Tone::Soft, 0.15f);
    addNote(b, 0.20f, 0.17f, 1046.5f, 0.3f, Tone::Bell, 0.12f);
    return b;
}

Buf sCharge() {
    const float T = 0.9f;
    Buf b = make(T);
    Osc sq, sw;
    SVF lp;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float f = sweep(160.0f, 1000.0f, t / 0.85f) * (1.0f + 0.008f * std::sin(TAU * 8.0f * t));
        const float v = lp.lp(0.6f * sq.square(f) + 0.5f * sw.saw(f), f * 4.0f, 0.9f);
        const float env = att(t, 0.02f) * (0.7f + 0.3f * t / T) * (1.0f - prog(t, T - 0.08f, T));
        b[i] = v * env;
    }
    return b;
}

Buf sVictory() {
    Buf b = make(2.0f);
    struct N { float f, t0, dur; };
    const N lead[] = {{392.0f, 0.00f, 0.10f},   {523.25f, 0.12f, 0.10f}, {659.25f, 0.24f, 0.10f},
                      {783.99f, 0.36f, 0.30f},  {659.25f, 0.70f, 0.12f}, {783.99f, 0.84f, 0.12f},
                      {1046.5f, 0.98f, 0.85f}};
    for (const N& n : lead) {
        const bool last = n.dur > 0.5f;
        addNote(b, n.t0, n.dur, n.f, 0.5f, Tone::Soft, last ? 1.2f : 0.4f, last ? 5.5f : 0.0f,
                last ? 0.012f : 0.0f);
    }
    // final chord harmony
    addNote(b, 0.98f, 0.85f, 659.25f, 0.2f, Tone::Soft, 1.0f);
    addNote(b, 0.98f, 0.85f, 783.99f, 0.2f, Tone::Soft, 1.0f);
    const N bass[] = {{130.81f, 0.00f, 0.32f}, {98.0f, 0.36f, 0.30f}, {130.81f, 0.70f, 0.12f},
                      {98.0f, 0.84f, 0.12f},   {130.81f, 0.98f, 0.85f}};
    for (const N& n : bass) addNote(b, n.t0, n.dur, n.f, 0.55f, Tone::Tri, 0.6f);
    return b;
}

Buf sFuse() {
    const float T = 1.0f;
    Buf b = make(T);
    Rng r(0xF5);
    SVF hiss, burst;
    Osc pop;
    float flutter = 0.0f, crack = 0.0f, popEnv = 0.0f, popF = 1000.0f;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        flutter += 0.002f * (r.bi() - flutter);
        const float hs = hiss.hp(r.bi(), 4000.0f) * (0.25f + 4.0f * std::fabs(flutter));
        if (r.uni() < 0.003f) crack = r.range(0.3f, 1.0f);
        const float cr = crack * burst.hp(r.bi(), 2500.0f);
        crack *= 0.97f;
        if (r.uni() < 0.0004f) { popEnv = 1.0f; popF = r.range(800.0f, 1500.0f); }
        const float pp = popEnv * pop.sine(popF);
        popEnv *= 0.985f;
        b[i] = (0.6f * hs + cr + 0.4f * pp) * att(t, 0.02f) * (1.0f - prog(t, T - 0.1f, T));
    }
    return b;
}

Buf sBat() {
    Buf b = make(0.2f);
    Rng r(0xBA);
    SVF click, thw;
    Osc body, w1, w2;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float c = click.bp(r.bi(), 2500.0f, 1.0f) * expd(t, 0.003f);
        const float bd = body.sine(sweep(260.0f, 140.0f, t / 0.05f)) * expd(t, 0.04f);
        const float wood = w1.sine(820.0f) * expd(t, 0.025f) * 0.5f + w2.sine(1930.0f) * expd(t, 0.012f) * 0.3f;
        const float tw = thw.lp(r.bi(), 1800.0f) * expd(t, 0.02f);
        b[i] = std::tanh(1.5f * (2.0f * c + bd + wood + 1.5f * tw));
    }
    return b;
}

Buf sDrown() {
    const float T = 0.8f;
    Buf b = make(T);
    const float times[] = {0.0f, 0.1f, 0.19f, 0.3f, 0.4f, 0.52f, 0.63f};
    for (int k = 0; k < 7; ++k) {
        const float f0 = sweep(900.0f, 300.0f, k / 6.0f);
        addChirp(b, times[k], 0.08f, f0, f0 * 1.5f, 0.6f * (1.0f - 0.05f * k), 0.035f);
    }
    Rng r(0xD2);
    SVF lp;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float am = 0.5f + 0.5f * std::sin(TAU * 8.0f * t);
        b[i] += 2.0f * lp.lp(r.bi(), 250.0f, 1.5f) * am * hump(t / T);
    }
    return b;
}

Buf sBeep() {
    Buf b = make(0.1f);
    addNote(b, 0.0f, 0.065f, 1760.0f, 0.8f, Tone::Sine);
    addNote(b, 0.0f, 0.065f, 3520.0f, 0.16f, Tone::Sine);
    return b;
}

Buf sAirstrike() {
    const float T = 1.5f, C = 0.75f;
    Buf b = make(T);
    Rng r(0xA1);
    Osc s1, s2, sq;
    SVF lp, nb;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        const float dop = 1.0f - 0.07f * std::tanh((t - C) / 0.25f); // high approaching, low leaving
        const float x = (t - C) / 0.38f;
        const float amp = std::exp(-x * x);
        const float f = 85.0f * dop;
        const float drone = s1.saw(f) + 0.6f * s2.saw(f * 1.01f) + 0.4f * sq.square(2.0f * f);
        const float d = lp.lp(drone, 400.0f + 2200.0f * amp, 1.0f);
        const float n = nb.bp(r.bi(), 900.0f * dop, 0.9f);
        b[i] = (0.6f * d + 2.4f * n) * amp;
    }
    return b;
}

Buf sLand() {
    Buf b = make(0.1f);
    Rng r(0x1A);
    Osc th;
    SVF lp;
    for (size_t i = 0; i < b.size(); ++i) {
        const float t = static_cast<float>(i) / SR;
        b[i] = (th.sine(sweep(110.0f, 45.0f, t / 0.06f)) * expd(t, 0.03f) +
                3.0f * lp.lp(r.bi(), 500.0f) * expd(t, 0.015f)) * att(t, 0.002f);
    }
    return b;
}

// ---------------------------------------------------------------------------
// Registry / playback
// ---------------------------------------------------------------------------
struct Def {
    const char* name;
    Buf (*synth)();
    float peak;
};

const Def DEFS[] = {
    {"explode", sExplode, 0.85f},     {"explode_small", sExplodeSmall, 0.8f},
    {"fire", sFire, 0.8f},            {"throw", sThrow, 0.6f},
    {"bounce", sBounce, 0.6f},        {"splash", sSplash, 0.75f},
    {"jump", sJump, 0.45f},           {"backflip", sBackflip, 0.45f},
    {"select", sSelect, 0.45f},       {"menu", sMenu, 0.5f},
    {"tick", sTick, 0.45f},           {"shotgun", sShotgun, 0.8f},
    {"hurt", sHurt, 0.7f},            {"die", sDie, 0.75f},
    {"holy", sHoly, 0.75f},           {"baa", sBaa, 0.7f},
    {"teleport", sTeleport, 0.6f},    {"crate", sCrate, 0.6f},
    {"turn", sTurn, 0.6f},            {"charge", sCharge, 0.5f},
    {"victory", sVictory, 0.7f},      {"fuse", sFuse, 0.45f},
    {"bat", sBat, 0.8f},              {"drown", sDrown, 0.7f},
    {"beep", sBeep, 0.4f},            {"airstrike", sAirstrike, 0.75f},
    {"land", sLand, 0.65f},
};
constexpr int NUM_DEFS = static_cast<int>(sizeof(DEFS) / sizeof(DEFS[0]));
constexpr int VOICES = 4;

struct Effect {
    Sound voices[VOICES];
    int count = 0;
    int next = 0;
};

Effect g_effects[NUM_DEFS];
bool g_ready = false;
float g_master = 1.0f;

// DC block, click-free fades, peak-normalize, and convert to a 16-bit Wave.
Sound bake(Buf& b, float peak) {
    const int n = static_cast<int>(b.size());
    // DC blocker (~20 Hz one-pole high-pass)
    float x1 = 0.0f, y1 = 0.0f;
    const float R = 1.0f - TAU * 20.0f / SR;
    for (int i = 0; i < n; ++i) {
        const float y = b[i] - x1 + R * y1;
        x1 = b[i];
        y1 = y;
        b[i] = y;
    }
    const int fin = n < SR / 1000 ? n : SR / 1000;          // 1 ms
    const int fout = n < SR * 8 / 1000 ? n : SR * 8 / 1000; // 8 ms
    for (int i = 0; i < fin; ++i) b[i] *= static_cast<float>(i) / fin;
    for (int i = 0; i < fout; ++i) b[n - 1 - i] *= static_cast<float>(i) / fout;

    float mx = 0.0f;
    for (float v : b) mx = std::fmax(mx, std::fabs(v));
    const float g = mx > 1e-6f ? peak / mx : 0.0f;

    auto* data = static_cast<short*>(MemAlloc(static_cast<unsigned int>(n * sizeof(short))));
    if (!data) return Sound{};
    for (int i = 0; i < n; ++i) {
        float v = b[i] * g;
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        data[i] = static_cast<short>(std::lround(v * 32767.0f));
    }
    Wave w;
    w.frameCount = static_cast<unsigned int>(n);
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

} // namespace

void init() {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;

    if (!IsAudioDeviceReady()) InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "SFX: audio device unavailable, sound disabled");
        return;
    }
    SetMasterVolume(g_master);

    const auto start = std::chrono::steady_clock::now();
    size_t totalFrames = 0;
    for (int d = 0; d < NUM_DEFS; ++d) {
        Buf buf = DEFS[d].synth();
        totalFrames += buf.size();
        Sound base = bake(buf, DEFS[d].peak);
        Effect& e = g_effects[d];
        if (!IsSoundValid(base)) continue;
        e.voices[e.count++] = base;
        for (int v = 1; v < VOICES; ++v) {
            Sound alias = LoadSoundAlias(base);
            if (IsSoundValid(alias)) e.voices[e.count++] = alias;
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    TraceLog(LOG_INFO, "SFX: synthesized %d sounds (%.2f s of audio) in %.1f ms", NUM_DEFS,
             static_cast<double>(totalFrames) / SR, ms);
    g_ready = true;
}

void playSound(const char* name, float volume, float pitch) {
    if (!g_ready || !name) return;
    for (int d = 0; d < NUM_DEFS; ++d) {
        if (std::strcmp(DEFS[d].name, name) != 0) continue;
        Effect& e = g_effects[d];
        if (e.count == 0) return;
        // Prefer an idle voice; otherwise steal round-robin.
        int pick = e.next;
        for (int k = 0; k < e.count; ++k) {
            const int idx = (e.next + k) % e.count;
            if (!IsSoundPlaying(e.voices[idx])) { pick = idx; break; }
        }
        e.next = (pick + 1) % e.count;
        const Sound& s = e.voices[pick];
        SetSoundVolume(s, clamp01(volume));
        SetSoundPitch(s, pitch > 0.01f ? pitch : 1.0f);
        PlaySound(s);
        return;
    }
}

void setMasterVolume(float v) {
    g_master = clamp01(v);
    if (g_ready || IsAudioDeviceReady()) SetMasterVolume(g_master);
}

} // namespace Audio
