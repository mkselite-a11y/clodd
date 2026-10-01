// ---------------------------------------------------------------------------
// Friend Link (2.4): your friend's web panel sends commands through a relay;
// GlobalEventsRemote.dll (next to the mod) does the networking on its own
// thread. Here we run the commands and report what's going on. Everything a
// command does goes through the mod's own systems: no items, no flags.
// Included after ev_prey.inc.
// ---------------------------------------------------------------------------

RECOMP_IMPORT(".", s32 ger_init(void));
RECOMP_IMPORT(".", s32 ger_state(void));
RECOMP_IMPORT(".", s32 ger_poll(char* out, u32 max));
RECOMP_IMPORT(".", void ger_status(const char* text, u32 len));

static s32 sLinkInit = false;
static s32 sLinkReady = false;
static s32 sLinkTimer = 0;
static s32 sLinkGap = 0;
static char sLinkLast[48] = "";
static char sLinkToast[48];
static char sLinkStatus[4096];
s32 gFriendLinkSize = 0;          // LinkSizeMode while a friend's size change lasts
static s32 sFriendSizeTimer = 0;
static s32 sFakeTimer = 0;        // Fake Out: the "...or not." follow-up
static s32 sNemHuntTimer = 0;     // "send it to them" is still trying to find room
static s32 sLinkCurId = 0;        // id of the command being run
static s32 sRefunds[8];           // ids of commands that couldn't run (the relay refunds them)
static s32 sRefundCount = 0;


// Soundboard (panel order)
static const u16 sPrankSfx[] = {
    NA_SE_SY_GET_ITEM,     NA_SE_SY_PIECE_OF_HEART, NA_SE_SY_TRE_BOX_APPEAR, NA_SE_EN_REDEAD_CRY,
    NA_SE_EN_WIZ_LAUGH,    NA_SE_EV_CHICKEN_CRY_M,  NA_SE_EV_COW_CRY,        NA_SE_VO_NAVY_HELLO,
    NA_SE_SY_TIMESIGNAL_BELL, NA_SE_IT_BOMB_EXPLOSION,
};

// Our Poes never leave a soul to bottle (that would hand out a real item).
// Both kinds move to the MISC list when they turn into a soul; the Big Poe
// timer is a backup in case that ever comes late.
static void Ev_NoPoeSouls(PlayState* play, Actor* actor) {
    TagData* t;

    if ((actor->id != ACTOR_EN_POH) && (actor->id != ACTOR_EN_BIGPO)) {
        return;
    }
    t = Tag_Get(actor);
    if ((t == NULL) || (t->tag == TAG_NONE) || (t->tag == TAG_KEEP)) {
        return;
    }
    if (actor->category == ACTORCAT_MISC) {
        Ev_Poof(play, &actor->world.pos, 1);
        Actor_Kill(actor);
        return;
    }
    if ((actor->id == ACTOR_EN_BIGPO) && (actor->colChkInfo.health == 0)) {
        if (!(t->v2flags & TAGF_V2_DYING)) {
            t->v2flags |= TAGF_V2_DYING;
            t->v2timer = 0;
        } else if (++t->v2timer > 36) {
            Ev_Poof(play, &actor->world.pos, 1);
            Actor_Kill(actor);
        }
    }
}

static const char* Link_StateDesc(void) {
    switch (sLinkInit ? ger_state() : 0) {
        case 0: return "Fill in the .txt next to the mod.";
        case 1: return "Reaching the relay...";
        case 2: return "Connected. Your friend can see you.";
        case 3: return "The relay says the room key is wrong.";
        case 5: return "The url in the .txt looks wrong.";
        default: return "Can't reach the relay. Check the url.";
    }
}

static const char* Link_StateValue(char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    static const char* sWords[6] = { "Not set up", "Connecting", "Connected", "Wrong key", "Offline", "Bad url" };
    s32 st = sLinkInit ? ger_state() : 0;

    st = CLAMP(st, 0, 5);
    Ev_Append(out, 0, sWords[st], maxLen);
    if (st == 2) {
        *r = 110;
        *b = 140;
    } else if (st == 0) {
        *r = *g = *b = 150;
    } else {
        *g = 120;
        *b = 120;
    }
    return out;
}

static char sLinkFrom[8] = "";    // name of the friend who sent the command being run
static char sFriendNowFrom[8] = ""; // ...and of the one whose "start now" is waiting

static s32 Link_StartsWith(const char* s, const char* prefix) {
    while (*prefix != '\0') {
        if (*s++ != *prefix++) {
            return false;
        }
    }
    return true;
}

// With several friends, say who did it: "Your friend healed you!" -> "Alex healed you!".
// Builds the text in sLinkToast without showing it.
static void Link_Format(const char* a, const char* b) {
    s32 n = 0;
    const char* rest = a;
    s32 k;

    if (sLinkFrom[0] != '\0') {
        for (k = 0; a[k] != '\0'; k++) {
            if (Link_StartsWith(a + k, "Your friend") || Link_StartsWith(a + k, "your friend")) {
                rest = a + k + 11;
                break;
            }
            if (Link_StartsWith(a + k, "Friend")) {
                rest = a + k + 6;
                break;
            }
        }
        if (rest != a) {
            // Keep what came before the mention, then the name, then the rest.
            for (n = 0; (n < k) && (n < (s32)sizeof(sLinkToast) - 1); n++) {
                sLinkToast[n] = a[n];
            }
            sLinkToast[n] = '\0';
            n = Ev_Append(sLinkToast, n, sLinkFrom, sizeof(sLinkToast));
            n = Ev_Append(sLinkToast, n, rest, sizeof(sLinkToast));
        } else {
            n = Ev_Append(sLinkToast, 0, sLinkFrom, sizeof(sLinkToast));
            n = Ev_Append(sLinkToast, n, ": ", sizeof(sLinkToast));
            n = Ev_Append(sLinkToast, n, a, sizeof(sLinkToast));
        }
    } else {
        n = Ev_Append(sLinkToast, 0, a, sizeof(sLinkToast));
    }

    Ev_Append(sLinkToast, n, b, sizeof(sLinkToast));
}

static void Link_Say(const char* a, const char* b) {
    Link_Format(a, b);
    Menu_ShowToast(sLinkToast);
    Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
}

static void Link_Result(const char* a, const char* b) {
    s32 n = Ev_Append(sLinkLast, 0, a, sizeof(sLinkLast));

    Ev_Append(sLinkLast, n, b, sizeof(sLinkLast));
}

// Ask the relay to give a command's points back.
static void Link_Refund(s32 id) {
    s32 k;

    if (id <= 0) {
        return;
    }
    if (sRefundCount == ARRAY_COUNT(sRefunds)) {
        for (k = 1; k < (s32)ARRAY_COUNT(sRefunds); k++) {
            sRefunds[k - 1] = sRefunds[k];
        }
        sRefundCount--;
    }
    sRefunds[sRefundCount++] = id;
}

// The command couldn't do anything: say why, and ask for the points back.
static void Link_Fail(const char* why, const char* what) {
    s32 n = Ev_Append(sLinkLast, 0, "Failed: ", sizeof(sLinkLast));

    n = Ev_Append(sLinkLast, n, why, sizeof(sLinkLast));
    Ev_Append(sLinkLast, n, what, sizeof(sLinkLast));
    Link_Refund(sLinkCurId);
}

// A queued "start now" that will never happen: give the points back.
static void Link_DropFriendNow(void) {
    if (sFriendNow >= 0) {
        Link_Refund(sFriendNowId);
        Ev_Append(sLinkLast, 0, "Refunded: your queued event couldn't start", sizeof(sLinkLast));
    }
    sFriendNow = -1;
    sFriendNowId = 0;
}

// "id|type|a|b|c|text|from" -> fields
static s32 Link_Parse(char* line, char** f, s32 max) {
    s32 count = 0;
    char* p = line;

    f[count++] = p;
    while ((*p != '\0') && (count < max)) {
        if (*p == '|') {
            *p = '\0';
            f[count++] = p + 1;
        }
        p++;
    }
    return count;
}

