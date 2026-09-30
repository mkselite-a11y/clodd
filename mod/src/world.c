#include "mf.h"

// The overworld of Termina as a graph. Nemeses and bounty targets live on this
// graph and travel along its edges while the player is elsewhere.
#define L(...) { __VA_ARGS__, 0xFF }

const Zone gZones[] = {
    /*  0 */ { "South Clock Town",   { SCENE_CLOCKTOWER, -1 },            L(1, 2, 3, 4, 5), 0 },
    /*  1 */ { "East Clock Town",    { SCENE_TOWN, -1 },                  L(0, 3, 5), 0 },
    /*  2 */ { "West Clock Town",    { SCENE_ICHIBA, -1 },                L(0, 5), 0 },
    /*  3 */ { "North Clock Town",   { SCENE_BACKTOWN, -1 },              L(0, 1, 5), 0 },
    /*  4 */ { "Laundry Pool",       { SCENE_ALLEY, -1 },                 L(0), 0 },
    /*  5 */ { "Termina Field",      { SCENE_00KEIKOKU, -1 },             L(0, 1, 2, 3, 6, 11, 17, 19, 23), 2 },
    /*  6 */ { "Road to the Swamp",  { SCENE_24KEMONOMITI, -1 },          L(5, 7), 2 },
    /*  7 */ { "Southern Swamp",     { SCENE_20SICHITAI, SCENE_20SICHITAI2 }, L(6, 8, 9, 10), 3 },
    /*  8 */ { "Deku Palace",        { SCENE_22DEKUCITY, -1 },            L(7), 1 },
    /*  9 */ { "Woodfall",           { SCENE_21MITURINMAE, -1 },          L(7), 3 },
    /* 10 */ { "Woods of Mystery",   { SCENE_26SARUNOMORI, -1 },          L(7), 2 },
    /* 11 */ { "Path to the Mountain", { SCENE_13HUBUKINOMITI, -1 },      L(5, 12), 3 },
    /* 12 */ { "Mountain Village",   { SCENE_10YUKIYAMANOMURA, SCENE_10YUKIYAMANOMURA2 }, L(11, 13, 15), 2 },
    /* 13 */ { "Twin Islands",       { SCENE_17SETUGEN, SCENE_17SETUGEN2 }, L(12, 14), 3 },
    /* 14 */ { "Goron Village",      { SCENE_11GORONNOSATO, SCENE_11GORONNOSATO2 }, L(13), 1 },
    /* 15 */ { "Path to Snowhead",   { SCENE_14YUKIDAMANOMITI, -1 },      L(12, 16), 4 },
    /* 16 */ { "Snowhead",           { SCENE_12HAKUGINMAE, -1 },          L(15), 4 },
    /* 17 */ { "Milk Road",          { SCENE_ROMANYMAE, -1 },             L(5, 18), 2 },
    /* 18 */ { "Romani Ranch",       { SCENE_F01, -1 },                   L(17), 1 },
    /* 19 */ { "Great Bay Coast",    { SCENE_30GYOSON, -1 },              L(5, 20, 21), 3 },
    /* 20 */ { "Zora Cape",          { SCENE_31MISAKI, -1 },              L(19, 22), 3 },
    /* 21 */ { "Pirates' Fortress",  { SCENE_TORIDE, -1 },                L(19), 4 },
    /* 22 */ { "Zora Hall",          { SCENE_33ZORACITY, -1 },            L(20), 0 },
    /* 23 */ { "Road to Ikana",      { SCENE_IKANAMAE, -1 },              L(5, 24, 25), 3 },
    /* 24 */ { "Ikana Graveyard",    { SCENE_BOTI, -1 },                  L(23), 4 },
    /* 25 */ { "Ikana Canyon",       { SCENE_IKANA, -1 },                 L(23, 26), 5 },
    /* 26 */ { "Stone Tower",        { SCENE_F40, SCENE_F41 },            L(25), 5 },
};

const s32 gZoneCount = ARRAY_LEN(gZones);

s32 World_ZoneForScene(s16 sceneId) {
    s32 i;
    for (i = 0; i < gZoneCount; i++) {
        if (gZones[i].scenes[0] == sceneId || gZones[i].scenes[1] == sceneId) {
            return i;
        }
    }
    return ZONE_NONE;
}

// Breadth-first search; returns the parent array filled for paths starting at `from`.
static void Bfs(s32 from, u8* parent, u8* dist) {
    u8 queue[32];
    s32 head = 0;
    s32 tail = 0;
    s32 i;

    for (i = 0; i < gZoneCount; i++) {
        parent[i] = 0xFF;
        dist[i] = 0xFF;
    }
    dist[from] = 0;
    queue[tail++] = from;
    while (head < tail) {
        s32 z = queue[head++];
        for (i = 0; i < 10 && gZones[z].links[i] != 0xFF; i++) {
            s32 n = gZones[z].links[i];
            if (dist[n] == 0xFF) {
                dist[n] = dist[z] + 1;
                parent[n] = z;
                queue[tail++] = n;
            }
        }
    }
}

s32 World_NextStepToward(s32 from, s32 to) {
    u8 parent[32];
    u8 dist[32];
    s32 cur = to;

    if (from == to || from >= gZoneCount || to >= gZoneCount) {
        return from;
    }
    Bfs(from, parent, dist);
    if (dist[to] == 0xFF) {
        return from;
    }
    while (parent[cur] != from && parent[cur] != 0xFF) {
        cur = parent[cur];
    }
    return cur;
}

s32 World_Distance(s32 from, s32 to) {
    u8 parent[32];
    u8 dist[32];
    if (from >= gZoneCount || to >= gZoneCount) {
        return 99;
    }
    Bfs(from, parent, dist);
    return dist[to];
}

s32 World_RandomNeighbour(s32 zone) {
    s32 n = 0;
    while (n < 10 && gZones[zone].links[n] != 0xFF) {
        n++;
    }
    if (n == 0) {
        return zone;
    }
    return gZones[zone].links[Rng_Range(0, n - 1)];
}

// ---------------------------------------------------------------------------
// Chronicle: a rolling log of what happened in the world
// ---------------------------------------------------------------------------

// Only things that happen "elsewhere" (world ticks) pop up as rumors; the rest
// goes quietly into the chronicle.
static u8 sRumorMode = false;

void World_Log(const char* text) {
    s32 slot = gMf.logHead;
    Str_Copy(gMf.log[slot], text, LOG_WIDTH);
    gMf.logHead = (gMf.logHead + 1) % LOG_LINES;
    if (gMf.logCount < LOG_LINES) {
        gMf.logCount++;
    }
    if (gMf.settings.notifyRumors && sRumorMode) {
        MfColor c = { 200, 180, 255, 255 };
        Hud_Notify(text, c);
    }
    Save_MarkDirty();
}

void World_Logf2(const char* a, const char* b, const char* c) {
    char buf[LOG_WIDTH];
    buf[0] = '\0';
    Str_Cat(buf, a, LOG_WIDTH);
    if (b != NULL) {
        Str_Cat(buf, b, LOG_WIDTH);
    }
    if (c != NULL) {
        Str_Cat(buf, c, LOG_WIDTH);
    }
    World_Log(buf);
}

// Called about every 45 seconds of real gameplay: moves the world forward.
void World_Tick(PlayState* play) {
    sRumorMode = true;
    if (gMf.settings.nemesisEnabled) {
        Nemesis_WorldTick(play);
    }
    if (gMf.settings.bountiesEnabled) {
        Bounty_WorldTick(play);
    }
    sRumorMode = false;
}
