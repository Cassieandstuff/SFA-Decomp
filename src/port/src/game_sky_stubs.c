// game_sky_stubs.c - link stand-ins for the sky DLL (src/dlls/engine/5/5.c).
//
// The sky vertical slice drives only the real time-of-day path (skyResetState +
// skyUpdateTimeOfDay + getSunPos), which touches just a handful of these. The rest are
// referenced by sky's render/lighting functions we don't call yet, so they only need to
// LINK. Defined with no game headers on purpose (bare-symbol linkage), like
// game_boot_stubs.c, so there are no signature conflicts.
//
// The five that actually run during skyResetState/skyUpdateTimeOfDay return valid objects:
//   textureLoadAsset -> a fake 64x64 Texture (skyResetState reads tex->width/height)
//   textureAlloc     -> a real buffer (stored, later mm_free'd - safe if not mm-owned)
//   textureFree/saveGameGetEnvState/randomGetRange -> benign values.

#include <stdlib.h>

// --- runs during the slice --------------------------------------------------
// Texture: width @0x0A, height @0x0C (main/texture.h). 0x100 bytes is plenty.
static unsigned char gFakeTex[0x100];
void* textureLoadAsset(int id) {
    (void)id;
    *(unsigned short*)(gFakeTex + 0x0A) = 64;   // width
    *(unsigned short*)(gFakeTex + 0x0C) = 64;   // height
    return gFakeTex;
}
void* textureAlloc(void) { return malloc(0x1000); }
void  textureFree(void) { }
static unsigned char gEnvState[0x100];
void* saveGameGetEnvState(void) { return gEnvState; }
int   randomGetRange(void) { return 0; }

// --- link-only (sky render/lighting paths, not exercised yet) ---------------
// The real player DLL calls Camera_GetCurrent() first thing and dereferences cam->yaw/pos/etc.
// (camera-relative controls, focus math). A null return crashes it, so hand back a valid, zeroed
// Camera (0x80 bytes covers the 0x60 struct + slack). Zeroed = yaw 0, origin - a safe default;
// Phase B will populate it from the port's live follow-camera so controls track what's rendered.
static unsigned char gStubCamera[0x80];
void* Camera_GetCurrent(void) {
    // scale (0x8) and fovY (0x18) must be non-zero: the player divides by them in its camera-relative
    // math, so leaving them 0 makes the character's position go NaN. Sane defaults until Phase B
    // feeds the real port camera.
    static int init = 0;
    if (!init) { init = 1; *(float*)(gStubCamera + 0x08) = 1.0f; *(float*)(gStubCamera + 0x18) = 1.0f; }
    return gStubCamera;
}

// Interim camera shim (Milestone 2): game_scene pushes the port follow-cam pose here each frame so
// the REAL playerUpdate - which reads Camera_GetCurrent()->yaw/pos for its camera-relative controls -
// tracks the on-screen view instead of a fixed yaw 0. Field offsets per include/main/camera.h:
// yaw@0x00 (s16), pitch@0x02 (s16), scale@0x08, pos x/y/z@0x0C/0x10/0x14, fovY@0x18. scale + fovY
// keep their non-zero init defaults (the player divides by them). Replaced by the real
// engine/1_camcontrol view camera once that DLL is brought up.
void stairfax_camera_set_pose(short yaw, short pitch, float x, float y, float z) {
    *(short*)(gStubCamera + 0x00) = yaw;
    *(short*)(gStubCamera + 0x02) = pitch;
    *(float*)(gStubCamera + 0x08) = 1.0f;   // scale (safety; player divides by it)
    *(float*)(gStubCamera + 0x0C) = x;
    *(float*)(gStubCamera + 0x10) = y;
    *(float*)(gStubCamera + 0x14) = z;
}
int Camera_GetFarPlane(void) { return 0; }
int Camera_GetFovY(void) { return 0; }
int Camera_GetInverseViewMatrix(void) { return 0; }
int Camera_RebuildProjectionMatrix(void) { return 0; }
int Camera_SetFarPlane(void) { return 0; }
int Curve_EvalCatmullRom(void) { return 0; }
int Curve_EvalLinear(void) { return 0; }
int GXSetFog(void) { return 0; }
int GXSetNumIndStages(void) { return 0; }
int GXSetNumTevStages(void) { return 0; }
int GXSetNumTexGens(void) { return 0; }
int GXSetTevAlphaIn(void) { return 0; }
int GXSetTevAlphaOp(void) { return 0; }
int GXSetTevColorIn(void) { return 0; }
int GXSetTevColorOp(void) { return 0; }
int GXSetTevDirect(void) { return 0; }
int GXSetTevOrder(void) { return 0; }
int GXSetTevSwapMode(void) { return 0; }
int GXSetTexCoordGen2(void) { return 0; }
/* PSMTXConcat / PSMTXMultVecSR / PSMTXScale are real in game_model_support.c (model render path). */
int PSVECMag(void) { return 0; }
int PSVECNormalize(void) { return 0; }
int PSVECScale(void) { return 0; }
int blendTextures(void) { return 0; }
int colorScale(void) { return 0; }
int coordsToMapCell(void) { return 0; }
int drawOrthoTexturedQuad(void) { return 0; }
int getEnvfxAct(void) { return 0; }
int getEnvfxActImmediately(void) { return 0; }
int getLoadedFileFlags(void) { return 0; }
int getSaveGameLoadStatus(void) { return 0; }
int getScreenResolution(void) { return 0; }
int gxSetOpaqueNoZWriteMode(void) { return 0; }
int lightSetColor(void) { return 0; }
// mathCosf(radians): the game's cosine (arg in radians). Was an int return-0 stub - float-returning
// stubbed as int -> garbage xmm0 -> the player's heading (velocityX/Z from mathCosf(yaw)) went NaN.
extern float cosf(float);
float mathCosf(float radians) { return cosf(radians); }
int modelLightStruct_getWorldPosition(void) { return 0; }
int modelLightStruct_selectObjectLights(void) { return 0; }
int modelLightStruct_setDiffuseColor(void) { return 0; }
int modelLightStruct_setDirection(void) { return 0; }
int modelLightStruct_setLightKind(void) { return 0; }
int modelLightStruct_setSpecularColor(void) { return 0; }
int moonFxRenderCallback(void) { return 0; }
int objCreateLight(void) { return 0; }
int objRender(void) { return 0; }
int shadowSetLightDirection(void) { return 0; }
int vecRotateZXY(void) { return 0; }

// data referenced by address
int lbl_803E8458[64];
