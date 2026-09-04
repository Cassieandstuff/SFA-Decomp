// game_player.c - Phase A vertical-slice harness for the real player DLL
// (src/dlls/objects/195_Player/player.c + the engine/15 motion-control interface).
//
// The game dispatches the player specially: Obj_UpdateAllObjects hard-cases romDefNo
// SABRE/KRYSTAL to playerUpdate(obj) (object.c). The port doesn't run Obj_UpdateAllObjects
// live yet, so this harness stands in - it calls the REAL playerUpdate(gPlayerObj) once per
// frame (like game_sky drives skyUpdateTimeOfDay), feeding real pad input, so the recompiled
// player state machine runs and drives the object's motion + animation. Gated on
// STAIRFAX_PLAYER_DLL so the interim hand-driven path stays the default until this is proven.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern unsigned char* gPlayerObj;    // spawned Sabre/Krystal GameObject (game_scene.cpp)
extern float timeDelta;              // per-frame time (frames); the port never set it -> 0 -> the
extern unsigned char framesThisStep, framesThisStepUnclamped;  // motion integration divided by 0 -> NaN pos
extern void playerUpdate(void* obj);          // real player DLL entry (player.c)
extern void objLoadPlayerFromSave(void* obj); // real player init (player.c) - Obj_RunInitCallback's
                                              // SABRE/KRYSTAL case; populates PlayerState from the save

// --- Player data-table reconstruction --------------------------------------------------------
// player.c reads its move/param tables as `lbl_80332EC0 + offset` (up to ~+0x1c00): in the retail
// player DLL those are ONE contiguous 0x2028-byte .data blob, which the decomp split into ~17 named
// sub-globals whose retail addresses/offsets we recovered from the DLL object (dtk `dol split` of
// main.dol -> build/GSAE01_rev1/obj/.../player.o, .data section). MSVC scatters those globals, so
// `base + offset` reads garbage. lbl_80332EC0 is enlarged to the full 0x2028 span (player.c); here
// we lay the real sub-tables back into it at their retail offsets before the player runs. Values
// come from player.c's own (already host-endian) definitions - no byte-swap needed. Offsets are
// DLL-internal and rev-independent. Runtime-written regions (gPlayerSpawnedObjects @0x14) stay 0.
extern int lbl_80332EC0[];   // the 0x2028-byte destination table (player.c)
extern unsigned char lbl_80332EF0[], lbl_80332F2C[], lbl_80332F48[], lbl_80332F78[], lbl_80332F88[];
extern unsigned char gPlayerAnimSpeedThresholds[], gPlayerMoveTableA[], lbl_80333110[];
extern unsigned char gPlayerMoveTableB[], gPlayerSurfacePfxModeTable[], gPlayerDefaultMoveParams[];
extern unsigned char gPlayerMotionTuning[], lbl_8033366C[], lbl_8033369C[], gPlayerMoveSlotTable[];
extern unsigned char gPlayerMoveSlotData[], gPlayerSpellGameBits[];

static const struct { unsigned off, size; const unsigned char* src; } gPlayerTableParts[] = {
    { 0x0030, 0x3C,   lbl_80332EF0 },
    { 0x006C, 0x1C,   lbl_80332F2C },
    { 0x0088, 0x30,   lbl_80332F48 },
    { 0x00B8, 0x10,   lbl_80332F78 },
    { 0x00C8, 0x38,   lbl_80332F88 },
    { 0x0100, 0x90,   gPlayerAnimSpeedThresholds },
    { 0x0190, 0xC0,   gPlayerMoveTableA },
    { 0x0250, 0x100,  lbl_80333110 },
    { 0x0350, 0x1C,   gPlayerMoveTableB },
    { 0x036C, 0x24,   gPlayerSurfacePfxModeTable },
    { 0x0390, 0x60,   gPlayerDefaultMoveParams },
    { 0x03F0, 0x3BC,  gPlayerMotionTuning },
    { 0x07AC, 0x30,   lbl_8033366C },
    { 0x07DC, 0x20,   lbl_8033369C },
    { 0x07FC, 0x58,   gPlayerMoveSlotTable },
    { 0x0854, 0x1340, gPlayerMoveSlotData },
    { 0x1B94, 0x68,   gPlayerSpellGameBits },
};

static void stairfax_player_build_table(void) {
    unsigned char* base = (unsigned char*)lbl_80332EC0;
    unsigned i;
    for (i = 0; i < sizeof(gPlayerTableParts)/sizeof(gPlayerTableParts[0]); ++i)
        memcpy(base + gPlayerTableParts[i].off, gPlayerTableParts[i].src, gPlayerTableParts[i].size);
}

static int gEnabled = -1;

int stairfax_player_dll_enabled(void) {
    if (gEnabled < 0) gEnabled = getenv("STAIRFAX_PLAYER_DLL") ? 1 : 0;
    return gEnabled;
}