static s32 Link_Int(const char* s) {
    s32 v = 0;
    s32 neg = false;

    if (*s == '-') {
        neg = true;
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

static s32 Str_Eq(const char* a, const char* b) {
    while ((*a != '\0') && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

static s32 Link_EventOk(s32 ev) {
    return (ev >= 0) && (ev < EV_COUNT) && !Events_IsHidden(ev);
}

static s32 Link_PoolOk(s32 pool) {
    s32 k;

    for (k = 0; k < (s32)ARRAY_COUNT(sHuntPool); k++) {
        if (sHuntPool[k] == pool) {
            return true;
        }
    }
    return false;
}

// Same rules as the game's own combos, and only with events you have on.
static s32 Link_ComboOk(s32 ev, s32 combo) {
    return Link_EventOk(combo) && (combo != ev) && gOpt[ID_EV_COMBOS] && gOpt[ID_EV_ON_FIRST + combo] &&
           Combo_CanBePrimary(ev) && Combo_CanBeSecond(combo);
}

// What a Friend Shield stops: anything that makes your life harder.
static s32 Link_Harmful(const char* type) {
    static const char* sMean[] = { "ev_now",    "ev_next",   "ev_soon",  "nem_make", "nem_trait", "nem_level",
                                   "nem_hunt",  "draft",     "bounty",   "ambush",   "curse",     "curse_set",
                                   "tax",       "airstrike", "size",     "wanted",   "ev_extend", "empower",
                                   "blackout",  "nem_stat",  "surprise", "place",    "steer" };
    s32 k;

    for (k = 0; k < (s32)ARRAY_COUNT(sMean); k++) {
        if (Str_Eq(type, sMean[k])) {
            return true;
        }
    }
    return false;
}

static s32 Link_TrySpawnNemNear(PlayState* play) {
    s32 zone = V2_ZoneNow(play);

    if (Nem_SpawnEx(play, true) == NULL) {
        return false;
    }
    if (zone >= 0) {
        sNem.zone = zone;
    }
    sNem.migrate = MAX(sNem.migrate, 4 * 60 * FPS);
    return true;
}

// ===========================================================================
// V3 Friend Link: who's on the panel, what each friend may do, steering,
// placements, surprises, the radar and the map the panel draws.
// ===========================================================================

RECOMP_IMPORT(".", s32 ger_friends(char* out, u32 max));
RECOMP_IMPORT(".", void ger_map(const char* text, u32 len));

// --- Friends and their modes -------------------------------------------------

#define FR_MAX 12
enum { FMODE_BLOCKED, FMODE_HELPFUL, FMODE_FULL };

typedef struct {
    char name[6];
    u8 mode;
    u8 online;
} FriendRec;

static FriendRec sFr[FR_MAX];
static s32 sFrCount = 0;
static s32 sFrLoaded = false;

static s32 Fr_Find(const char* name) {
    s32 k;

    for (k = 0; k < sFrCount; k++) {
        if (Str_Eq(sFr[k].name, name)) {
            return k;
        }
    }
    return -1;
}

static void Fr_Save(void) {
    char buf[FR_MAX * 9 + 4];
    s32 n = 0;
    s32 k;

    buf[0] = '\0';
    for (k = 0; k < sFrCount; k++) {
        // Only friends you changed need remembering: everyone else is Full.
        if (sFr[k].mode == FMODE_FULL) {
            continue;
        }
        n = Ev_Append(buf, n, (n > 0) ? "," : "", sizeof(buf));
        n = Ev_Append(buf, n, sFr[k].name, sizeof(buf));
        n = Ev_Append(buf, n, (sFr[k].mode == FMODE_BLOCKED) ? ":0" : ":1", sizeof(buf));
    }
    ger_store("friend_modes", buf);
}

// A friend we haven't seen before (or the slot of one long gone).
static s32 Fr_Add(const char* name) {
    s32 k = Fr_Find(name);
    s32 i;

    if (k >= 0) {
        return k;
    }
    if (sFrCount == FR_MAX) {
        // Full: forget someone offline, preferring friends with no special mode.
        s32 drop = -1;

        for (i = 0; i < sFrCount; i++) {
            if (!sFr[i].online && ((drop < 0) || (sFr[i].mode == FMODE_FULL))) {
                drop = i;
            }
        }
        if (drop < 0) {
            return -1;
        }
        for (i = drop + 1; i < sFrCount; i++) {
            sFr[i - 1] = sFr[i];
        }
        sFrCount--;
    }
    k = sFrCount++;
    for (i = 0; (name[i] != '\0') && (i < 5); i++) {
        sFr[k].name[i] = name[i];
    }
    sFr[k].name[i] = '\0';
    sFr[k].mode = FMODE_FULL;
    sFr[k].online = false;
    return k;
}

static void Fr_Load(void) {
    char buf[FR_MAX * 9 + 4];
    char name[6];
    s32 len;
    s32 i = 0;

    sFrLoaded = true;
    len = ger_fetch("friend_modes", buf, sizeof(buf));
    if (len <= 0) {
        return;
    }
    while (i < len) {
        s32 n = 0;
        s32 k;

        while ((i < len) && (buf[i] != ':') && (buf[i] != ',')) {
            if (n < 5) {
                name[n++] = buf[i];
            }
            i++;
        }
        name[n] = '\0';
        if ((i < len) && (buf[i] == ':') && (n > 0)) {
            k = Fr_Add(name);
            if (k >= 0) {
                sFr[k].mode = (buf[i + 1] == '0') ? FMODE_BLOCKED : FMODE_HELPFUL;
            }
            i += 2;
        }
        while ((i < len) && (buf[i] != ',')) {
            i++;
        }
        i++;
    }
}

// A command from someone with no name (an old panel) counts as Full.
static s32 Fr_Mode(const char* name) {
    s32 k = (name[0] != '\0') ? Fr_Find(name) : -1;

    return (k >= 0) ? sFr[k].mode : FMODE_FULL;
}

static s32 Link_FriendsOnline(void) {
    s32 k;
    s32 n = 0;

    for (k = 0; k < sFrCount; k++) {
        n += sFr[k].online;
    }
    return n;
}

// Menu rows: online friends first, then the ones we remember.
static s32 Fr_Row(s32 row) {
    s32 pass;
    s32 k;

    for (pass = 0; pass < 2; pass++) {
        for (k = 0; k < sFrCount; k++) {
            if ((sFr[k].online == (pass == 0)) && (row-- == 0)) {
                return k;
            }
        }
    }
    return -1;
}

s32 Link_FriendRows(void) {
    return MIN(sFrCount, ID_LNK_FR_LAST - ID_LNK_FR_FIRST + 1);
}

static const char* Link_FriendLabel(s32 row) {
    static char sLabel[24];
    s32 k = Fr_Row(row);
    s32 n;

    if (k < 0) {
        return "";
    }
    n = Ev_Append(sLabel, 0, sFr[k].name, sizeof(sLabel));
    Ev_Append(sLabel, n, sFr[k].online ? " (here)" : "", sizeof(sLabel));
    return sLabel;
}

static const char* Link_FriendValue(s32 row, char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    static const char* sWords[3] = { "Blocked", "Helpful", "Full" };
    s32 k = Fr_Row(row);
    s32 m = (k >= 0) ? sFr[k].mode : FMODE_FULL;

    Ev_Append(out, 0, sWords[m], maxLen);
    if (m == FMODE_BLOCKED) {
        *r = 255, *g = 70, *b = 70;
    } else if (m == FMODE_HELPFUL) {
        *r = 110, *g = 230, *b = 140;
    } else {
        *r = 255, *g = 200, *b = 90;
    }
    return out;
}

static void Link_FriendCycle(s32 row) {
    s32 k = Fr_Row(row);

    if (k < 0) {
        return;
    }
    // Full -> Helpful -> Blocked -> Full
    sFr[k].mode = (sFr[k].mode == FMODE_FULL) ? FMODE_HELPFUL : (sFr[k].mode == FMODE_HELPFUL) ? FMODE_BLOCKED : FMODE_FULL;
    Fr_Save();
}

// "NAME,NAME" from the DLL: who has the panel open. Pop-ups for joins and leaves.
static void Fr_Poll(s32 connected) {
    char buf[96];
    u8 seen[FR_MAX];
    s32 len = connected ? ger_friends(buf, sizeof(buf)) : 0;
    s32 i = 0;
    s32 k;

    for (k = 0; k < FR_MAX; k++) {
        seen[k] = false;
    }
    while (i < len) {
        char name[6];
        s32 n = 0;

        while ((i < len) && (buf[i] != ',')) {
            if (n < 5) {
                name[n++] = buf[i];
            }
            i++;
        }
        i++;
        name[n] = '\0';
        if (n == 0) {
            continue;
        }
        k = Fr_Add(name);
        if (k < 0) {
            continue;
        }
        seen[k] = true;
        if (!sFr[k].online) {
            sFr[k].online = true;
            if (V2_ON(ID_V2_LINK)) {
                s32 t = Ev_Append(sLinkToast, 0, sFr[k].name, sizeof(sLinkToast));

                Ev_Append(sLinkToast, t,
                          (sFr[k].mode == FMODE_BLOCKED)   ? " tried to join (blocked)"
                          : (sFr[k].mode == FMODE_HELPFUL) ? " joined (helpful only)"
                                                           : " joined the Friend Link",
                          sizeof(sLinkToast));
                Menu_Notify(sLinkToast, NOTE_FRIEND);
            }
        }
    }
    for (k = 0; k < sFrCount; k++) {
        if (sFr[k].online && !seen[k]) {
            sFr[k].online = false;
            if (V2_ON(ID_V2_LINK) && (sFr[k].mode != FMODE_BLOCKED)) {
                s32 t = Ev_Append(sLinkToast, 0, sFr[k].name, sizeof(sLinkToast));

                Ev_Append(sLinkToast, t, " left the Friend Link", sizeof(sLinkToast));
                Menu_Notify(sLinkToast, NOTE_FRIEND);
            }
        }
    }
}

// --- Spots a friend picks on the radar -----------------------------------------

// Solid ground under (x, z), searched from well above Link. No water.
static s32 Link_GroundAt(PlayState* play, f32 x, f32 z, Vec3f* out) {
    Player* player = GET_PLAYER(play);
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe;
    f32 y;
    f32 waterY;
    WaterBox* waterBox;
    s32 pass;

    if (player == NULL) {
        return false;
    }
    for (pass = 0; pass < 2; pass++) {
        probe.x = x;
        probe.y = player->actor.world.pos.y + ((pass == 0) ? 400.0f : 1500.0f);
        probe.z = z;
        y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (y <= BGCHECK_Y_MIN) {
            continue;
        }
        if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) && (waterY > y + 10.0f)) {
            return false;
        }
        out->x = x;
        out->y = y;
        out->z = z;
        return true;
    }
    return false;
}

// "x,z" -> numbers. Returns how far it read (0 = nothing usable).
static s32 Link_ParseXZ(const char* s, s32* x, s32* z) {
    s32 i = 0;

    if ((s[0] == '\0') || (s[0] == ';')) {
        return 0;
    }
    *x = Link_Int(s);
    while ((s[i] != '\0') && (s[i] != ',') && (s[i] != ';')) {
        i++;
    }
    if (s[i] != ',') {
        return 0;
    }
    i++;
    *z = Link_Int(s + i);
    while ((s[i] != '\0') && (s[i] != ';')) {
        i++;
    }
    return (s[i] == ';') ? i + 1 : i;
}

// --- Steering ------------------------------------------------------------------

static s32 sSteerTimer = 0; // frames left of the friend's control (it goes back to normal after 5 s idle)
static s32 sSteerEv = -1;
static f32 sSteerX;
static f32 sSteerZ;

// What a friend can steer right now: 0 Moon Tether, 1 Moonfall, 2 Searchlights (-1 nothing).
static s32 Steer_Kind(void) {
    switch (sActive) {
        case EV_TETHER: return 0;
        case EV_MOONFALL: return 1;
        case EV_SEARCH: return 2;
        default: return -1;
    }
}

static s32 Steer_Get(s32 kind, f32* x, f32* z) {
    if ((sSteerTimer <= 0) || (sSteerEv != sActive) || (Steer_Kind() != kind)) {
        return false;
    }
    *x = sSteerX;
    *z = sSteerZ;
    return true;
}

// --- Placements (the first 15 seconds of some events) ---------------------------

#define PLACE_WINDOW (15 * FPS)

static s32 V3_Place(PlayState* play, s32 ev, Vec3f* at, s32 extra);
static s32 V3_PlaceCount(void);

static s32 Place_Allowed(s32 ev) {
    return (ev == EV_TREASURE) || (ev == EV_DOWSING) || (ev == EV_SEALS) || (ev == EV_SCYTHE) ||
           (ev == EV_PROCESSION);
}

static s32 Place_Open(void) {
    return (sActive >= 0) && Place_Allowed(sActive) && (sEventFrames < PLACE_WINDOW);
}

// --- Surprises -------------------------------------------------------------------

#define SURPRISE_MAX 8
enum { SUR_EVENT, SUR_AMBUSH, SUR_BOMB, SUR_RUPOORS, SUR_NEMESIS, SUR_KINDS };

typedef struct {
    s32 id;      // the command (refunded if it fizzles)
    s8 zone;
    u8 kind;
    s16 arg;     // event or enemy
    u8 hasPos;   // the friend picked a spot on the map
    u8 ready;    // placed in the area you're in
    Vec3f pos;
    char from[6];
} Surprise;

static Surprise sSur[SURPRISE_MAX];
static s32 sSurCount = 0;
static s32 sSurScene = -1;

static void Sur_Remove(s32 i) {
    s32 k;

    for (k = i + 1; k < sSurCount; k++) {
        sSur[k - 1] = sSur[k];
    }
    sSurCount--;
}

// Never by a door or a loading zone: pushed away from the nearest one, or a new spot.
static s32 Sur_Settle(PlayState* play, Surprise* s) {
    Vec3f p;

    if (s->hasPos) {
        s32 tries;

        if (!Link_GroundAt(play, s->pos.x, s->pos.z, &p)) {
            return World_PickHome(play, &s->pos);
        }
        for (tries = 0; (tries < 8) && (World_ExitDist(&p) < WORLD_EXIT_CLEAR); tries++) {
            f32 best = 1.0e9f;
            s32 bi = 0;
            s32 i;
            s16 away;

            for (i = 0; i < sEntranceCount; i++) {
                f32 d = Ev_DistXZ(&p, &sEntrances[i]);

                if (d < best) {
                    best = d;
                    bi = i;
                }
            }
            away = Math_Vec3f_Yaw(&sEntrances[bi], &p);
            if (!Link_GroundAt(play, p.x + Math_SinS(away) * 80.0f, p.z + Math_CosS(away) * 80.0f, &p)) {
                break;
            }
        }
        if (World_ExitDist(&p) < WORLD_EXIT_CLEAR) {
            return World_PickHome(play, &s->pos);
        }
        s->pos = p;
        return true;
    }
    return World_PickHome(play, &s->pos);
}

// Sets off a surprise. Returns false if it has to wait (an event is already queued).
static s32 Sur_Fire(PlayState* play, Surprise* s) {
    Player* player = GET_PLAYER(play);
    s32 ok = true;
    s32 i;

    Ev_Append(sLinkFrom, 0, s->from, sizeof(sLinkFrom));
    switch (s->kind) {
        case SUR_EVENT:
            if (!gOpt[ID_EV_MASTER] || !Link_EventOk(s->arg) || !gOpt[ID_EV_ON_FIRST + s->arg] ||
                (sBanMask & (1ull << s->arg))) {
                ok = false;
            } else if ((sActive < 0) && (sRouletteTimer == 0) && Ev_CanRun(play, s->arg)) {
                sStartByFriend = true;
                Ev_Start(play, s->arg);
            } else if (sFriendNow < 0) {
                sFriendNow = s->arg;
                sFriendNowId = 0; // already paid for and set off: nothing to refund now
                Ev_Append(sFriendNowFrom, 0, s->from, sizeof(sFriendNowFrom));
            } else {
                return false;
            }
            break;
        case SUR_AMBUSH: {
            s32 made = 0;

            for (i = 0; i < 3; i++) {
                s16 yaw = (s16)(i * 0x5555 + (s32)(Rand_ZeroOne() * 0x2000));
                Vec3f at;

                if (Link_GroundAt(play, s->pos.x + Math_SinS(yaw) * 160.0f, s->pos.z + Math_CosS(yaw) * 160.0f,
                                  &at) &&
                    (V2_SpawnEnemy(play, s->arg, &at, 0.0f, 0.0f, TAG_V2) != NULL)) {
                    made++;
                } else if (V2_SpawnEnemy(play, s->arg, NULL, 200.0f, 400.0f, TAG_V2) != NULL) {
                    made++;
                }
            }
            ok = made > 0;
            break;
        }
        case SUR_BOMB:
            for (i = 0; i < 6; i++) {
                Vec3f to = s->pos;
                Vec3f from;

                to.x += Rand_CenteredFloat(320.0f);
                to.z += Rand_CenteredFloat(320.0f);
                from = to;
                from.y = Ev_SkyHeight(play, &to, 250.0f);
                Drop_Launch(play, &from, &to, 20 + i * 8);
            }
            break;
        case SUR_RUPOORS:
            for (i = 0; i < 12; i++) {
                Vec3f at = s->pos;

                at.x += Rand_CenteredFloat(360.0f);
                at.z += Rand_CenteredFloat(360.0f);
                at.y = Ev_SkyHeight(play, &s->pos, 250.0f) + Rand_ZeroOne() * 120.0f;
                Rupoor_SpawnAt(play, &at);
            }
            break;
        case SUR_NEMESIS:
            if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
                ok = false;
            } else if ((sNem.actor == NULL) || !V2_IsLive(play, sNem.actor)) {
                ok = Link_TrySpawnNemNear(play);
                if (!ok) {
                    sNemHuntTimer = 15 * FPS;
                    ok = true;
                }
            }
            break;
    }
    if (ok) {
        Link_Format("Surprise from your friend!", "");
        Menu_Notify(sLinkToast, NOTE_FRIEND);
        Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
    } else {
        Link_Refund(s->id);
    }
    (void)player;
    return true;
}

// Every frame while commands can land: settle, shimmer, and spring the trap.
static void Sur_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 zone = V2_ZoneNow(play);
    s32 i;

    if (player == NULL) {
        return;
    }
    if (play->sceneId != sSurScene) {
        sSurScene = play->sceneId;
        for (i = 0; i < sSurCount; i++) {
            sSur[i].ready = false;
        }
    }
    for (i = 0; i < sSurCount; i++) {
        Surprise* s = &sSur[i];

        if ((zone < 0) || (s->zone != zone)) {
            continue;
        }
        if (!s->ready) {
            // Wait until the area's doors are known.
            if ((sAreaTimer > FPS) && ((sAreaTimer % 10) == 0) && Sur_Settle(play, s)) {
                s->ready = true;
            }
            continue;
        }
        // A faint shimmer: easy to miss if you aren't looking.
        if (((play->gameplayFrames % 14) == (u32)(i * 2)) && (Ev_DistXZ(&s->pos, &player->actor.world.pos) < 1500.0f)) {
            Vec3f p = s->pos;
            Vec3f vel = { 0.0f, 0.6f, 0.0f };
            Vec3f acc = { 0.0f, 0.0f, 0.0f };
            Color_RGBA8 prim = { 255, 255, 255, 160 };
            Color_RGBA8 env = { 190, 150, 255, 120 };

            p.x += Rand_CenteredFloat(50.0f);
            p.y += 15.0f + Rand_ZeroOne() * 30.0f;
            p.z += Rand_CenteredFloat(50.0f);
            EffectSsKirakira_SpawnDispersed(play, &p, &vel, &acc, &prim, &env, 1200, 24);
        }
        if ((Ev_DistXZ(&s->pos, &player->actor.world.pos) < 110.0f) &&
            (fabsf(s->pos.y - player->actor.world.pos.y) < 120.0f) && Sur_Fire(play, s)) {
            Sur_Remove(i);
            return;
        }
    }
}

