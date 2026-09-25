#pragma once

// Procedurally synthesized sound effects. No asset files: every sound is
// generated sample-by-sample at startup (see sfx.cpp for the name list).
namespace Audio {

void init();                       // InitAudioDevice() + synthesize all sounds. Safe to call once.
// Play a named sound. Unknown names are silently ignored. volume 0..1, pitch multiplier (1 = normal).
void playSound(const char* name, float volume = 1.0f, float pitch = 1.0f);
void setMasterVolume(float v);     // 0..1

} // namespace Audio
