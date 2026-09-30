#include "mf.h"

// Every enemy the nemesis/bounty systems can field. Difficulty is a 1..10 rating
// used for rewards, stat budgets and "who wins" fights in the world simulation.
// Enemies that can steal equipment (Like Like, Takkuri), grab and warp you
// (Wallmaster) or cause real explosions (Real Bombchu) are deliberately left out:
// they could cost you randomizer items or blow open bombable walls.
const Species gSpecies[] = {
    // name              actor                 object               params            dif flags                    yOff
    { "Deku Baba",       ACTOR_EN_DEKUBABA,    OBJECT_DEKUBABA,     0,                1, SPF_STATIONARY,          0 },
    { "Stalchild",       ACTOR_EN_SKB,         OBJECT_SKB,          0,                1, SPF_ROAM | SPF_NIGHT,     0 },
    { "Chuchu",          ACTOR_EN_SLIME,       OBJECT_SLIME,        0,                1, SPF_ROAM,                 0 },
    { "Keese",           ACTOR_EN_FIREFLY,     OBJECT_FIREFLY,      2,                1, SPF_ROAM | SPF_FLY,       60 },
    { "Guay",            ACTOR_EN_CROW,        OBJECT_CROW,         0,                1, SPF_ROAM | SPF_FLY,       80 },
    { "Mad Scrub",       ACTOR_EN_DEKUNUTS,    OBJECT_DEKUNUTS,     0,                2, SPF_STATIONARY,          0 },
    { "Leever",          ACTOR_EN_NEO_REEBA,   OBJECT_RB,           0x8000,           2, SPF_ROAM,                 0 },
    { "Fire Keese",      ACTOR_EN_FIREFLY,     OBJECT_FIREFLY,      0,                2, SPF_ROAM | SPF_FLY,       60 },
    { "Black Boe",       ACTOR_EN_MKK,         OBJECT_MKK,          0,                2, SPF_ROAM | SPF_NIGHT,     0 },
    { "Tektite",         ACTOR_EN_TITE,        OBJECT_TITE,         -1,               2, SPF_ROAM,                 0 },
    { "Snapper",         ACTOR_EN_KAME,        OBJECT_TL,           0,                3, SPF_ROAM,                 0 },
    { "Dragonfly",       ACTOR_EN_GRASSHOPPER, OBJECT_GRASSHOPPER,  0,                3, SPF_ROAM | SPF_FLY,       60 },
    { "Eeno",            ACTOR_EN_SNOWMAN,     OBJECT_SNOWMAN,      1,                3, SPF_ROAM,                 0 },
    { "Poe",             ACTOR_EN_POH,         OBJECT_PO,           0,                3, SPF_ROAM | SPF_FLY | SPF_NIGHT, 40 },
    { "Bubble",          ACTOR_EN_BB,          OBJECT_BB,           (s16)0xFF00,      3, SPF_ROAM | SPF_FLY,       60 },
    { "Freezard",        ACTOR_EN_FZ,          OBJECT_FZ,           0,                3, SPF_STATIONARY,          0 },
    { "Armos",           ACTOR_EN_AM,          OBJECT_AM,           0,                4, SPF_ROAM,                 0 },
    { "Dodongo",         ACTOR_EN_DODONGO,     OBJECT_DODONGO,      0,                4, SPF_ROAM,                 0 },
    { "Floormaster",     ACTOR_EN_FLOORMAS,    OBJECT_WALLMASTER,   0,                4, SPF_ROAM,                 0 },
    { "Hiploop",         ACTOR_EN_PP,          OBJECT_PP,           0,                4, SPF_ROAM,                 0 },
    { "Wolfos",          ACTOR_EN_WF,          OBJECT_WF,           0,                5, SPF_ROAM | SPF_NIGHT,     0 },
    { "ReDead",          ACTOR_EN_RD,          OBJECT_RD,           1,                5, SPF_ROAM | SPF_NIGHT,     0 },
    { "Peahat",          ACTOR_EN_PEEHAT,      OBJECT_PH,           0,                6, SPF_ROAM,                 0 },
    { "White Wolfos",    ACTOR_EN_WF,          OBJECT_WF,           1,                6, SPF_ROAM,                 0 },
    { "Garo",            ACTOR_EN_JSO,         OBJECT_JSO,          0,                6, SPF_ROAM | SPF_NIGHT,     0 },
    { "Dinolfos",        ACTOR_EN_DINOFOS,     OBJECT_DINOFOS,      0,                7, SPF_ROAM,                 0 },
    { "Iron Knuckle",    ACTOR_EN_IK,          OBJECT_IK,           1,                9, SPF_ROAM,                 0 },
};

const s32 gSpeciesCount = ARRAY_LEN(gSpecies);

s32 Roster_FindByActor(s16 actorId, s16 params) {
    s32 i;
    s32 fallback = -1;

    for (i = 0; i < gSpeciesCount; i++) {
        if (gSpecies[i].actorId == actorId) {
            if (gSpecies[i].params == params) {
                return i;
            }
            if (fallback < 0) {
                fallback = i;
            }
        }
    }
    return fallback;
}

s32 Roster_Random(s32 minDifficulty, s32 maxDifficulty) {
    s32 candidates[32];
    s32 n = 0;
    s32 i;

    for (i = 0; i < gSpeciesCount; i++) {
        if (gSpecies[i].difficulty >= minDifficulty && gSpecies[i].difficulty <= maxDifficulty) {
            candidates[n++] = i;
        }
    }
    if (n == 0) {
        return 1;
    }
    return candidates[Rng_Range(0, n - 1)];
}

s32 Roster_RandomRoaming(s32 maxDifficulty) {
    s32 candidates[32];
    s32 n = 0;
    s32 i;

    for (i = 0; i < gSpeciesCount; i++) {
        if ((gSpecies[i].flags & SPF_ROAM) && gSpecies[i].difficulty <= maxDifficulty) {
            candidates[n++] = i;
        }
    }
    if (n == 0) {
        return 1;
    }
    return candidates[Rng_Range(0, n - 1)];
}