// --- The map: the floor around you, sampled bit by bit ------------------------------
// Blocks of 16x16 cells, 128 units each. A cell is '.' (nothing), '~' (water) or
// a height letter: chr(0x30 + clamp(round((y - base) / 40) + 32, 0, 63)).

#define MAP_CELL 128
#define MAP_BLOCK (16 * MAP_CELL)
#define MAP_SENT_MAX 64
#define MAP_PER_FRAME 12

static u32 sMapSent[MAP_SENT_MAX];
static s32 sMapSentCount = 0;
static s32 sMapScene = -1;
static s32 sMapBx;
static s32 sMapBz;
static s32 sMapIdx = -1; // next cell to sample, -1: idle
static s32 sMapBase;
static char sMapCells[257];

static u32 Map_Key(s32 scene, s32 bx, s32 bz) {
    return ((u32)(scene & 0xFFFF) << 16) | ((u32)(bx & 0xFF) << 8) | (u32)(bz & 0xFF);
}

static s32 Map_WasSent(u32 key) {
    s32 k;

    for (k = 0; k < sMapSentCount; k++) {
        if (sMapSent[k] == key) {
            return true;
        }
    }
    return false;
}

static s32 Map_Floor(f32 v) {
    return (s32)((v >= 0.0f) ? (v / MAP_BLOCK) : (v / MAP_BLOCK - 1.0f));
}

