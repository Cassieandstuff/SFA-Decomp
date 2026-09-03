// game_engine_stubs.cpp - Phase 3 bring-up stubs for gameloop_main.c (init/gameLoop).
//
// init()/gameLoop() reference ~169 engine symbols not yet ported. Stand-ins to get the
// real game loop LINKING and RUNNING natively: functions no-op (0, or 1 for init() loop
// gates); Resource_Acquire returns the shared no-op interface (so `gX = Resource_Acquire()`
// leaves a valid vtable); DLL interface pointers point at that no-op vtable; POINTER
// globals point at real zeroed storage; value/array globals get storage. Replaced by real
// game TUs as subsystems come up. STAIRFAX_TRACE=1 logs stub calls.

#include "dolphin/gx/GXStruct.h"
#include <cstdio>
#include <cstdlib>
#include <windows.h>

extern "C" GXRenderModeObj GXNtsc480IntDf = {};
static int gTrace = 0;
static void gEngineNoop(void) {}
extern "C" { void* gEngineStubVtbl[256]; }
static void* gEngineStubVtblPtr = gEngineStubVtbl;  // one "interface" = &this
// Crash diagnostics: print the exception code + faulting address (module-relative) so a
// silent access violation in recompiled game code can be located via the .map/objdump.
static LONG WINAPI stairfaxCrashFilter(EXCEPTION_POINTERS* ep) {
    void* pc = (void*)ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = GetModuleHandleW(NULL);
    fprintf(stderr, "\n[CRASH] code=0x%08lX at %p (module base %p, +0x%tX)\n",
            ep->ExceptionRecord->ExceptionCode, pc, (void*)mod,
            (char*)pc - (char*)mod);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        fprintf(stderr, "[CRASH] access %s addr %p\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
struct EngineVtblInit {
    EngineVtblInit(){
        setbuf(stdout, 0); setbuf(stderr, 0);
        SetUnhandledExceptionFilter(stairfaxCrashFilter);
        gTrace = getenv("STAIRFAX_TRACE") ? 1 : 0;
        for(int i=0;i<256;++i) gEngineStubVtbl[i]=(void*)gEngineNoop;
        GXNtsc480IntDf.fbWidth=640; GXNtsc480IntDf.efbHeight=480; GXNtsc480IntDf.xfbHeight=480;
        GXNtsc480IntDf.viWidth=640; GXNtsc480IntDf.viHeight=480;
    }
};
static EngineVtblInit gEngineVtblInit;
static void trace(const char* n){ if(gTrace) fprintf(stderr,"[stub] %s\n", n); }
static unsigned char gPtrGlobalStore[3][0x10000];

extern "C" {
// Interface acquisition returns the shared no-op interface (**vtable).
void** gBaddieControlInterface = (void**)&gEngineStubVtblPtr;
void** gBoneParticleEffectInterface = (void**)&gEngineStubVtblPtr;
void** gCameraInterface = (void**)&gEngineStubVtblPtr;
void** gCarryableInterface = (void**)&gEngineStubVtblPtr;
void** gCheckpointInterface = (void**)&gEngineStubVtblPtr;
void** gCloudActionInterface = (void**)&gEngineStubVtblPtr;
void** gDll12Interface = (void**)&gEngineStubVtblPtr;
void** gExpgfxInterface = (void**)&gEngineStubVtblPtr;
void** gGameUIInterface = (void**)&gEngineStubVtblPtr;
void** gMapEventInterface = (void**)&gEngineStubVtblPtr;
void** gMinimapInterface = (void**)&gEngineStubVtblPtr;
void** gModgfxInterface = (void**)&gEngineStubVtblPtr;
void** gNewCloudsInterface = (void**)&gEngineStubVtblPtr;
void** gObjectTriggerInterface = (void**)&gEngineStubVtblPtr;
void** gPartfxInterface = (void**)&gEngineStubVtblPtr;
void** gPathControlInterface = (void**)&gEngineStubVtblPtr;
void** gPlayerInterface = (void**)&gEngineStubVtblPtr;
void** gPlayerShadowInterface = (void**)&gEngineStubVtblPtr;
void** gProjgfxInterface = (void**)&gEngineStubVtblPtr;
void** gRomCurveInterface = (void**)&gEngineStubVtblPtr;
void** gScreenTransitionInterface = (void**)&gEngineStubVtblPtr;
void** gScreensInterface = (void**)&gEngineStubVtblPtr;
void** gSky2Interface = (void**)&gEngineStubVtblPtr;
void** gSkyInterface = (void**)&gEngineStubVtblPtr;
void** gTitleMenuControlInterface = (void**)&gEngineStubVtblPtr;
void** gTitleMenuControlInterfaceCopy = (void**)&gEngineStubVtblPtr;
void** gTitleMenuItemInterface = (void**)&gEngineStubVtblPtr;
void** gTitleMenuLinkInterface = (void**)&gEngineStubVtblPtr;
void** gWaterfxInterface = (void**)&gEngineStubVtblPtr;
void* gAskProgressiveScanFlag = gPtrGlobalStore[0];
void* gGameBitSaveData        = gPtrGlobalStore[1];
void* gGameBitTable           = gPtrGlobalStore[2];
int frameCountdown[256];
int gAudioStreamDvdState[256];
int gAudioStreamPlaying[256];
int gDvdCoverOpenErrorActive[256];
int gDvdLastDriveStatus[256];
int gGameBitCount[256];
int gGameLoopButtonObjectCount[256];
int gGameLoopButtonObjects[256];
int gGameLoopFullMapUnloadPending[256];
int gGameLoopHardReset[256];
int gGameLoopInitComplete[256];
int gGameLoopMapLoadPending[256];
int gGameLoopMapLoaded[256];
int gGameLoopMusicActive[256];
int gGameLoopMusicFadeTimer[256];
int gGameLoopMusicRequestCount[256];
int gGameLoopPendingMapDataFileId[256];
int gGameLoopPendingMapId[256];
int gGameLoopPendingMusicId[256];
int gGameLoopPendingUiDllId[256];
int gGameLoopPlayerTrailIndex[256];
int gGameLoopPlayerTrailTime[256];
int gGameLoopProgressiveMode[256];
int gGameLoopReloadRequested[256];
int gGameLoopResetComboDebounce[256];
int gGameLoopResetFadeOutTimer[256];
int gGameLoopResetHoldTimer[256];
int gVideoRetracePending[256];
int gameState[256];
int hudHiddenFrameCount[256];
int lbl_803DCA38[256];
int lbl_803DCA3F[256];
int sGameLoopResetMessages[256];
int screenBlankFrameCount[256];
int shouldResetNextFrame[256];
int timeStop[256];
int AISetStreamPlayState(){ trace("AISetStreamPlayState"); return 0; }
int AISetStreamVolLeft(){ trace("AISetStreamVolLeft"); return 0; }
int AISetStreamVolRight(){ trace("AISetStreamVolRight"); return 0; }
int Camera_ApplyCurrentViewport(){ trace("Camera_ApplyCurrentViewport"); return 0; }
int Camera_InitState(){ trace("Camera_InitState"); return 0; }
int Camera_UpdateShakeAndFarPlane(){ trace("Camera_UpdateShakeAndFarPlane"); return 0; }
int LCDisable(){ trace("LCDisable"); return 0; }
int LCEnable(){ trace("LCEnable"); return 0; }
int Music_Trigger(){ trace("Music_Trigger"); return 0; }
int OSInitFastCast(){ trace("OSInitFastCast"); return 0; }
int OSSetSaveRegion(){ trace("OSSetSaveRegion"); return 0; }
int Rcp_InitDistortionEffects(){ trace("Rcp_InitDistortionEffects"); return 0; }
int Sfx_SetObjectSoundsPaused(){ trace("Sfx_SetObjectSoundsPaused"); return 0; }
int Sfx_UpdateLoopedObjectSounds(){ trace("Sfx_UpdateLoopedObjectSounds"); return 0; }
int _initCardAndDsp(){ trace("_initCardAndDsp"); return 0; }
int allocSomething32bytes(){ trace("allocSomething32bytes"); return 0; }
int askProgressiveScanMode(){ trace("askProgressiveScanMode"); return 0; }
int audioInit(){ trace("audioInit"); return 1; }
int audioReset(){ trace("audioReset"); return 0; }
int audioStopAll(){ trace("audioStopAll"); return 0; }
int audioUpdate(){ trace("audioUpdate"); return 0; }
int beginLoadingMap(){ trace("beginLoadingMap"); return 0; }
int camcontrol_setAButtonIconForTarget(){ trace("camcontrol_setAButtonIconForTarget"); return 0; }
int clearForceLoadImmediately(){ trace("clearForceLoadImmediately"); return 0; }
int curUiDllDraw(){ trace("curUiDllDraw"); return 0; }
int debugPrintDraw(){ trace("debugPrintDraw"); return 0; }
int debugPrintInit(){ trace("debugPrintInit"); return 0; }
int debugPrintReset(){ trace("debugPrintReset"); return 0; }
int doNothing_beforeTitleScreen(){ trace("doNothing_beforeTitleScreen"); return 0; }
int doNothing_endOfFrame(){ trace("doNothing_endOfFrame"); return 0; }
int doNothing_startOfFrame(){ trace("doNothing_startOfFrame"); return 0; }
int doPendingMapLoads(){ trace("doPendingMapLoads"); return 0; }
int drawRect(){ trace("drawRect"); return 0; }
int dvdCheckError(){ trace("dvdCheckError"); return 0; }
int errDisplayInstallHandlers(){ trace("errDisplayInstallHandlers"); return 0; }
int gameTextInit(){ trace("gameTextInit"); return 0; }
int gameTextInitRendererState(){ trace("gameTextInitRendererState"); return 0; }
int gameTextLoadDir(){ trace("gameTextLoadDir"); return 0; }
int gameTextRun(){ trace("gameTextRun"); return 0; }
int gameTextSetDrawFunc(){ trace("gameTextSetDrawFunc"); return 0; }
int initGameTimer(){ trace("initGameTimer"); return 0; }
int initLoadFiles(){ trace("initLoadFiles"); return 1; }
int initLoadingScreenTextures(){ trace("initLoadingScreenTextures"); return 0; }
int initMapBlocks(){ trace("initMapBlocks"); return 0; }
int initMaps(){ trace("initMaps"); return 0; }
int initSkyStars(){ trace("initSkyStars"); return 0; }
int initTextures(){ trace("initTextures"); return 0; }
int isSaveGameLoading(){ trace("isSaveGameLoading"); return 0; }
int loadDataFiles(){ trace("loadDataFiles"); return 0; }
int loadMapAndParent(){ trace("loadMapAndParent"); return 0; }
int loadTaskTexts(){ trace("loadTaskTexts"); return 0; }
int loadTextureFiles(){ trace("loadTextureFiles"); return 0; }
int loadUiDll(){ trace("loadUiDll"); return 0; }
int mainLoopDoGameText(){ trace("mainLoopDoGameText"); return 0; }
int mapLoadDataFiles(){ trace("mapLoadDataFiles"); return 0; }
int mapReloadWithFadeout(){ trace("mapReloadWithFadeout"); return 0; }
int mapSetup(){ trace("mapSetup"); return 0; }
int mapUnload(){ trace("mapUnload"); return 0; }
int mapUpdateCameraPosByTransformSpace(){ trace("mapUpdateCameraPosByTransformSpace"); return 0; }
int newshadows_initProceduralTextures(){ trace("newshadows_initProceduralTextures"); return 0; }
int objRenderModelAndHitVolumes(){ trace("objRenderModelAndHitVolumes"); return 0; }
int playerInitFuncPtrsEntry(){ trace("playerInitFuncPtrsEntry"); return 0; }
int resetSomeGxFlags(){ trace("resetSomeGxFlags"); return 0; }
int runLoadingScreens(){ trace("runLoadingScreens"); return 0; }
int setDrawCloudsAndLights(){ trace("setDrawCloudsAndLights"); return 0; }
int setForceLoadImmediately(){ trace("setForceLoadImmediately"); return 0; }
int subtitleInit(){ trace("subtitleInit"); return 0; }
int subtitleUpdateAndDraw(){ trace("subtitleUpdateAndDraw"); return 0; }
int taskHintRecordCompletedTask(){ trace("taskHintRecordCompletedTask"); return 0; }
int trackInitCollisionBuffers(){ trace("trackInitCollisionBuffers"); return 0; }
int trackIntersect(){ trace("trackIntersect"); return 0; }
int tvInit(){ trace("tvInit"); return 0; }
int uiDll_runFrameEndAndLoadNext(){ trace("uiDll_runFrameEndAndLoadNext"); return 0; }
int uiDll_runFrameStartAndLoadNext(){ trace("uiDll_runFrameStartAndLoadNext"); return 0; }
int unloadMap(){ trace("unloadMap"); return 0; }
int updateEnvironment(){ trace("updateEnvironment"); return 0; }
int voxmaps_updateTimers(){ trace("voxmaps_updateTimers"); return 0; }
int waterFxInit(){ trace("waterFxInit"); return 0; }
int waterFxUpdate(){ trace("waterFxUpdate"); return 0; }
int mmSetForceHeap3Only(){ trace("mmSetForceHeap3Only"); return 0; }
}

