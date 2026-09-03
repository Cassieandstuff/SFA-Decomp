// game_objprint_stubs.c - satisfy objprint_dolphin.c's dependency graph for an UNTEXTURED,
// UNLIT first pass of the REAL model render path (objRenderModel -> modelDoRenderInstrs).
//
// The geometry decode + skinning is fully REAL (objprint's render-instruction interpreter +
// gx_draw's DL decoder + model.c's ObjModel_UpdateAnimMatrices). What is stubbed here is only
// the TEV/texture/light/shadow/fuzz stage setup and a few camera/math bridges - i.e. shading,
// which comes later by compiling rcp_dolphin.c/shader_dolphin.c/modellight.c/newshadows.c for
// real. These stubs are SAFE no-ops: pointer-returning ones hand back a zeroed dummy so any
// field read by the real code is 0 and never faults. Signatures are intentionally (void) - the
// callers use the real prototypes from headers; cdecl lets the caller clean the stack.

// Forward-declare the CRT double math we need rather than <math.h> - that header defines
// asinf/atan2f as inlines, which collide with the real symbols the game references extern.
double atan2(double, double);
double asin(double);

static unsigned char gDummy[4096];   // zeroed; returned where real code may read fields

// --- camera bridge: the real render path asks for the current view matrix ----
// game_scene sets this each frame from its camView (3x4, row-major). Camera_GetViewMatrix
// returns it so modelDoRenderInstrs composes world*view exactly as retail does.
float gPortViewMatrix[3][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};
float* Camera_GetViewMatrix(void) { return &gPortViewMatrix[0][0]; }
// gCameraLightPerspectiveMatrix: used to build projected-light tex matrices (unused untextured).
float gCameraLightPerspectiveMatrix[3][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};

// culling / projection helpers - return benign values so nothing culls the player away.
float Camera_DistanceToCurrentViewPosition(void) { return 1000.0f; }
int   Camera_ClipToScreen(void) { return 0; }
int   Camera_ProjectWorldPointWithOffset(void) { return 0; }
void  Obj_TransformWorldPointToLocal(void) { }

// --- math bridges (real) -----------------------------------------------------
// Matrix_TransformPoint(m, in, out): out = m(3x4) * (in,1). setMatrixFromObjectPos: identity+trans.
void Matrix_TransformPoint(const float* m, const float* in, float* out) {
    float x=in[0], y=in[1], z=in[2];
    out[0]=m[0]*x+m[1]*y+m[2]*z+m[3];
    out[1]=m[4]*x+m[5]*y+m[6]*z+m[7];
    out[2]=m[8]*x+m[9]*y+m[10]*z+m[11];
}
void setMatrixFromObjectPos(float* m, float x, float y, float z) {
    m[0]=1;m[1]=0;m[2]=0;m[3]=x; m[4]=0;m[5]=1;m[6]=0;m[7]=y; m[8]=0;m[9]=0;m[10]=1;m[11]=z;
}
float atan2f_fast(float y, float x) { return (float)atan2((double)y, (double)x); }
float asinf(float x) { return (float)asin((double)x); }
int   getAngle(int x, int z) { return (int)(atan2((double)x,(double)z)/3.14159265*32768.0); }

// --- GX matrix/TEV/channel state: no-ops for untextured (RHI ignores TEV) -----
// (GXLoadNrmMtxImm is real in gx_draw.c.)
void GXSetChanAmbColor(void) {}
void GXSetChanMatColor(void) {}
void GXSetTevColor(void) {}
void GXSetTevColorS10(void) {}
void GXSetTevKColor(void) {}
void GXSetTevKColorSel(void) {}
void GXSetTevKAlphaSel(void) {}
void GXSetTevIndirect(void) {}
void GXSetIndTexMtx(void) {}
void GXSetIndTexCoordScale(void) {}
void GXSetIndTexOrder(void) {}
void _gxSetFogParams(void) {}

// --- Rcp / TEV stage builders (rcp_dolphin.c / shader_dolphin.c): no-op -------
void Rcp_ResetTextureStageState(void) {}
void Rcp_ApplyTextureStageCounts(void) {}
void* Shader_getLayer(void) { return gDummy; }
int  textureGetAnimationFrame(void) { return 0; }
void addVertexColorStage(void) {}
void addVertexColorKAlphaStage(void) {}
void addTexLayerStage(void) {}
void addTexLayerStageKColor(void) {}
void addTexLayerStageKAlpha(void) {}
void addTexLayerStageSwizzled(void) {}
void addKColorModulateStage(void) {}
void addColorFadeStage(void) {}
void addLitColorStage(void) {}
void addAlphaLitColorReg2Stage(void) {}
void addLightTexReg2Stage(void) {}
void addTexModulateReg2Stage(void) {}
int  addEnvMapBumpStages(void) { return 0; }
void addEnvMapTexCoord(void) {}
void addSphereMapTexStage(void) {}
void addProjectedLightTevStage(void) {}
void addSmallReflectionTevStage(void) {}
void addShadowFalloffTevStages(void) {}
void addCastShadowTevStages(void) {}
void addWavyCausticTevStage(void) {}
void addWarpedNoiseTevStages(void) {}
void addRenderOpFadeStage(void) {}
void AttractMovie_AddVideoTevStages(void) {}
int  isHeavyFogEnabled(void) { return 0; }
void renderHeavyFog(void) {}
void getFogColorRgb(void) {}

// --- model light channels (modellight.c): no-op (default flat lighting) -------
void lightGetColor(void) {}
void modelLightChannel_configure(void) {}
void modelLightChannels_applyGXControls(void) {}
void modelLightChannels_reset(void) {}
void ModelLightStruct_free(void) {}
void modelLightStruct_loadChannelLight(void) {}
int  modelLightStruct_getProjectedLightChannelPreference(void) { return 0; }
void* modelLightStruct_getProjectionTexMtx(void) { return gDummy; }
void* modelLightStruct_getProjectionTexture(void) { return gDummy; }
int  modelLightStruct_getProjectionTevModes(void) { return 0; }

// --- newshadows / shadow / hud (fuzz+shadow passes; not the normal pass) ------
void* newshadows_getCausticTexture(void) { return gDummy; }
void* newshadows_getReflectionScrollOffsets(void) { return gDummy; }
void* newshadows_getNoiseTextureFrames(void) { return gDummy; }
void* newshadows_getShadowTextureTable4x8(void) { return gDummy; }
void  getObjectShadowDrawParams(void) {}
int   depthReadRequestPoll(void) { return 0; }
void  hudDrawColored(void) {}