static void LinkMap_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 n;

    if (player == NULL) {
        return;
    }
    if (play->sceneId != sMapScene) {
        sMapScene = play->sceneId;
        sMapIdx = -1;
    }
    if (sMapIdx < 0) {
        // The block you're in first, then the ones around it.
        static const s8 sOrder[9][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                         { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
        s32 bx = Map_Floor(player->actor.world.pos.x);
        s32 bz = Map_Floor(player->actor.world.pos.z);
        s32 k;

        if ((sMapSentCount >= MAP_SENT_MAX) || (sAreaTimer < 2 * FPS) || ((play->gameplayFrames % FPS) != 0)) {
            return;
        }
        for (k = 0; k < 9; k++) {
            if (!Map_WasSent(Map_Key(play->sceneId, bx + sOrder[k][0], bz + sOrder[k][1]))) {
                sMapBx = bx + sOrder[k][0];
                sMapBz = bz + sOrder[k][1];
                sMapBase = (s32)player->actor.world.pos.y;
                sMapIdx = 0;
                break;
            }
        }
        return;
    }
    for (n = 0; (n < MAP_PER_FRAME) && (sMapIdx < 256); n++, sMapIdx++) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f probe;
        f32 y;
        f32 waterY;
        WaterBox* waterBox;
        s32 code;
        char c;

        probe.x = (f32)(sMapBx * MAP_BLOCK + (sMapIdx % 16) * MAP_CELL + MAP_CELL / 2);
        probe.z = (f32)(sMapBz * MAP_BLOCK + (sMapIdx / 16) * MAP_CELL + MAP_CELL / 2);
        probe.y = (f32)(sMapBase + 1300);
        y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (WaterBox_GetSurface1(play, &play->colCtx, probe.x, probe.z, &waterY, &waterBox) &&
            ((y <= BGCHECK_Y_MIN) || (waterY > y + 10.0f))) {
            c = '~';
        } else if (y <= BGCHECK_Y_MIN) {
            c = '.';
        } else {
            f32 rel = (y - sMapBase) / 40.0f;

            code = (s32)((rel >= 0.0f) ? (rel + 0.5f) : (rel - 0.5f)) + 32;
            c = (char)(0x30 + CLAMP(code, 0, 63));
        }
        sMapCells[sMapIdx] = c;
    }
    if (sMapIdx >= 256) {
        static char sMapText[360];
        s32 t;
        s32 k;
        s32 any = false;

        sMapCells[256] = '\0';
        for (k = 0; k < 256; k++) {
            any |= (sMapCells[k] != '.');
        }
        if (any) {
            t = Ev_Append(sMapText, 0, "scene=", sizeof(sMapText));
            t = Ev_AppendNum(sMapText, t, play->sceneId, sizeof(sMapText));
            t = Ev_Append(sMapText, t, "\nbx=", sizeof(sMapText));
            t = Ev_AppendNum(sMapText, t, sMapBx, sizeof(sMapText));
            t = Ev_Append(sMapText, t, "\nbz=", sizeof(sMapText));
            t = Ev_AppendNum(sMapText, t, sMapBz, sizeof(sMapText));
            t = Ev_Append(sMapText, t, "\nbase=", sizeof(sMapText));
            t = Ev_AppendNum(sMapText, t, sMapBase, sizeof(sMapText));
            t = Ev_Append(sMapText, t, "\ncells=", sizeof(sMapText));
            t = Ev_Append(sMapText, t, sMapCells, sizeof(sMapText));
            t = Ev_Append(sMapText, t, "\n", sizeof(sMapText));
            ger_map(sMapText, (u32)t);
        }
        sMapSent[sMapSentCount++] = Map_Key(play->sceneId, sMapBx, sMapBz);
        sMapIdx = -1;
    }
}