// Run one frame of the real player logic over the spawned character.
void stairfax_player_dll_tick(void) {
    if (!stairfax_player_dll_enabled() || !gPlayerObj) return;

    // One-time init: the game runs Obj_RunInitCallback on spawn, which for the player calls
    // objLoadPlayerFromSave to populate PlayerState (move tables, param curves, yaw, timers,
    // sub-interface init). The port's spawn path doesn't run that callback, so do it once here
    // before the first playerUpdate - without it the state is raw and the update null-derefs.
    static int inited = 0;
    int tr = getenv("STAIRFAX_PLAYER_DLL_TRACE") ? 1 : 0;
    if (!inited) {
        inited = 1;
        // Flat-ground first pass: the ground level for the collision stubs is the player's spawn Y
        // (she's spawned standing on the terrain). Captured before the update so she doesn't fall.
        extern float gStairfaxGroundY;
        gStairfaxGroundY = *(float*)(gPlayerObj + 0x10);   // spawn localPosY
        // Zero the PlayerState (obj->extra, dllStateSize 0x8e0). loadCharacter's alloc doesn't
        // guarantee zeroed dll-state, so fields the init doesn't explicitly set (e.g. focusObject)
        // are garbage -> a non-null focusObject makes the vehicle-sync path deref junk. A freshly
        // spawned player has zeroed state.
        { unsigned char* st = *(unsigned char**)(gPlayerObj + 0xB8); if (st) memset(st, 0, 0x8e0); }
        stairfax_player_build_table();   // lay the real move/param tables into lbl_80332EC0 first
        if (tr) { fprintf(stderr, "[player-dll] init: calling objLoadPlayerFromSave obj=%p\n", (void*)gPlayerObj); fflush(stderr); }
        objLoadPlayerFromSave(gPlayerObj);
        if (tr) {
            unsigned char* o = gPlayerObj;
            fprintf(stderr, "[player-dll] init done; pos=(%.1f,%.1f,%.1f)\n",
                    *(float*)(o+0x0C), *(float*)(o+0x10), *(float*)(o+0x14)); fflush(stderr);
        }
    }

    // SFA measures time in FRAMES: a normal frame is timeDelta=1.0, framesThisStep=1. The port
    // never sets these (they stay 0), and the player's motion integration divides by them -> NaN.
    timeDelta = 1.0f; framesThisStep = 1; framesThisStepUnclamped = 1;

    if (tr) { static int q=0; if(q++<3){ fprintf(stderr,"[player-dll] calling playerUpdate\n"); fflush(stderr);} }
    playerUpdate(gPlayerObj);

    // Hard-floor clamp (Phase C first pass): the character spawns airborne and her spawn state does
    // only horizontal wall-sweeps, never a downward ground probe, so engine/15 gravity would drop her
    // through the world. Until she's driven into a proper grounded state (or real per-triangle terrain
    // collision is wired), hold her at/above the flat ground plane so she stands. localPosY@0x10,
    // worldPosY@0x1C, velocityY@0x28.
    {
        extern float gStairfaxGroundY;
        unsigned char* o = gPlayerObj;
        if (*(float*)(o + 0x10) < gStairfaxGroundY) {
            *(float*)(o + 0x10) = gStairfaxGroundY;   // localPosY
            *(float*)(o + 0x1C) = gStairfaxGroundY;   // worldPosY
            if (*(float*)(o + 0x28) < 0.0f) *(float*)(o + 0x28) = 0.0f;   // velocityY: stop falling
        }
    }

    if (getenv("STAIRFAX_PLAYER_DLL_TRACE")) {
        static int f = 0;
        if ((f++ % 60) == 0) {
            unsigned char* o = gPlayerObj;
            float px = *(float*)(o + 0x0C), py = *(float*)(o + 0x10), pz = *(float*)(o + 0x14);
            short yaw = *(short*)(o + 0x00);
            unsigned char* inner = *(unsigned char**)(o + 0xB8);   // PlayerState (obj->extra)
            unsigned flags360 = inner ? *(unsigned*)(inner + 0x360) : 0;
            float speed = inner ? *(float*)(inner + 0x470) : 0.0f;  // currentSpeed region
            float vx=*(float*)(o+0x24), vy=*(float*)(o+0x28), vz=*(float*)(o+0x2C);
            short cmode = inner?*(short*)(inner+0x274):0, stateId = inner?*(short*)(inner+0x278):0;
            unsigned f0 = inner?*(unsigned*)(inner+0x25C):0;   // baddie.flags0 region (gravity gate)
            fprintf(stderr, "[player-dll] f=%d local=(%.1f,%.1f,%.1f) vel=(%.2f,%.2f,%.2f) ctrlMode=%d stateId=%d flags360=0x%x\n",
                    f, px, py, pz, vx, vy, vz, cmode, stateId, flags360);
            (void)f0;
        }
    }
}
