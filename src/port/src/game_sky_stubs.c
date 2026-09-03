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
int Camera_GetCurrent(void) { return 0; }
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
int PSMTXConcat(void) { return 0; }
int PSMTXMultVecSR(void) { return 0; }
int PSMTXScale(void) { return 0; }
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
int mathCosf(void) { return 0; }
int modelLightStruct_getWorldPosition(void) { return 0; }
int modelLightStruct_selectObjectLights(void) { return 0; }
int modelLightStruct_setDiffuseColor(void) { return 0; }
int modelLightStruct_setDirection(void) { return 0; }
int modelLightStruct_setLightKind(void) { return 0; }
int modelLightStruct_setSpecularColor(void) { return 0; }
int moonFxRenderCallback(void) { return 0; }
int objCreateLight(void) { return 0; }
int objRender(void) { return 0; }
int selectTexture(void) { return 0; }
int shadowSetLightDirection(void) { return 0; }
int vecRotateZXY(void) { return 0; }

// data referenced by address
int lbl_803E8458[64];