// Runs one command. Returns false for the quick ones (steering) that shouldn't hold up the next.
static s32 Link_Run(PlayState* play, char* line) {
    char* f[7] = { "", "", "", "", "", "", "" };
    const char* type;
    s32 a;
    s32 b;
    s32 c;
    const char* text;
    s32 hp = gSaveContext.save.saveInfo.playerData.health;
    s32 cap = gSaveContext.save.saveInfo.playerData.healthCapacity;

    if (Link_Parse(line, f, 7) < 5) {
        return true;
    }
    {
        s32 k;

        for (k = 0; (f[6][k] != '\0') && (k < 5); k++) {
            sLinkFrom[k] = f[6][k];
        }
        sLinkFrom[k] = '\0';
    }
    sLinkCurId = Link_Int(f[0]);
    type = f[1];
    a = Link_Int(f[2]);
    b = Link_Int(f[3]);
    c = Link_Int(f[4]);
    text = f[5];

    // Each friend's mode (Friends tab): Blocked friends get nothing through,
    // Helpful ones only the nice things.
    if (Fr_Mode(sLinkFrom) == FMODE_BLOCKED) {
        Link_Refund(sLinkCurId);
        return !Str_Eq(type, "steer");
    }
    if ((Fr_Mode(sLinkFrom) == FMODE_HELPFUL) && Link_Harmful(type)) {
        Link_Fail(sLinkFrom, " can only help");
        return !Str_Eq(type, "steer");
    }
    // Steering: the friend holds the event's aim while they keep moving it.
    if (Str_Eq(type, "steer")) {
        if ((Steer_Kind() == a) && (sActive >= 0)) {
            sSteerEv = sActive;
            sSteerX = (f32)b;
            sSteerZ = (f32)c;
            sSteerTimer = 5 * FPS;
        }
        return false;
    }

    // Friend Shield (Moon Service) eats the next mean move.
    if ((sFriendShields > 0) && Link_Harmful(type)) {
        sFriendShields--;
        Link_Result("Blocked by their Friend Shield", "");
        if (sLinkFrom[0] != '\0') {
            s32 n = Ev_Append(sLinkToast, 0, "Friend Shield blocked ", sizeof(sLinkToast));

            n = Ev_Append(sLinkToast, n, sLinkFrom, sizeof(sLinkToast));
            Ev_Append(sLinkToast, n, "!", sizeof(sLinkToast));
            Menu_ShowToast(sLinkToast);
        } else {
            Menu_ShowToast("Friend Shield blocked your friend!");
        }
        Audio_PlaySfx(NA_SE_IT_SHIELD_REFLECT_SW);
        return true;
    }

    if (Str_Eq(type, "ev_now") && Link_EventOk(a)) {
        if (!gOpt[ID_EV_MASTER]) {
            Link_Fail("events are off", "");
        } else if (!gOpt[ID_EV_ON_FIRST + a]) {
            Link_Fail("that event is off", "");
        } else if (Events_IsHidden(a) || (sBanMask & (1ull << a))) {
            Link_Fail("that event is banned this cycle", "");
        } else if (sFriendNow >= 0) {
            Link_Fail("an event is already queued", "");
        } else if ((sActive >= 0) || (sRouletteTimer > 0) || !Ev_CanRun(play, a)) {
            sFriendNow = a; // starts as soon as it can
            sFriendNowId = sLinkCurId;
            Ev_Append(sFriendNowFrom, 0, sLinkFrom, sizeof(sFriendNowFrom));
            Link_Result("Starts as soon as it can: ", Events_ShortName(a));
            Link_Say("Your friend has plans for you...", "");
        } else {
            sRouletteTimer = 0;
            sStartByFriend = true;
            Ev_Start(play, a);
            Link_Result("Started ", Events_Name(a));
            Link_Say("Your friend started ", Events_ShortName(a));
        }
    } else if (Str_Eq(type, "ev_next") && Link_EventOk(a)) {
        if (!gOpt[ID_EV_MASTER]) {
            Link_Fail("events are off", "");
        } else if (sForesight) {
            Link_Fail("Foresight locked the next event", "");
        } else if (!gOpt[ID_EV_ON_FIRST + a]) {
            Link_Fail("that event is off", "");
        } else if ((b >= 0) && !Link_ComboOk(a, b)) {
            Link_Fail("that combo can't happen", ""); // refunds the combo price too
        } else {
            sFriendNext = a;
            sFriendCombo = (b >= 0) ? b : -1;
            Link_Result((sFriendCombo >= 0) ? "Next event (combo): " : "Next event: ", Events_ShortName(a));
            Link_Say("Your friend picked your next event...", "");
        }
    } else if (Str_Eq(type, "ev_soon")) {
        if (!gOpt[ID_EV_MASTER]) {
            Link_Fail("events are off", "");
        } else if (sActive >= 0) {
            Link_Fail("an event is running", "");
        } else {
            sUntilNext = MIN(sUntilNext, 10 * FPS);
            Link_Result("Next event in 10 seconds", "");
            Link_Say("Your friend hurried the next event!", "");
        }
    } else if (Str_Eq(type, "nem_make") && Link_PoolOk(a)) {
        if (!V2_ON(ID_V2_NEM)) {
            Link_Fail("Nemesis is off", "");
        } else if (sNem.pool >= 0) {
            Link_Fail("they already have one", "");
        } else {
            Nem_Promote(play, a, NULL);
            Link_Result("Created a Nemesis: ", Pool_Name(a));
        }
    } else if (Str_Eq(type, "nem_trait") && (a >= 0) && (a < TR_COUNT)) {
        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (Nem_Traits() & (1 << a)) {
            Link_Fail("it already has ", sTraitNames[a]);
        } else {
            sNemBonusTraits |= (1 << a);
            Nem_RefreshLive(play);
            Link_Result("Nemesis trait: ", sTraitNames[a]);
            Link_Say("Nemesis gained ", sTraitNames[a]);
        }
    } else if (Str_Eq(type, "nem_level")) {
        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (sNem.level >= Nem_TypeCap(sNem.pool)) {
            Link_Fail("it's at max level", "");
        } else {
            Link_Format("Your friend leveled up your Nemesis!", "");
            Nem_LevelUp(sLinkToast);
            Link_Result("Nemesis leveled up", "");
        }
    } else if (Str_Eq(type, "nem_hunt")) {
        Player* player = GET_PLAYER(play);

        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
            // Already here: it jumps right next to them.
            Vec3f spot;

            if ((player != NULL) &&
                Ev_FindSpot(play, 180.0f, 320.0f, player->actor.shape.rot.y, 0x7FFF, true, &spot)) {
                s32 pool = V2_PoolOfActor(sNem.actor);

                Ev_Poof(play, &sNem.actor->world.pos, 1);
                spot.y += sPool[(pool >= 0) ? pool : sNem.pool].yOffset;
                sNem.actor->world.pos = spot;
                sNem.actor->prevPos = spot;
                Ev_Poof(play, &spot, 1);
                Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
            }
            Link_Result("Nemesis jumped to them", "");
            Link_Say("Your Nemesis found you!", "");
        } else if (Link_TrySpawnNemNear(play)) {
            Link_Result("Nemesis sent after them", "");
            Link_Say("Your Nemesis found you!", "");
        } else {
            sNemHuntTimer = 15 * FPS; // keeps trying for a bit
            Link_Result("Nemesis is on its way", "");
            Link_Say("Your Nemesis is coming for you!", "");
        }
    } else if (Str_Eq(type, "draft") && (a >= 0) && (a < MUT_COUNT) && (b >= 0) && (b < MUT_COUNT) &&
               (a != MUT_RETIRED) && (b != MUT_RETIRED)) {
        if (!gOpt[ID_V2_MUT]) {
            Link_Fail("mutators are off", "");
        } else if ((a == b) || (sMutCount >= 6) || (Mut_Has(a) && Mut_Has(b))) {
            Link_Fail("that draft can't happen", "");
        } else {
            sFriendDraftA = a;
            sFriendDraftB = b;
            Link_Result("Next draft rigged", "");
            Link_Say("Your friend rigged your next draft...", "");
        }
    } else if (Str_Eq(type, "bounty") && Link_PoolOk(a) && (b >= 1) && (b <= 5)) {
        if (!V2_ON(ID_V2_BOUNTY)) {
            Link_Fail("bounties are off", "");
        } else {
            s32 zone = V2_ZoneNow(play);
            Bounty* bt = &sBounties[SLOT_FRIEND];
            s32 bits = 0;
            s32 k;

            if ((bt->actor != NULL) && V2_IsLive(play, bt->actor)) {
                Ev_KillOne(play, bt->actor);
            }
            Bounty_Fill(SLOT_FRIEND, a, b, BK_FRIEND);
            for (k = 0; k < 16; k++) {
                bits += (c >> k) & 1;
            }
            if (((c & 0xFFFF) != 0) && (bits <= b)) {
                bt->traits = (u16)c;
            }
            bt->zone = (zone >= 0) ? zone : 1;
            bt->accepted = true; // it comes to you
            // Right here, right now if there's room.
            bt->actor = V2_SpawnEnemy(play, a, NULL, 250.0f, 450.0f, TAG_BOUNTY);
            if (bt->actor != NULL) {
                V2_ApplyTraits(bt->actor, bt->traits, 1.0f + 0.25f * bt->stars);
                bt->spawnedVisit = true;
                bt->aliveTimer = 0;
                bt->hitSince = false;
                Ev_Poof(play, &bt->actor->world.pos, 1);
            }
            Link_Result("Bounty posted: ", Pool_Name(a));
            Link_Say("Your friend put a bounty on you!", "");
        }
    } else if (Str_Eq(type, "ambush") && Link_PoolOk(a) && (b >= 1) && (b <= 3)) {
        s32 i;
        s32 made = 0;

        const char* at = text;

        for (i = 0; i < b; i++) {
            Vec3f spot;
            s32 x;
            s32 z;
            s32 used = Link_ParseXZ(at, &x, &z);
            Actor* e = NULL;

            // Where the friend clicked on the radar (if it's ground), else near them.
            if (used > 0) {
                at += used;
                if (Link_GroundAt(play, (f32)x, (f32)z, &spot) && (World_ExitDist(&spot) >= 150.0f)) {
                    e = V2_SpawnEnemy(play, a, &spot, 0.0f, 0.0f, TAG_V2);
                }
            }
            if (e == NULL) {
                e = V2_SpawnEnemy(play, a, NULL, 200.0f, 450.0f, TAG_V2);
            }
            if (e != NULL) {
                made++;
            }
        }
        if (made > 0) {
            Link_Result("Ambush: ", Pool_Name(a));
            Link_Say("Ambush! Courtesy of your friend.", "");
        } else {
            Link_Fail("no room for an ambush", "");
        }
    } else if (Str_Eq(type, "curse")) {
        if (!gOpt[ID_EV_CURSE]) {
            Link_Fail("Cycle Curse is off", "");
        } else {
            Curse_Roll();
            Link_Result("Curse rerolled: ", Curse_Name(sCurse));
        }
    } else if (Str_Eq(type, "gift")) {
        sMarks = MIN(sMarks + 10, 60000);
        Link_Result("Gave 10 Moon Marks", "");
        Link_Say("Your friend sent you 10 Moon Marks!", "");
    } else if (Str_Eq(type, "tax")) {
        if (sMarks <= 0) {
            Link_Fail("they're broke", "");
        } else {
            sMarks = MAX(sMarks - 15, 0);
            Link_Result("Took 15 Moon Marks", "");
            Link_Say("Your friend taxed 15 Moon Marks!", "");
        }
    } else if (Str_Eq(type, "airstrike")) {
        Player* player = GET_PLAYER(play);
        Vec3f center;
        s32 i;

        // Aimed on the radar (c = 1), or right on them.
        if ((player != NULL) && !((c == 1) && Link_GroundAt(play, (f32)a, (f32)b, &center))) {
            center = player->actor.world.pos;
        }
        for (i = 0; (player != NULL) && (i < 5); i++) {
            Vec3f to = center;
            Vec3f from;

            to.x += Rand_CenteredFloat(360.0f);
            to.z += Rand_CenteredFloat(360.0f);
            from = to;
            from.y = Ev_SkyHeight(play, &to, 250.0f);
            Drop_Launch(play, &from, &to, 30 + i * 12);
        }
        Link_Result("Air strike", "");
        Link_Say("Incoming air strike!", "");
    } else if (Str_Eq(type, "heal")) {
        if ((hp <= 0) || (hp >= cap)) {
            Link_Fail("they're already full", "");
        } else {
            gSaveContext.save.saveInfo.playerData.health = MIN(hp + 0x20, cap);
            sV2PrevHp = gSaveContext.save.saveInfo.playerData.health; // not a hit or a potion
            Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
            Link_Result("Healed 2 hearts", "");
            Link_Say("Your friend healed you!", "");
        }
    } else if (Str_Eq(type, "sfx") && (a >= 0) && (a < (s32)ARRAY_COUNT(sPrankSfx))) {
        Audio_PlaySfx(sPrankSfx[a]); // no message: that's the joke
        Link_Result("Played a sound", "");
    } else if (Str_Eq(type, "shake")) {
        Camera* cam = GET_ACTIVE_CAM(play);

        if (cam != NULL) {
            s16 q = Quake_Request(cam, QUAKE_TYPE_3);

            Quake_SetSpeed(q, 20000);
            Quake_SetPerturbations(q, 12, 0, 0, 0);
            Quake_SetDuration(q, 45);
            Audio_PlaySfx(NA_SE_IT_BOMB_EXPLOSION);
        }
        Link_Result("Shook the ground", "");
    } else if (Str_Eq(type, "ev_end")) {
        if (sActive < 0) {
            Link_Fail("no event running", "");
        } else {
            Ev_Abort(play);
            Link_Result("Ended the event", "");
            Link_Say("Your friend ended the event.", "");
        }
    } else if (Str_Eq(type, "ev_extend")) {
        if ((sActive < 0) || !Wave2_IsTimed()) {
            Link_Fail("no timed event running", "");
        } else {
            sFramesLeft += 30 * FPS;
            sTotalFrames = MAX(sTotalFrames, sFramesLeft);
            Link_Result("Event +30 seconds", "");
            Link_Say("Your friend added 30 seconds!", "");
        }
    } else if (Str_Eq(type, "pouch") && (a >= 0) && (a < PI_COUNT)) {
        if (!V2_ON(ID_V2_POUCH)) {
            Link_Fail("the pouch is off", "");
        } else if (sPouch[a] + sPouchAmount[a] > sPouchMax[a]) {
            Link_Fail("their pouch is full", "");
        } else {
            sPouch[a] += sPouchAmount[a];
            sPouchSel = a;
            sPouchFlash = 10;
            Link_Result("Gave ", sPouchNames[a]);
            Link_Say("Friend gift: ", sPouchNames[a]);
        }
    } else if (Str_Eq(type, "curse_set") && (a >= 0) && (a < CURSE_COUNT)) {
        if (!gOpt[ID_EV_CURSE]) {
            Link_Fail("Cycle Curse is off", "");
        } else if (!Curse_CanUse(a) || (a == sCurse)) {
            Link_Fail("can't use ", Curse_Name(a));
        } else {
            sCurse = a;
            Curse_PickFavorite();
            sCurseCardPending = true;
            sWrath = 0;
            sWrathPending = false;
            Link_Result("Curse set: ", Curse_Name(a));
        }
    } else if (Str_Eq(type, "wanted")) {
        if (!V2_ON(ID_V2_BOUNTY)) {
            Link_Fail("bounties are off", "");
        } else if (sBounties[SLOT_WANTED].pool >= 0) {
            Link_Fail("a WANTED poster is already up", "");
        } else {
            sWantedPosted = true;
            Bounty_Fill(SLOT_WANTED, sHuntPool[V2_Rand(ARRAY_COUNT(sHuntPool))], 5, BK_WANTED);
            Link_Result("WANTED poster up", "");
            Link_Say("Your friend put up a WANTED poster!", "");
        }
    } else if (Str_Eq(type, "size") && ((a == LINKSIZE_GIANT) || (a == LINKSIZE_TINY))) {
        gFriendLinkSize = a;
        sFriendSizeTimer = 30 * FPS;
        Link_Result((a == LINKSIZE_GIANT) ? "Made them giant" : "Made them tiny", "");
        Link_Say((a == LINKSIZE_GIANT) ? "Your friend made you GIANT!" : "Your friend made you tiny!", "");
    } else if (Str_Eq(type, "fakeout")) {
        // Looks exactly like a real warning (same box, same beeps) when a real one could show.
        sFakeTimer = 5 * FPS;
        if (Ev_ShowWarning() && ((gOpt[ID_EV_TIMERSTYLE] % LVL_MAX) != LVL_HIGH)) {
            if (gOpt[ID_EV_WARNSOUND]) {
                Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
            }
        } else {
            Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
            Menu_ShowToast("Something is coming...");
        }
        Link_Result("Fake out", "");
    } else if (Str_Eq(type, "nem_name") && (text[0] != '\0')) {
        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else {
            s32 k;

            for (k = 0; (text[k] != '\0') && (k < 12); k++) {
                sNemCustom[k] = text[k];
            }
            sNemCustom[k] = '\0';
            Link_Result("Nemesis renamed: ", sNemCustom); // quietly: they find out when they meet it
        }
    } else if (Str_Eq(type, "nem_stat") && (a >= 0) && (a < NS_COUNT)) {
        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (Nem_PointsFree() <= 0) {
            Link_Fail("no points to spend", "");
        } else if (!Nem_SpendPoint(a)) {
            Link_Fail("that stat is maxed: ", sNemStatNames[a]);
        } else {
            sNemPointTimer = 0;
            Link_Result("Nemesis +1 ", sNemStatNames[a]);
            Link_Say("Friend trained your Nemesis: +", sNemStatNames[a]);
        }
    } else if (Str_Eq(type, "nem_purge")) {
        if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else {
            char num[8];

            if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
                Ev_Poof(play, &sNem.actor->world.pos, 1);
                Ev_KillOne(play, sNem.actor);
            }
            sNemInherit = (u8)CLAMP(MAX(sNemInherit, sNem.level), 1, NEM_LEVEL_MAX);
            sNem.pool = -1;
            sNem.actor = NULL;
            sNem.engaged = false;
            Nem_ClearFriendStuff();
            Nem_ResetStats();
            sNemRestSlot = Mut_SlotNow(); // the next one has to be crowned, not handed out on the spot
            Ev_Itoa(num, sNemInherit);
            Link_Result("Nemesis purged. Next starts at level ", num);
            Link_Say("Friend purged your Nemesis. Up to Lv", num);
        }
    } else if (Str_Eq(type, "empower")) {
        if (!gOpt[ID_EV_MASTER]) {
            Link_Fail("events are off", "");
        } else if (sFriendEmpowerNext) {
            Link_Fail("the next one is already empowered", "");
        } else {
            sFriendEmpowerNext = true;
            Link_Result("Next event empowered", "");
            Link_Say("Your friend empowered your next event", "");
        }
    } else if (Str_Eq(type, "blackout")) {
        sFriendDarkTimer = 25 * FPS;
        Link_Result("Blackout", "");
        Link_Say("Lights out! Courtesy of your friend.", "");
    } else if (Str_Eq(type, "place")) {
        // place|event|x|z|extra: moves this event's spot, early on.
        Vec3f spot;
        s32 ok = false;

        if (!Place_Open() || (sActive != a)) {
            Link_Fail("too late to place it", "");
        } else if (!Link_GroundAt(play, (f32)b, (f32)c, &spot)) {
            Link_Fail("no ground there", "");
        } else {
            if (a == EV_TREASURE) {
                ok = Treasure_PlaceAt(play, spot.x, spot.z);
            } else if (a == EV_PROCESSION) {
                ok = Proc_PlaceAt(play, spot.x, spot.z);
            } else {
                ok = V3_Place(play, a, &spot, Link_Int(text));
            }
            if (ok) {
                Link_Result("Placed: ", Events_ShortName(a));
            } else {
                Link_Fail("can't place it there", "");
            }
        }
    } else if (Str_Eq(type, "surprise") && (a >= 0) && (a < ZONE_COUNT) && (b >= 0) && (b < SUR_KINDS)) {
        // surprise|area|kind|event or enemy|"x,z": waits in that area until they walk into it.
        Surprise* sp;
        s32 x = 0;
        s32 z = 0;

        if (sSurCount == SURPRISE_MAX) {
            Link_Fail("too many surprises waiting", "");
        } else if ((b == SUR_EVENT) && !Link_EventOk(c)) {
            Link_Fail("pick an event", "");
        } else if ((b == SUR_AMBUSH) && !Link_PoolOk(c)) {
            Link_Fail("pick an enemy", "");
        } else {
            sp = &sSur[sSurCount++];
            sp->id = sLinkCurId;
            sp->zone = (s8)a;
            sp->kind = (u8)b;
            sp->arg = (s16)c;
            sp->ready = false;
            sp->hasPos = Link_ParseXZ(text, &x, &z) > 0;
            sp->pos.x = (f32)x;
            sp->pos.y = 0.0f;
            sp->pos.z = (f32)z;
            Ev_Append(sp->from, 0, sLinkFrom, sizeof(sp->from));
            if (!sp->hasPos) {
                sp->pos.x = sp->pos.z = 0.0f;
            }
            Link_Result("Surprise waiting in ", sZoneNames[a]);
            if (sLinkFrom[0] != '\0') {
                s32 n = Ev_Append(sLinkToast, 0, sLinkFrom, sizeof(sLinkToast));

                Ev_Append(sLinkToast, n, " has placed a surprise...", sizeof(sLinkToast));
            } else {
                Ev_Append(sLinkToast, 0, "Your friend has placed a surprise...", sizeof(sLinkToast));
            }
            Menu_Notify(sLinkToast, NOTE_FRIEND);
            Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
        }
    } else if (Str_Eq(type, "msg") && (text[0] != '\0')) {
        Link_Result("Message shown", "");
        Link_Say("Friend: ", text);
    }
    return true;
}

