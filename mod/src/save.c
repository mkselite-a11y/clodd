#include "mf.h"

// The mod keeps its own save next to the game's save file, one per file slot:
//   <game save path>.moonfall<slot>.bin
// It is written by the native library, so nothing is stored inside the game's
// save data and the randomizer's save format is never touched.

SaveData gMf;

#define MF_MAGIC 0x4D464D48 // "MFMH"
#define SAVE_WORDS ((sizeof(SaveData) + 3) / 4)

static u8 sDirty = false;
static s32 sDirtyFrames = 0;
static s32 sAutoFrames = 0;
static s8 sLoadedSlot = -1;
static char sPath[260];

void Save_Defaults(void) {
    u32 seed = gMf.rngState;
    s32 i;
    u8* p = (u8*)&gMf;

    for (i = 0; i < (s32)sizeof(SaveData); i++) {
        p[i] = 0;
    }
    gMf.magic = MF_MAGIC;
    gMf.version = MF_SAVE_VERSION;
    gMf.rngState = (seed != 0) ? seed : (0xA5A5F00Du ^ (u32)osGetTime());
    gMf.curseId = 0xFFFF;
    gMf.curseCycle = 0xFFFF;
    gMf.moonMarks = 10; // a small starting purse so the shop is not empty-handed

    gMf.settings.eventsEnabled = true;
    gMf.settings.nemesisEnabled = true;
    gMf.settings.bountiesEnabled = true;
    gMf.settings.friendsAllowed = true;
    gMf.settings.eventIntervalSecs = 180;
    gMf.settings.eventJitterSecs = 30;
    gMf.settings.eventMinSecs = 30;
    gMf.settings.eventMaxSecs = 45;
    gMf.settings.promotionChance = 60;
    gMf.settings.allowDungeons = true;
    gMf.settings.crossZoneObjects = true;
    gMf.settings.objectBudgetKB = 256;
    gMf.settings.pad[0] = 2;
    gMf.settings.hudBanners = true;
    gMf.settings.friendHurtful = true;
    gMf.settings.friendCooldownSecs = 20;
    gMf.settings.difficultyPct = 100;
    gMf.settings.notifyRumors = true;

    Events_ResetSettings();
    Bounty_Refresh(false);
    World_Log("The moon glares down at Termina.");
    sDirty = true;
}

static void BuildPath(void) {
    unsigned char* base = recomp_get_save_file_path();
    sPath[0] = '\0';
    if (base != NULL) {
        Str_Copy(sPath, (const char*)base, sizeof(sPath) - 24);
        recomp_free(base);
    }
    if (sPath[0] == '\0') {
        Str_Copy(sPath, "moonfall_mayhem", sizeof(sPath));
    }
    Str_Cat(sPath, ".moonfall", sizeof(sPath));
    Str_CatInt(sPath, gSaveContext.fileNum, sizeof(sPath));
    Str_Cat(sPath, ".bin", sizeof(sPath));
}

void Save_LoadForCurrentFile(void) {
    s32 ok;

    BuildPath();
    ok = mfn_load_blob(sPath, &gMf, SAVE_WORDS);
    if (!ok || gMf.magic != MF_MAGIC || gMf.version != MF_SAVE_VERSION) {
        Save_Defaults();
        Save_FlushNow();
    }
    // Settings revision 2: events 1, 13, 18 and 29 were replaced; give them
    // their own defaults, and raise the old small model budget.
    if (gMf.settings.pad[0] < 2) {
        static const u8 replaced[] = { 1, 13, 18, 29 };
        s32 i;
        for (i = 0; i < (s32)sizeof(replaced); i++) {
            EventSettings* es = &gMf.events[replaced[i]];
            es->enabled = true;
            es->weight = 5;
            es->p[0] = gEvents[replaced[i]].defaults[0];
            es->p[1] = gEvents[replaced[i]].defaults[1];
            es->p[2] = gEvents[replaced[i]].defaults[2];
        }
        gMf.settings.objectBudgetKB = MF_MAX(gMf.settings.objectBudgetKB, 256);
        gMf.settings.pad[0] = 2;
        Save_MarkDirty();
    }
    sLoadedSlot = gSaveContext.fileNum;
    gRt.loaded = true;
}

void Save_MarkDirty(void) {
    if (!sDirty) {
        sDirtyFrames = 0;
    }
    sDirty = true;
}

void Save_FlushNow(void) {
    if (sPath[0] == '\0') {
        BuildPath();
    }
    mfn_save_blob(sPath, &gMf, SAVE_WORDS);
    sDirty = false;
    sDirtyFrames = 0;
    sAutoFrames = 0;
}

void Save_Update(PlayState* play) {
    if (!gRt.loaded || sLoadedSlot != gSaveContext.fileNum) {
        Save_LoadForCurrentFile();
        return;
    }
    sAutoFrames++;
    if (sDirty) {
        sDirtyFrames++;
        // Debounce: write a few seconds after the last change, or at least once a minute.
        if (sDirtyFrames > SEC(4) || sAutoFrames > SEC(60)) {
            Save_FlushNow();
        }
    }
}

// Keep our file in step with the game's own saves.
RECOMP_CALLBACK("*", recomp_after_load_save) void Mf_AfterLoadSave(void* fileSelect, void* sramCtx) {
    gRt.loaded = false;
}

RECOMP_CALLBACK("*", recomp_after_owl_save) void Mf_AfterOwlSave(void* owl, PlayState* play) {
    Save_FlushNow();
}

RECOMP_CALLBACK("*", recomp_after_autosave) void Mf_AfterAutosave(PlayState* play) {
    Save_FlushNow();
}
