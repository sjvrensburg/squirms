#pragma once
// The vendored raylib ships only raylib.h/raymath.h, but libraylib.a is built
// with rlgl inside it. These are the handful of immediate-mode rlgl entry
// points the renderer needs for textured terrain polygons, declared with the
// same C signatures as rlgl.h.
extern "C" {
void rlBegin(int mode);
void rlEnd(void);
void rlVertex2f(float x, float y);
void rlTexCoord2f(float x, float y);
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void rlSetTexture(unsigned int id);
void rlDisableBackfaceCulling(void);
void rlEnableBackfaceCulling(void);
bool rlCheckRenderBatchLimit(int vCount);
}

#ifndef RL_QUADS
#define RL_TRIANGLES 0x0004
#define RL_QUADS     0x0007
#endif