// --- Status for the panel ----------------------------------------------------

static s32 Link_Line(s32 n, const char* key, const char* value) {
    n = Ev_Append(sLinkStatus, n, key, sizeof(sLinkStatus));
    n = Ev_Append(sLinkStatus, n, "=", sizeof(sLinkStatus));
    n = Ev_Append(sLinkStatus, n, value, sizeof(sLinkStatus));
    return Ev_Append(sLinkStatus, n, "\n", sizeof(sLinkStatus));
}

static s32 Link_LineNum(s32 n, const char* key, s32 value) {
    char num[16];

    Ev_Itoa(num, value);
    return Link_Line(n, key, num);
}

// Why commands are waiting right now ("" = they aren't).
static const char* Link_HoldReason(PlayState* play, s32 playing) {
    if (!V2_ON(ID_V2_LINK)) {
        return "Friend Link is off";
    }
    if (gSaveContext.save.saveInfo.playerData.health <= 0) {
        return "They're down";
    }
    if (sQuizHolding) {
        return "Pop Quiz in progress";
    }
    if (!playing || gMenuOpen || sSaveUiOpen) {
        return "Paused or in a menu";
    }
    return "";
}

// V3: what the panel's radar, map and friend list need.
static s32 Link_Radar(PlayState* play, s32 n) {
    static char big[640];
    Player* player = GET_PLAYER(play);
    Actor* near[24];
    f32 dist[24];
    s32 count = 0;
    s32 t;
    s32 k;
    s32 c;

    if (player != NULL) {
        Camera* cam = GET_ACTIVE_CAM(play);

        t = Ev_AppendNum(big, 0, (s32)player->actor.world.pos.x, sizeof(big));
        t = Ev_Append(big, t, ",", sizeof(big));
        t = Ev_AppendNum(big, t, (s32)player->actor.world.pos.y, sizeof(big));
        t = Ev_Append(big, t, ",", sizeof(big));
        t = Ev_AppendNum(big, t, (s32)player->actor.world.pos.z, sizeof(big));
        t = Ev_Append(big, t, ",", sizeof(big));
        t = Ev_AppendNum(big, t, (s32)((u16)player->actor.shape.rot.y * 360 / 0x10000), sizeof(big));
        t = Ev_Append(big, t, ",", sizeof(big));
        Ev_AppendNum(big, t, (cam != NULL) ? (s32)((u16)Camera_GetCamDirYaw(cam) * 360 / 0x10000) : 0, sizeof(big));
        n = Link_Line(n, "pos", big);
    }
    n = Link_LineNum(n, "scene", play->sceneId);

    // The 24 closest enemies and people (e enemy, b bounty, N Nemesis, n npc), then surprises here (s).
    for (c = 0; (player != NULL) && (c < 2); c++) {
        Actor* actor = play->actorCtx.actorLists[(c == 0) ? ACTORCAT_ENEMY : ACTORCAT_NPC].first;

        for (; actor != NULL; actor = actor->next) {
            f32 d = Ev_DistXZ(&actor->world.pos, &player->actor.world.pos);
            s32 slot;

            if ((actor->update == NULL) || (d > 2500.0f) || ((c == 0) && (actor->colChkInfo.health <= 0))) {
                continue;
            }
            if (count < 24) {
                slot = count++;
            } else {
                slot = 0;
                for (k = 1; k < 24; k++) {
                    if (dist[k] > dist[slot]) {
                        slot = k;
                    }
                }
                if (dist[slot] <= d) {
                    continue;
                }
            }
            near[slot] = actor;
            dist[slot] = d;
        }
    }
    t = 0;
    big[0] = '\0';
    for (k = 0; k < count; k++) {
        Actor* actor = near[k];
        u8 tag = Tag_Of(actor);
        const char* kind = (actor->category != ACTORCAT_ENEMY) ? "n"
                           : ((actor == sNem.actor) || (tag == TAG_NEMV2)) ? "N"
                           : (tag == TAG_BOUNTY) ? "b"
                                                 : "e";

        t = Ev_Append(big, t, (t > 0) ? ";" : "", sizeof(big));
        t = Ev_Append(big, t, kind, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_AppendNum(big, t, (s32)actor->world.pos.x, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_AppendNum(big, t, (s32)actor->world.pos.z, sizeof(big));
    }
    for (k = 0; k < sSurCount; k++) {
        if (sSur[k].ready && (sSur[k].zone == V2_ZoneNow(play))) {
            t = Ev_Append(big, t, (t > 0) ? ";s:" : "s:", sizeof(big));
            t = Ev_AppendNum(big, t, (s32)sSur[k].pos.x, sizeof(big));
            t = Ev_Append(big, t, ":", sizeof(big));
            t = Ev_AppendNum(big, t, (s32)sSur[k].pos.z, sizeof(big));
        }
    }
    n = Link_Line(n, "acts", big);

    // Every friend we know, with their mode (0 blocked, 1 helpful, 2 full).
    t = 0;
    big[0] = '\0';
    for (k = 0; k < sFrCount; k++) {
        t = Ev_Append(big, t, (k > 0) ? "," : "", sizeof(big));
        t = Ev_Append(big, t, sFr[k].name, sizeof(big));
        t = Ev_Append(big, t, (sFr[k].mode == FMODE_BLOCKED) ? ":0" : (sFr[k].mode == FMODE_HELPFUL) ? ":1" : ":2",
                      sizeof(big));
    }
    n = Link_Line(n, "fmodes", big);

    // A placement window: event, seconds left, how many spots (Pressure Seals has several).
    if (Place_Open()) {
        t = Ev_AppendNum(big, 0, sActive, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_AppendNum(big, t, (PLACE_WINDOW - sEventFrames + FPS - 1) / FPS, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        Ev_AppendNum(big, t, (sActive == EV_SEALS) ? V3_PlaceCount() : 1, sizeof(big));
        n = Link_Line(n, "evplace", big);
    }
    n = Link_LineNum(n, "steer", Steer_Kind());

    // Surprises still waiting: area:kind:from (never where exactly).
    t = 0;
    big[0] = '\0';
    for (k = 0; k < sSurCount; k++) {
        t = Ev_Append(big, t, (k > 0) ? ";" : "", sizeof(big));
        t = Ev_AppendNum(big, t, sSur[k].zone, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_AppendNum(big, t, sSur[k].kind, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_Append(big, t, sSur[k].from, sizeof(big));
    }
    n = Link_Line(n, "surprises", big);
    return n;
}

static void Link_SendStatus(PlayState* play, s32 playing) {
    char tmp[320];
    s32 n = 0;
    s32 k;
    s32 t;
    s32 zone = V2_ZoneNow(play);
    s32 hp = gSaveContext.save.saveInfo.playerData.health;
    s32 cap = gSaveContext.save.saveInfo.playerData.healthCapacity;
    s32 minutes = (s32)gSaveContext.save.time * 1440 / 0x10000;
    static const char* sQuarter[4] = { "", ".25", ".5", ".75" };
    u64 mask = 0;
    s32 opts = 0;

    n = Link_Line(n, "link", V2_ON(ID_V2_LINK) ? "1" : "0");
    n = Link_Line(n, "playing", playing ? "1" : "0");
    n = Link_Line(n, "hold", Link_HoldReason(play, playing));
    n = Link_Line(n, "area", (zone >= 0) ? sZoneNames[zone] : "Indoors or a dungeon");
    n = Link_LineNum(n, "day", gSaveContext.save.day);
    t = Ev_AppendNum(tmp, 0, minutes / 60, sizeof(tmp));
    t = Ev_Append(tmp, t, ((minutes % 60) < 10) ? ":0" : ":", sizeof(tmp));
    Ev_AppendNum(tmp, t, minutes % 60, sizeof(tmp));
    n = Link_Line(n, "clock", tmp);
    n = Link_Line(n, "night", Ev_IsNight() ? "1" : "0");
    t = Ev_AppendNum(tmp, 0, hp / 16, sizeof(tmp));
    Ev_Append(tmp, t, sQuarter[(hp % 16) / 4], sizeof(tmp));
    n = Link_Line(n, "hp", tmp);
    n = Link_LineNum(n, "maxhp", cap / 16);
    n = Link_Line(n, "hpfull", (hp >= cap) ? "1" : "0");
    if (sActive >= 0) {
        t = Ev_Append(tmp, 0, Events_Name(sActive), sizeof(tmp));
        if (sCombo >= 0) {
            t = Ev_Append(tmp, t, " + ", sizeof(tmp));
            Ev_Append(tmp, t, Events_Name(sCombo), sizeof(tmp));
        }
        n = Link_Line(n, "ev", tmp);
        if (Wave2_IsTimed()) {
            n = Link_LineNum(n, "evleft", sFramesLeft / FPS);
        }
    } else {
        n = Link_Line(n, "ev", "-");
        if (gOpt[ID_EV_MASTER]) {
            n = Link_LineNum(n, "next", sUntilNext / FPS);
        }
    }
    n = Link_Line(n, "curse", gOpt[ID_EV_CURSE] ? Curse_Name(sCurse) : "Off");
    n = Link_LineNum(n, "curseid", gOpt[ID_EV_CURSE] ? sCurse : -1);
    n = Link_LineNum(n, "marks", sMarks);
    n = Link_Line(n, "rank", sRankNames[Rank_Of(sBountiesDone)]);
    if (sNem.pool >= 0) {
        u16 tr = Nem_Traits();
        s32 first = true;

        Nem_Name(tmp, sizeof(tmp));
        t = Ui_StrLen(tmp);
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_Append(tmp, t, Pool_Name(sNem.pool), sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_AppendNum(tmp, t, Nem_EffLevel(), sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_Append(tmp, t, sZoneNames[sNem.zone % ZONE_COUNT], sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        for (k = 0; k < TR_COUNT; k++) {
            if (tr & (1 << k)) {
                t = Ev_Append(tmp, t, first ? "" : ", ", sizeof(tmp));
                t = Ev_Append(tmp, t, sTraitNames[k], sizeof(tmp));
                first = false;
            }
        }
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_AppendNum(tmp, t, sNem.level, sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        Ev_AppendNum(tmp, t, Nem_TypeCap(sNem.pool), sizeof(tmp));
        n = Link_Line(n, "nem", tmp); // name|type|level now|area|traits|level|max level
        t = 0;
        for (k = 0; k < NS_COUNT; k++) {
            t = Ev_AppendNum(tmp, t, sNemStat[k], sizeof(tmp));
            t = Ev_Append(tmp, t, ",", sizeof(tmp));
        }
        Ev_AppendNum(tmp, t, Nem_PointsFree(), sizeof(tmp));
        n = Link_Line(n, "nemstats", tmp); // speed,power,health,tracking,unspent
    } else {
        n = Link_Line(n, "nem", "-");
    }
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < sMutCount; k++) {
        t = Ev_Append(tmp, t, (k > 0) ? "," : "", sizeof(tmp));
        t = Ev_Append(tmp, t, sMutNames[sMutOrder[k]], sizeof(tmp));
    }
    n = Link_Line(n, "muts", tmp);
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < V2_BOUNTY_SLOTS; k++) {
        Bounty* bt = &sBounties[k];
        s32 s;

        if ((bt->pool < 0) || !bt->accepted) {
            continue;
        }
        t = Ev_Append(tmp, t, (t > 0) ? ";" : "", sizeof(tmp));
        t = Ev_Append(tmp, t,
                      (bt->kind == BK_FRIEND)   ? "Yours: "
                      : (bt->kind == BK_WANTED) ? "WANTED "
                      : (bt->kind == BK_RARE)   ? "RARE "
                                                : "",
                      sizeof(tmp));
        t = Ev_Append(tmp, t, Pool_Name(bt->pool), sizeof(tmp));
        t = Ev_Append(tmp, t, " ", sizeof(tmp));
        for (s = 0; (s < bt->stars) && (t < (s32)sizeof(tmp) - 1); s++) {
            tmp[t++] = '*';
        }
        tmp[t] = '\0';
        t = Ev_Append(tmp, t, " @ ", sizeof(tmp));
        t = Ev_Append(tmp, t, sZoneNames[bt->zone % ZONE_COUNT], sizeof(tmp));
    }
    n = Link_Line(n, "bounties", tmp);
    // For your friend's map: where you are (-1 indoors), where you last were outdoors,
    // where your Nemesis is, and where each taken bounty waits (kind letter + area).
    n = Link_LineNum(n, "pz", zone);
    n = Link_LineNum(n, "lz", sPlayerZone);
    n = Link_LineNum(n, "nz", (sNem.pool >= 0) ? sNem.zone % ZONE_COUNT : -1);
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < V2_BOUNTY_SLOTS; k++) {
        Bounty* bt = &sBounties[k];

        if ((bt->pool < 0) || !bt->accepted) {
            continue;
        }
        t = Ev_Append(tmp, t, (t > 0) ? "," : "", sizeof(tmp));
        t = Ev_Append(tmp, t,
                      (bt->kind == BK_FRIEND)   ? "F"
                      : (bt->kind == BK_WANTED) ? "W"
                      : (bt->kind == BK_RARE)   ? "R"
                                                : "B",
                      sizeof(tmp));
        t = Ev_AppendNum(tmp, t, bt->zone % ZONE_COUNT, sizeof(tmp));
    }
    n = Link_Line(n, "bz", tmp);
    // Which events they have on (hex bitmask), so the panel can grey out the rest.
    for (k = 0; k < EV_COUNT; k++) {
        if (gOpt[ID_EV_ON_FIRST + k] && !Events_IsHidden(k)) {
            mask |= (1ull << k);
        }
    }
    for (k = 0; k < 16; k++) {
        tmp[k] = "0123456789abcdef"[(mask >> (4 * (15 - k))) & 0xF];
    }
    tmp[16] = '\0';
    n = Link_Line(n, "enabled", tmp);
    // Feature switches: events, mutators, bounties, Nemesis, pouch, curse, combos.
    opts = (gOpt[ID_EV_MASTER] ? 1 : 0) | (gOpt[ID_V2_MUT] ? 2 : 0) | (V2_ON(ID_V2_BOUNTY) ? 4 : 0) |
           (V2_ON(ID_V2_NEM) ? 8 : 0) | (V2_ON(ID_V2_POUCH) ? 16 : 0) | (gOpt[ID_EV_CURSE] ? 32 : 0) |
           (gOpt[ID_EV_COMBOS] ? 64 : 0);
    n = Link_LineNum(n, "opts", opts);
    n = Link_LineNum(n, "mutcount", sMutCount);
    n = Link_LineNum(n, "neminherit", sNemInherit);
    n = Link_Line(n, "foresight", sForesight ? "1" : "0");
    n = Link_Line(n, "friendnext", (sFriendNext >= 0) ? Events_Name(sFriendNext) : "");
    n = Link_Line(n, "friendnow", (sFriendNow >= 0) ? Events_Name(sFriendNow) : "");
    n = Link_LineNum(n, "shields", sFriendShields);
    n = Link_Line(n, "empower", sFriendEmpowerNext ? "1" : "0");
    n = Link_Line(n, "wanted", (sBounties[SLOT_WANTED].pool >= 0) ? "1" : "0");
    n = Link_LineNum(n, "deaths", sLinkDeaths);
    n = Link_LineNum(n, "survived", sLinkSurvived);
    n = Link_LineNum(n, "claimed", sBountiesDone);
    n = Link_LineNum(n, "evseq", sEvSeq);
    n = Link_Line(n, "evres", sEvResult);
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < PI_COUNT; k++) {
        if (sPouch[k] > 0) {
            t = Ev_Append(tmp, t, (t > 0) ? ", " : "", sizeof(tmp));
            t = Ev_Append(tmp, t, sPouchNames[k], sizeof(tmp));
            t = Ev_Append(tmp, t, " x", sizeof(tmp));
            t = Ev_AppendNum(tmp, t, sPouch[k], sizeof(tmp));
        }
    }
    n = Link_Line(n, "pouch", tmp);
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < sRefundCount; k++) {
        t = Ev_Append(tmp, t, (k > 0) ? "," : "", sizeof(tmp));
        t = Ev_AppendNum(tmp, t, sRefunds[k], sizeof(tmp));
    }
    n = Link_Line(n, "refund", tmp);
    n = Link_Line(n, "last", sLinkLast);
    n = Link_Radar(play, n);
    ger_status(sLinkStatus, (u32)n);
}

// Every frame, from V2_Update.
static void Link_Update(PlayState* play, s32 playing) {
    static char cmd[128];

    if (!sLinkInit) {
        sLinkInit = true;
        sLinkReady = ger_init();
    }
    if (!sFrLoaded) {
        Fr_Load();
    }
    if (sSteerTimer > 0) {
        if (sSteerEv != sActive) {
            sSteerTimer = 0;
        } else if (playing) {
            sSteerTimer--;
        }
    }
    if ((sFriendSizeTimer > 0) && playing && (--sFriendSizeTimer == 0)) {
        gFriendLinkSize = 0;
    }
    if ((sFakeTimer > 0) && playing && gOpt[ID_EV_WARNSOUND] && Ev_ShowWarning() && ((sFakeTimer % FPS) == 0) &&
        (sFakeTimer < 5 * FPS)) {
        Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
    }
    if ((sFakeTimer > 0) && playing && (--sFakeTimer == 0)) {
        Menu_ShowToast("...or not.");
    }
    // Blackout: dark skies while no event has its own look going.
    if (sFriendDarkTimer > 0) {
        if (playing) {
            sFriendDarkTimer--;
        }
        if (sActive < 0) {
            if (sFriendDarkTimer > 0) {
                Ev_ApplyEnv(play, ENV_DARK, 0.8f);
            } else if (!sNightDark) {
                Ev_ClearEnv(play);
                Ev_RestoreLighting(play);
            }
        }
    }
    if (!sLinkReady) {
        return;
    }
    if (++sLinkTimer >= FPS) {
        sLinkTimer = 0;
        Fr_Poll(ger_state() == 2);
        Link_SendStatus(play, playing);
    }
    if (playing && V2_ON(ID_V2_LINK) && (ger_state() == 2)) {
        LinkMap_Update(play);
    }
    if (!V2_ON(ID_V2_LINK)) {
        while (ger_poll(cmd, sizeof(cmd)) > 0) {
            // Link off: drop anything that arrives.
        }
        Link_DropFriendNow();
        sNemHuntTimer = 0;
        return;
    }
    if (sLinkGap > 0) {
        sLinkGap--;
    }
    // Commands wait while Link can't take them: paused, a menu, the Pop Quiz,
    // or while he's going down.
    if (Link_HoldReason(play, playing)[0] != '\0') {
        return;
    }
    // A "start now" that had to wait.
    if ((sFriendNow >= 0) && (sActive < 0) && (sRouletteTimer == 0) && (sCurseCardTimer == 0)) {
        if (!gOpt[ID_EV_MASTER] || !gOpt[ID_EV_ON_FIRST + sFriendNow] || (sBanMask & (1ull << sFriendNow))) {
            Link_DropFriendNow();
        } else if (Ev_CanRun(play, sFriendNow)) {
            s32 ev = sFriendNow;

            sFriendNow = -1;
            sFriendNowId = 0;
            sStartByFriend = true;
            Ev_Start(play, ev);
            Ev_Append(sLinkFrom, 0, sFriendNowFrom, sizeof(sLinkFrom));
            Link_Say("Your friend started ", Events_ShortName(ev));
        }
    }
    // "Send it to them" still looking for room.
    if (sNemHuntTimer > 0) {
        sNemHuntTimer--;
        if ((sNem.pool < 0) || ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor))) {
            sNemHuntTimer = 0;
        } else if (((sNemHuntTimer % 10) == 0) && Link_TrySpawnNemNear(play)) {
            sNemHuntTimer = 0;
            Menu_ShowToast("Your Nemesis found you!");
        }
    }
    Sur_Update(play);
    if (sLinkGap > 0) {
        return;
    }
    if ((ger_poll(cmd, sizeof(cmd)) > 0) && Link_Run(play, cmd)) {
        sLinkGap = FPS; // a second between commands, so each one lands
    }
}
