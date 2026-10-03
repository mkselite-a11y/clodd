// ---------------------------------------------------------------------------
// Friend Link (2.4): your friend's web panel sends commands through a relay;
// CUSTOM_GlobalEventsRemote.dll (next to the mod) does the networking on its own
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
static s32 sNemHuntSlot = 0;      // ...for this Nemesis
static s32 sLinkCurId = 0;        // id of the command being run
static s32 sRefunds[8];           // ids of commands that couldn't run (the relay refunds them)
static s32 sRefundCount = 0;


// Soundboard (panel order: keep in step with "sounds" in remote/relay/tables.json).
static const u16 sPrankSfx[] = {
    NA_SE_SY_GET_ITEM,       NA_SE_SY_CORRECT_CHIME, NA_SE_SY_TRE_BOX_APPEAR, NA_SE_SY_HITPOINT_ALARM,
    NA_SE_SY_ERROR,          NA_SE_SY_TIMESIGNAL_BELL, NA_SE_VO_NAVY_HELLO,   NA_SE_EN_STALKIDS_LAUGH,
    NA_SE_EN_PO_LAUGH,       NA_SE_EN_WIZ_LAUGH,     NA_SE_EN_REDEAD_CRY,     NA_SE_EN_GOLON_SNORE1,
    NA_SE_EV_CHICKEN_CRY_M,  NA_SE_EV_COW_CRY,       NA_SE_EV_HORSE_NEIGH,    NA_SE_EV_DOG_CRY_EVENING,
    NA_SE_IT_BOMB_EXPLOSION,
};
static s32 sPrankSfxId = -1;
static s32 sPrankReps = 0;    // plays left (short sounds repeat a few times)
static s32 sPrankGap = 0;     // frames to the next one
static s32 sBgmDucked = 0;    // frames the music stays quiet so the sound is heard

// Prank sounds: the music dips for a moment, and short ones play a few times.
static void Prank_Sound(s32 k) {
    u16 sfx = sPrankSfx[k];

    sPrankSfxId = sfx;
    sPrankReps = (sfx == NA_SE_SY_HITPOINT_ALARM) ? 6 : ((k >= 6) && (k <= 15)) ? 2 : 1;
    sPrankGap = 0;
    if (sBgmDucked == 0) {
        Audio_SetMainBgmVolume(0x28, 3);
    }
    sBgmDucked = 3 * FPS;
}

static void Prank_SoundUpdate(void) {
    if ((sPrankReps > 0) && (--sPrankGap <= 0)) {
        Audio_PlaySfx((u16)sPrankSfxId);
        sPrankReps--;
        sPrankGap = (sPrankSfxId == NA_SE_SY_HITPOINT_ALARM) ? 8 : 16;
    }
    if ((sBgmDucked > 0) && (--sBgmDucked == 0)) {
        Audio_SetMainBgmVolume(0x7F, 10);
    }
}

// --- Pranks with something to see -------------------------------------------------

#define LINGER_FRAMES (30 * FPS)
static s32 sDiscoTimer = 0;
static s32 sConfettiTimer = 0;

typedef struct {
    Vec3f pos;
    s32 timer;
} Beam;
static Beam sBeams[4];

// A friend's "look here": a column of light at their mark, a few seconds.
static void Beam_Add(PlayState* play, f32 x, f32 z);
static void Beam_Update(PlayState* play);

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

// "id|type|..." -> "type|..."
static const char* Link_Strip(const char* line) {
    while ((*line != '\0') && (*line != '|')) {
        line++;
    }
    return (*line == '|') ? line + 1 : line;
}

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
    if (sFriendNow == EV_QUIZ) {
        sFQuizCount = 0; // a friend's quiz that never happened takes its questions with it
    }
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
    // (3.1.8) The same event twice: one run, twice as fierce, for the events that allow it.
    if ((combo == ev) && Ev_CanDouble(ev)) {
        return Link_EventOk(ev) && gOpt[ID_EV_COMBOS] && gOpt[ID_EV_ON_FIRST + ev];
    }
    return Link_EventOk(combo) && (combo != ev) && gOpt[ID_EV_COMBOS] && gOpt[ID_EV_ON_FIRST + combo] &&
           Combo_CanBePrimary(ev) && Combo_CanBeSecondFriend(combo, ev);
}

// What a Friend Shield stops: anything that makes your life harder.
static s32 Link_Harmful(const char* type) {
    static const char* sMean[] = { "ev_now",    "ev_next",   "ev_soon",  "nem_make", "nem_trait", "nem_level",
                                   "nem_hunt",  "draft",     "quiz",     "bounty",   "ambush",
                                   "tax",       "airstrike", "wanted",   "ev_extend", "empower",
                                   "blackout",  "surprise", "place",    "steer",   "forced_ambush" };
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
enum { FMODE_BLOCKED, FMODE_HELPFUL, FMODE_FULL, FMODE_EXP }; // EXP: Full plus the Experimental tab

typedef struct {
    char name[6];
    u8 mode;
    u8 online;
    u32 nemDied; // when their last Nemesis died (0: never had one): the oldest gets the next
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
        n = Ev_Append(buf, n, (sFr[k].mode == FMODE_BLOCKED) ? ":0" : (sFr[k].mode == FMODE_EXP) ? ":3" : ":1", sizeof(buf));
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
    sFr[k].nemDied = 0;
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
                sFr[k].mode = (buf[i + 1] == '0') ? FMODE_BLOCKED : (buf[i + 1] == '3') ? FMODE_EXP : FMODE_HELPFUL;
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

// Menu rows: only friends who are here right now. (Offline friends with a
// Blocked or Helpful setting stay remembered, hidden, until they come back.)
static s32 Fr_Row(s32 row) {
    s32 k;

    for (k = 0; k < sFrCount; k++) {
        if (sFr[k].online && (row-- == 0)) {
            return k;
        }
    }
    return -1;
}

s32 Link_FriendRows(void) {
    return MIN(Link_FriendsOnline(), ID_LNK_FR_LAST - ID_LNK_FR_FIRST + 1);
}

static const char* Link_FriendLabel(s32 row) {
    static char sLabel[24];
    s32 k = Fr_Row(row);

    if (k < 0) {
        return "";
    }
    Ev_Append(sLabel, 0, sFr[k].name, sizeof(sLabel));
    return sLabel;
}

static const char* Link_FriendValue(s32 row, char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    static const char* sWords[4] = { "Blocked", "Helpful", "Full", "Experimental" };
    s32 k = Fr_Row(row);
    s32 m = (k >= 0) ? sFr[k].mode : FMODE_FULL;

    Ev_Append(out, 0, sWords[m], maxLen);
    if (m == FMODE_BLOCKED) {
        *r = 255, *g = 70, *b = 70;
    } else if (m == FMODE_HELPFUL) {
        *r = 110, *g = 230, *b = 140;
    } else if (m == FMODE_EXP) {
        *r = 200, *g = 110, *b = 255;
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
    // Full -> Experimental -> Helpful -> Blocked -> Full
    sFr[k].mode = (sFr[k].mode == FMODE_FULL)    ? FMODE_EXP
                  : (sFr[k].mode == FMODE_EXP)     ? FMODE_HELPFUL
                  : (sFr[k].mode == FMODE_HELPFUL) ? FMODE_BLOCKED
                                                   : FMODE_FULL;
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
                          : (sFr[k].mode == FMODE_EXP)     ? " joined (Experimental)"
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
    // Forget friends who left with nothing special set (their setting was Full anyway).
    for (k = 0, i = 0; k < sFrCount; k++) {
        if (sFr[k].online || (sFr[k].mode != FMODE_FULL)) {
            sFr[i++] = sFr[k];
        }
    }
    sFrCount = i;
}

// --- Friends' Nemeses (3.1.8) ------------------------------------------------
// The first friend here steers the default Nemesis (slot 0). Every other friend
// here can have one of their own (slots 1-3). When one is free, it goes to the
// friend whose Nemesis died longest ago. A friend who leaves keeps theirs.

static u32 sNemDeathSeq = 0;
static s32 sNemGrantTimer = 0;

static void Fr_NemDied(const char* owner) {
    s32 k = Fr_Find(owner);

    if (k >= 0) {
        sFr[k].nemDied = ++sNemDeathSeq;
    }
}

static s32 Fr_FirstOnline(void) {
    s32 k;

    for (k = 0; k < sFrCount; k++) {
        if (sFr[k].online) {
            return k;
        }
    }
    return -1;
}

// The Nemesis a friend's panel moves go to (-1: they have none).
static s32 Nem_SlotForFriend(const char* name) {
    s32 i;
    s32 first;

    if (name[0] == '\0') {
        return 0;
    }
    for (i = 1; i < NEM_MAX; i++) {
        if ((sNems[i].pool >= 0) && Str_Eq(sNems[i].owner, name)) {
            return i;
        }
    }
    first = Fr_FirstOnline();
    if ((first < 0) || Str_Eq(sFr[first].name, name)) {
        return 0;
    }
    return -1;
}

// A free slot for a friend's Nemesis (-1: all taken).
static s32 Nem_FreeSlot(void) {
    s32 i;

    for (i = 1; i < NEM_MAX; i++) {
        if (sNems[i].pool < 0) {
            return i;
        }
    }
    return -1;
}

// Once a second: hands out friends' Nemeses (one a minute at most).
static void Nem_FriendTick(PlayState* play) {
    s32 first = Fr_FirstOnline();
    s32 best = -1;
    s32 slot = -1;
    s32 k;
    s32 i;

    if (sNemGrantTimer > 0) {
        sNemGrantTimer--;
        return;
    }
    if (!V2_ON(ID_V2_NEM) || !V2_ON(ID_V2_LINK) || (Mut_SlotNow() < 1) || sNemBanished || (sActive >= 0) || (first < 0)) {
        return;
    }
    for (k = 0; k < sFrCount; k++) {
        if (!sFr[k].online || (k == first) || ((sFr[k].mode != FMODE_FULL) && (sFr[k].mode != FMODE_EXP)) ||
            (Nem_SlotForFriend(sFr[k].name) >= 0)) {
            continue;
        }
        if ((best < 0) || (sFr[k].nemDied < sFr[best].nemDied)) {
            best = k;
        }
    }
    if (best < 0) {
        return;
    }
    // A loaded one with no owner (owners aren't saved) is adopted first.
    for (i = 1; i < NEM_MAX; i++) {
        if ((sNems[i].pool >= 0) && (sNems[i].owner[0] == '\0')) {
            Ev_Append(sNems[i].owner, 0, sFr[best].name, sizeof(sNems[i].owner));
            sNemGrantTimer = 60;
            return;
        }
    }
    slot = Nem_FreeSlot();
    if (slot < 0) {
        return;
    }
    sNemCur = slot;
    sNems[slot].owner[0] = '\0';
    Nem_Promote(play, sHuntPool[V2_Rand(ARRAY_COUNT(sHuntPool))], NULL);
    if (sNem.pool >= 0) {
        s32 here = V2_ZoneNow(play);
        s32 far = V2_Rand(ZONE_OUTDOOR);

        if (far == Zone_Outdoor(here)) {
            far = (far + 7) % ZONE_OUTDOOR;
        }
        sNem.zone = far;
        bzero(&sNem.body, sizeof(sNem.body));
        Ev_Append(sNem.owner, 0, sFr[best].name, sizeof(sNem.owner));
        Nem_Note(" hunts you now.", "");
    }
    sNemCur = 0;
    sNemGrantTimer = 60;
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

// (3.1.8) A friend's wager on the next event locks it until it starts (-1: none).
static s32 sBetLock = -1;

static s32 Link_BetLocked(void) {
    return (sBetLock >= 0) && (sActive < 0);
}

#define SURPRISE_MAX 8
enum { SUR_EVENT, SUR_AMBUSH, SUR_BOMB, SUR_RUPOORS, SUR_NEMESIS, SUR_KINDS };

typedef struct {
    s32 id;      // the command (refunded if it fizzles)
    s8 zone;
    u8 kind;
    s16 arg;     // event or enemy
    u8 hasPos;   // the friend picked a spot or an area on the map
    u8 ready;    // placed in the area you're in
    Vec3f pos;   // where it goes off (the spot, or where you stepped in)
    f32 x0, z0, x1, z1; // (3.1.8) the area it covers: walk in and it goes off
    char from[6];
} Surprise;

static Surprise sSur[SURPRISE_MAX];
static s32 sSurCount = 0;
static s32 sSurScene = -1;

// (3.1.8) A low see-through purple wall along the edges of surprise areas, only when you're
// within SUR_WALL_NEAR of an edge (fading in as you get closer). Ground heights along the
// edges come from floor raycasts every SUR_WALL_STEP units, cached per scene.
#define SUR_WALL_MAX 64
#define SUR_WALL_NEAR 320.0f // (3.1.10: was 150, too close to see the box coming)
#define SUR_WALL_STEP 100.0f
#define SUR_WALL_SPAN 450.0f // how far along an edge (each way) the wall shows
#define SUR_GCACHE 128
#define SUR_EXIT_CLEAR 200.0f // never goes off this close to a door or loading zone
typedef struct {
    Vec3f a;
    Vec3f b;
    u8 alpha;
} SurWall;
static SurWall sSurWall[SUR_WALL_MAX];
static s32 sSurWallCount = 0;
static struct {
    s32 x;
    s32 z;
    f32 y;
    u8 state; // 0 empty, 1 ground, 2 none
} sSurG[SUR_GCACHE];
static s32 sSurGNext = 0;
static s32 sSurGNew = 0; // raycasts left this frame

// A spot becomes a small area around it.
static void Sur_AreaAround(Surprise* s, f32 half) {
    s->x0 = s->pos.x - half;
    s->x1 = s->pos.x + half;
    s->z0 = s->pos.z - half;
    s->z1 = s->pos.z + half;
}

static s32 Sur_Inside(Surprise* s, Vec3f* p) {
    return (p->x >= s->x0) && (p->x <= s->x1) && (p->z >= s->z0) && (p->z <= s->z1);
}

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

// (3.1.10) A friend's ambush happens once. Its enemies are remembered; once one is
// beaten it gets 2 seconds to finish dying, then it's gone for good. Some kinds
// (Chuchus, Stalchildren, Floormasters) come back on their own otherwise. They
// never split either.
#define FAMB_MAX 24
static Actor* sFAmb[FAMB_MAX];
static u8 sFAmbDead[FAMB_MAX];

static Actor* FAmb_Spawn(PlayState* play, s32 pool, Vec3f* at, f32 minD, f32 maxD) {
    Actor* e = V2_SpawnEnemy(play, pool, at, minD, maxD, TAG_V2);
    TagData* t;
    s32 k;

    if (e == NULL) {
        return NULL;
    }
    t = Tag_Get(e);
    if (t != NULL) {
        t->v2flags |= TAGF_V2_SPLIT;
    }
    for (k = 0; k < FAMB_MAX; k++) {
        if (sFAmb[k] == NULL) {
            sFAmb[k] = e;
            sFAmbDead[k] = 0;
            break;
        }
    }
    return e;
}

static void FAmb_Update(PlayState* play, s32 playing) {
    s32 k;

    // sFAmbDead: 0 not seen with health yet (some kinds start at 0), 1 alive, 2+ beaten (counting up).
    for (k = 0; k < FAMB_MAX; k++) {
        Actor* e = sFAmb[k];

        if (e == NULL) {
            continue;
        }
        if (!V2_IsLive(play, e) || (e->update == NULL) || (Tag_Of(e) != TAG_V2)) {
            sFAmb[k] = NULL; // (gone, or the slot now holds something else)
            continue;
        }
        if (sFAmbDead[k] == 0) {
            if (e->colChkInfo.health > 0) {
                sFAmbDead[k] = 1;
            }
        } else if ((sFAmbDead[k] == 1) && (e->colChkInfo.health == 0)) {
            sFAmbDead[k] = 2;
        } else if ((sFAmbDead[k] >= 2) && playing && (++sFAmbDead[k] >= 2 * FPS + 2)) {
            // (once beaten it stays beaten, even if it heals itself back up)
            Ev_Poof(play, &e->world.pos, 1);
            Actor_Kill(e);
            sFAmb[k] = NULL;
        }
    }
}

// Sets off a surprise. Returns false if it has to wait (an event is already queued).
static s32 Sur_Fire(PlayState* play, Surprise* s) {
    Player* player = GET_PLAYER(play);
    s32 ok = true;
    s32 i;

    Ev_Append(sLinkFrom, 0, s->from, sizeof(sLinkFrom));
    switch (s->kind) {
        case SUR_EVENT:
            if (Link_BetLocked()) {
                return false; // waits: a wager is on the next event
            }
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
                    (FAmb_Spawn(play, s->arg, &at, 0.0f, 0.0f) != NULL)) {
                    made++;
                } else if (FAmb_Spawn(play, s->arg, NULL, 200.0f, 400.0f) != NULL) {
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
            sNemCur = MAX(Nem_SlotForFriend(s->from), 0); // (the planter's own Nemesis)
            if (!V2_ON(ID_V2_NEM) || (sNem.pool < 0)) {
                ok = false;
            } else if ((sNem.actor == NULL) || !V2_IsLive(play, sNem.actor)) {
                ok = Link_TrySpawnNemNear(play);
                if (!ok) {
                    sNemHuntTimer = 15 * FPS;
                    sNemHuntSlot = sNemCur;
                    ok = true;
                }
            }
            sNemCur = 0;
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

// Cached ground height at an edge point (false: none, or not sampled yet this frame).
static s32 Sur_Ground(PlayState* play, f32 x, f32 z, f32* y) {
    s32 ix = (s32)x;
    s32 iz = (s32)z;
    s32 k;
    Vec3f at;

    for (k = 0; k < SUR_GCACHE; k++) {
        if ((sSurG[k].state != 0) && (sSurG[k].x == ix) && (sSurG[k].z == iz)) {
            *y = sSurG[k].y;
            return sSurG[k].state == 1;
        }
    }
    if (sSurGNew <= 0) {
        return false;
    }
    sSurGNew--;
    k = sSurGNext;
    sSurGNext = (sSurGNext + 1) % SUR_GCACHE;
    sSurG[k].x = ix;
    sSurG[k].z = iz;
    sSurG[k].state = Link_GroundAt(play, (f32)ix, (f32)iz, &at) ? 1 : 2;
    sSurG[k].y = at.y;
    *y = at.y;
    return sSurG[k].state == 1;
}

// The wall pieces near you, rebuilt every frame (cheap: heights are cached).
static void Sur_Walls(PlayState* play, s32 zone) {
    Player* player = GET_PLAYER(play);
    Vec3f* me = &player->actor.world.pos;
    s32 i;

    sSurWallCount = 0;
    sSurGNew = 10;
    for (i = 0; i < sSurCount; i++) {
        Surprise* s = &sSur[i];
        s32 e;

        if ((zone < 0) || (s->zone != zone) || !s->ready || !(s->x1 > s->x0)) {
            continue;
        }
        for (e = 0; e < 4; e++) {
            // Edges: 0 north (z0), 1 south (z1), 2 west (x0), 3 east (x1).
            s32 alongX = (e < 2);
            f32 fixed = (e == 0) ? s->z0 : (e == 1) ? s->z1 : (e == 2) ? s->x0 : s->x1;
            f32 lo = alongX ? s->x0 : s->z0;
            f32 hi = alongX ? s->x1 : s->z1;
            f32 meAlong = alongX ? me->x : me->z;
            f32 across = fabsf((alongX ? me->z : me->x) - fixed);
            f32 off = (meAlong < lo) ? lo - meAlong : (meAlong > hi) ? meAlong - hi : 0.0f;
            f32 d = sqrtf(SQ(across) + SQ(off));
            f32 near;
            f32 t;
            f32 t0;
            f32 t1;
            f32 lastY = me->y;
            s32 steps;

            if (d > SUR_WALL_NEAR) {
                continue;
            }
            near = 1.0f - d / SUR_WALL_NEAR;
            t0 = MAX(lo, meAlong - SUR_WALL_SPAN);
            t1 = MIN(hi, meAlong + SUR_WALL_SPAN);
            // (3.1.9) The step always moves forward. Before, an edge on a negative multiple of 100
            // (x = -1200, say) gave the same point back as "next", and the loop never ended: the
            // game froze as soon as you came near that rectangle.
            for (t = t0, steps = 0; (t < t1) && (sSurWallCount < SUR_WALL_MAX) && (steps < 24); steps++) {
                s32 q = (s32)(t / SUR_WALL_STEP);
                f32 tn;

                if ((f32)q * SUR_WALL_STEP > t) {
                    q--; // (floor for negatives)
                }
                tn = (f32)(q + 1) * SUR_WALL_STEP;
                if (!(tn > t)) {
                    tn = t + SUR_WALL_STEP;
                }
                tn = MIN(t1, tn);
                f32 ya;
                f32 yb;
                f32 mid = (t + tn) * 0.5f;
                f32 fade = 1.0f - fabsf(mid - meAlong) / SUR_WALL_SPAN;
                SurWall* w = &sSurWall[sSurWallCount];

                // (3.1.10) A point with no floor (a wall, a pit, or not sampled yet) borrows the
                // last good height on this edge, else Link's, so the box has no holes.
                if (!Sur_Ground(play, alongX ? t : fixed, alongX ? fixed : t, &ya) || !(fabsf(ya - me->y) < 400.0f)) {
                    ya = lastY;
                }
                if (!Sur_Ground(play, alongX ? tn : fixed, alongX ? fixed : tn, &yb) || !(fabsf(yb - me->y) < 400.0f)) {
                    yb = ya;
                }
                if (fabsf(ya - yb) >= 150.0f) {
                    yb = ya; // (a ledge: keep the piece level instead of dropping it)
                }
                lastY = yb;
                {
                    w->a.x = alongX ? t : fixed;
                    w->a.z = alongX ? fixed : t;
                    w->a.y = ya;
                    w->b.x = alongX ? tn : fixed;
                    w->b.z = alongX ? fixed : tn;
                    w->b.y = yb;
                    w->alpha = (u8)(150.0f * near * CLAMP(fade, 0.0f, 1.0f));
                    if (w->alpha > 4) {
                        sSurWallCount++;
                    }
                }
                t = tn;
            }
        }
    }
}

// Every frame while commands can land: settle, shimmer, and spring the trap.
static void Sur_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 zone = V2_ZoneNow(play);
    s32 i;

    if ((sActive >= 0) || ((sBetLock >= 0) && ((play->gameplayFrames % 10) == 0) && (Ev_PreviewNext(play) != sBetLock))) {
        sBetLock = -1; // it started (the worker settles it), or it can't happen now (the worker refunds)
    }
    if (player == NULL) {
        return;
    }
    if (play->sceneId != sSurScene) {
        sSurScene = play->sceneId;
        sSurWallCount = 0;
        bzero(sBeams, sizeof(sBeams)); // (a friend's beam belongs to the area it was sent in)
        bzero(sSurG, sizeof(sSurG));
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
            if (s->x1 > s->x0) {
                s->ready = sAreaTimer > FPS; // an area: nothing to settle
            } else if ((sAreaTimer > FPS) && ((sAreaTimer % 10) == 0) && Sur_Settle(play, s)) {
                Sur_AreaAround(s, 140.0f); // (no spot picked: a random one, a little area around it)
                s->ready = true;
            }
            continue;
        }
        // Walked in (and not right at a door you just came through): it goes off where you are.
        if (Sur_Inside(s, &player->actor.world.pos) && (World_ExitDist(&player->actor.world.pos) >= SUR_EXIT_CLEAR)) {
            s->pos = player->actor.world.pos;
            if (Sur_Fire(play, s)) {
                Sur_Remove(i);
                sSurWallCount = 0;
                return;
            }
        }
    }
    Sur_Walls(play, zone);
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
    s32 wasQuiz = false;
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
    // Experimental friends: the sandbox ("x|op|0|0|k:v,...").
    if (Str_Eq(type, "x")) {
        if (Fr_Mode(sLinkFrom) != FMODE_EXP) {
            Link_Fail(sLinkFrom, " isn't Experimental");
        } else {
            const char* what = Exp_Run(play, a, text);

            if (what == NULL) {
                Link_Fail("unknown Experimental move", "");
            } else if (what[0] == '!') {
                Link_Fail("EXP: ", what + 1); // (can't happen there: the friend's page shows why)
            } else {
                Link_Result("EXP: ", what);
            }
        }
        return true;
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
        if (Str_Eq(type, "quiz")) {
            sFQuizCount = 0; // (their questions go with it)
        }
        return true;
    }

    // A friend's own Pop Quiz: "quizq" lines carry the questions, then "quiz" starts it.
    if (Str_Eq(type, "quizq")) {
        if ((a >= 0) && (a < 4)) {
            Ev_Append(sFQuiz[a], 0, text, sizeof(sFQuiz[a]));
        }
        return true;
    }
    if (Str_Eq(type, "quiz")) {
        sFQuizCount = CLAMP(a, 0, 4);
        Ev_Append(sFQuizFrom, 0, sLinkFrom, sizeof(sFQuizFrom));
        type = "ev_now";
        a = EV_QUIZ;
        wasQuiz = true;
    }
    if (Str_Eq(type, "ev_now") && Link_EventOk(a)) {
        if (Link_BetLocked()) {
            Link_Fail("a wager locked the next event", "");
        } else if (!gOpt[ID_EV_MASTER]) {
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
        } else if (Link_BetLocked()) {
            Link_Fail("a wager locked the next event", "");
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
    } else if (Str_Eq(type, "betlock") && Link_EventOk(a)) {
        // (3.1.8) A friend wagered on the next event: it stays the next one (never shown to you).
        if ((sActive >= 0) || (sRouletteTimer > 0) || (Ev_PreviewNext(play) != a)) {
            Link_Fail("the next event changed", "");
        } else {
            sBetLock = a;
            Link_Result("Next event locked: ", Events_ShortName(a));
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
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        if (ns < 0) {
            ns = Nem_FreeSlot(); // a friend with none of their own makes one
        }
        if (!V2_ON(ID_V2_NEM)) {
            Link_Fail("Nemesis is off", "");
        } else if (ns < 0) {
            Link_Fail("every Nemesis slot is taken", "");
        } else if (sNems[ns].pool >= 0) {
            Link_Fail("they already have one", "");
        } else {
            sNemCur = ns;
            sNem.owner[0] = '\0';
            Nem_Promote(play, a, NULL);
            if ((ns > 0) && (sNem.pool >= 0)) {
                Ev_Append(sNem.owner, 0, sLinkFrom, sizeof(sNem.owner));
            }
            sNemCur = 0;
            Link_Result("Created a Nemesis: ", Pool_Name(a));
        }
    } else if ((Str_Eq(type, "nem_trait") || Str_Eq(type, "nem_pick")) && (a >= 0) && (a < TR_COUNT) && (a != TR_RETIRED)) {
        // nem_trait: a paid extra trait. nem_pick: the trait it earned (every 5 levels).
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (Nem_Traits() & (1 << a)) {
            Link_Fail("it already has ", sTraitNames[a]);
        } else if (Str_Eq(type, "nem_pick") && (sNem.picks == 0)) {
            Link_Fail("no trait pick waiting", "");
        } else {
            if (Str_Eq(type, "nem_pick")) {
                sNem.picks--;
                if (sNem.picks == 0) {
                    sNem.pickTimer = 0;
                }
            }
            Nem_AddTraitId(a);
            Nem_RefreshLive(play);
            Link_Result("Nemesis trait: ", sTraitNames[a]);
            // (3.1.9) no pop-up: the new trait shows on the Nemesis page
        }
        sNemCur = 0;
    } else if (Str_Eq(type, "nem_level")) {
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (sNem.level >= NEM_LEVEL_MAX) {
            Link_Fail("it's at max level", "");
        } else {
            Nem_GainPoints(1, " got a skill point from a friend:");
            Link_Result("Nemesis +1 skill point", "");
        }
        sNemCur = 0;
    } else if (Str_Eq(type, "nem_stat") && (a >= 0) && (a < NST_COUNT)) {
        // Spending its unspent points: a stat and how many.
        s32 ns = Nem_SlotForFriend(sLinkFrom);
        s32 put;

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else if (sNem.points == 0) {
            Link_Fail("no skill points to spend", "");
        } else if ((put = Nem_Spend(a, CLAMP(b, 1, 50))) == 0) {
            Link_Fail("that stat is maxed", "");
        } else {
            s32 u;

            (void)u; // (3.1.9) what it learns shows on the Nemesis page, no pop-up
            Link_Result("Trained ", sNemStatNames[a]);
        }
        sNemCur = 0;
    } else if (Str_Eq(type, "nem_hunt")) {
        Player* player = GET_PLAYER(play);
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
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
            sNemHuntSlot = sNemCur;
            Link_Result("Nemesis is on its way", "");
            Link_Say("Your Nemesis is coming for you!", "");
        }
        sNemCur = 0;
    } else if (Str_Eq(type, "draft") && (a >= 0) && (a < PICK_COUNT) && (b >= 0) && (b < PICK_COUNT)) {
        // a and b are picks: a mutator id, or MUT_COUNT + a curse id.
        if (!gOpt[ID_V2_MUT]) {
            Link_Fail("the Moon Draft is off", "");
        } else if (!Pick_Usable(a) || !Pick_Usable(b)) {
            Link_Fail("they can't be offered that", "");
        } else if ((a == b) || (sMutCount >= 6) || (Pick_Has(a) && Pick_Has(b))) {
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
                    e = FAmb_Spawn(play, a, &spot, 0.0f, 0.0f);
                }
            }
            if (e == NULL) {
                e = FAmb_Spawn(play, a, NULL, 200.0f, 450.0f);
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
    } else if (Str_Eq(type, "gift")) {
        sMarks = MIN(sMarks + 10, MARKS_MAX);
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
        Prank_Sound(a); // no message: that's the joke
        Link_Result("Played a sound", "");
    } else if (Str_Eq(type, "cuccos")) {
        Player* player = GET_PLAYER(play);
        s32 i;
        s32 made = 0;

        for (i = 0; (player != NULL) && (i < 6); i++) {
            Vec3f spot;
            Actor* cucco;

            if (!Ev_FindSpot(play, 80.0f, 220.0f, (s16)(i * 0x2AAA), 0x1800, true, &spot)) {
                continue;
            }
            cucco = Ev_Spawn(play, ACTOR_EN_NIW, OBJECT_NIW, &spot, 0, (s16)(Rand_ZeroOne() * 0xFFFF), 0,
                             (s16)0xFFFF, TAG_V2);
            if (cucco != NULL) {
                Linger_Add(cucco, LINGER_FRAMES);
                made++;
            }
        }
        if (made > 0) {
            Audio_PlaySfx(NA_SE_EV_CHICKEN_CRY_M);
            Link_Result("Cucco party", "");
            Link_Say("Your friend threw a cucco party. Don't hit them.", "");
        } else {
            Link_Fail("no room for cuccos", "");
        }
    } else if (Str_Eq(type, "disco")) {
        sDiscoTimer = 15 * FPS;
        Link_Result("Disco", "");
        Link_Say("Your friend turned on the disco lights!", "");
    } else if (Str_Eq(type, "confetti")) {
        sConfettiTimer = 3 * FPS;
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
        Link_Result("Confetti", "");
        Link_Say("Your friend is celebrating you!", "");
    } else if (Str_Eq(type, "beam")) {
        // Free and quiet: a friend pointing at something.
        Beam_Add(play, (f32)a, (f32)b);
    } else if (Str_Eq(type, "forced_ambush") && Link_PoolOk(a) && (b >= 3) && (b <= 6)) {
        // Pulled out of any menu, surrounded.
        s32 i;
        s32 made = 0;

        for (i = 0; i < b; i++) {
            Vec3f spot;
            Actor* e = NULL;

            if (Ev_FindSpot(play, 180.0f, 320.0f, (s16)(i * (0x10000 / b)), 0x1800, true, &spot)) {
                e = FAmb_Spawn(play, a, &spot, 0.0f, 0.0f);
            }
            if (e == NULL) {
                e = FAmb_Spawn(play, a, NULL, 200.0f, 400.0f);
            }
            if (e != NULL) {
                Ev_Poof(play, &e->world.pos, 1);
                made++;
            }
        }
        if (made > 0) {
            V2_Banner("FORCED AMBUSH", Pool_Name(a));
            Audio_PlaySfx(NA_SE_SY_FOUND);
            Link_Result("Forced ambush: ", Pool_Name(a));
            Link_Say("Your friend dragged you into an ambush!", "");
        } else {
            Link_Fail("no room for an ambush", "");
        }
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
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else {
            s32 k;

            for (k = 0; (text[k] != '\0') && (k < 12); k++) {
                sNemCustom[k] = ((text[k] == '|') || (text[k] == ';') || (text[k] == ',')) ? ' ' : text[k];
            }
            sNemCustom[k] = '\0';
            Link_Result("Nemesis renamed: ", sNemCustom); // quietly: they find out when they meet it
        }
        sNemCur = 0;
    } else if (Str_Eq(type, "nem_purge")) {
        s32 ns = Nem_SlotForFriend(sLinkFrom);

        sNemCur = MAX(ns, 0);
        if (!V2_ON(ID_V2_NEM) || (ns < 0) || (sNem.pool < 0)) {
            Link_Fail("no Nemesis", "");
        } else {
            if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
                Ev_Poof(play, &sNem.actor->world.pos, 1);
                Ev_KillOne(play, sNem.actor);
            }
            if (sNem.owner[0] != '\0') {
                Fr_NemDied(sNem.owner);
            }
            Nem_Reset();
            sNem.owner[0] = '\0';
            if (ns == 0) {
                sNemRestSlot = Mut_SlotNow(); // the next one has to be crowned, not handed out on the spot
            }
            Link_Result("Nemesis purged", "");
            Link_Say("Friend purged your Nemesis.", "");
        }
        sNemCur = 0;
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

        static s32 sSurSeen[16];
        static s32 sSurSeenNext = 0;
        s32 dup = false;
        s32 k;

        // (3.1.10) The same surprise is only ever planted once.
        for (k = 0; k < 16; k++) {
            dup |= (sLinkCurId > 0) && (sSurSeen[k] == sLinkCurId);
        }
        if (dup) {
            return false;
        }
        sSurSeen[sSurSeenNext] = sLinkCurId;
        sSurSeenNext = (sSurSeenNext + 1) % 16;
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
            s32 used = Link_ParseXZ(text, &x, &z);
            s32 x2 = 0;
            s32 z2 = 0;

            // (3.1.9) Every corner inside the world, whatever the panel sent.
            x = CLAMP(x, -32000, 32000);
            z = CLAMP(z, -32000, 32000);
            sp->hasPos = used > 0;
            sp->pos.x = (f32)x;
            sp->pos.y = 0.0f;
            sp->pos.z = (f32)z;
            sp->x0 = sp->x1 = sp->z0 = sp->z1 = 0.0f; // (no area yet: set once placed)
            Ev_Append(sp->from, 0, sLinkFrom, sizeof(sp->from));
            if (!sp->hasPos) {
                sp->pos.x = sp->pos.z = 0.0f;
            } else if (Link_ParseXZ(text + used, &x2, &z2) > 0) {
                x2 = CLAMP(x2, -32000, 32000);
                z2 = CLAMP(z2, -32000, 32000);
                // Two corners: the area the friend dragged out, any size (at least a few steps across).
                sp->x0 = (f32)MIN(x, x2);
                sp->x1 = (f32)MAX(MAX(x, x2), MIN(x, x2) + 120);
                sp->z0 = (f32)MIN(z, z2);
                sp->z1 = (f32)MAX(MAX(z, z2), MIN(z, z2) + 120);
                sp->pos.x = (sp->x0 + sp->x1) * 0.5f;
                sp->pos.z = (sp->z0 + sp->z1) * 0.5f;
            } else {
                Sur_AreaAround(sp, 140.0f); // one spot: a small area around it
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
    } else {
        Link_Fail("can't do that", ""); // an old or bad command: refunded
    }
    if (wasQuiz && !EV_RUNNING(EV_QUIZ) && (sFriendNow != EV_QUIZ)) {
        sFQuizCount = 0; // the quiz didn't happen: their questions don't wait for a later one
    }
    return true;
}

// --- Archipelago room (typed in by the player, for the friends' heatmap) ---------------

static char sApAddr[80] = "";
static char sApSlot[40] = "";
static s32 sApLoaded = false;
static s32 sApWanted = false;
static RecompuiContext sApCtx;
static RecompuiResource sApAddrIn;
static RecompuiResource sApSlotIn;
static s32 sApMade = false;
static volatile s32 sApOpen = false;
static volatile s32 sApClickSave = false;
static volatile s32 sApClickClose = false;

static void Ap_Load(void) {
    char buf[124];
    s32 len = ger_fetch("ap_room", buf, sizeof(buf));
    s32 i = 0;
    s32 k = 0;

    sApLoaded = true;
    if (len <= 0) {
        return;
    }
    while ((i < len) && (buf[i] != '|') && (k < (s32)sizeof(sApAddr) - 1)) {
        sApAddr[k++] = buf[i++];
    }
    sApAddr[k] = '\0';
    k = 0;
    if (buf[i] == '|') {
        i++;
    }
    while ((i < len) && (k < (s32)sizeof(sApSlot) - 1)) {
        sApSlot[k++] = buf[i++];
    }
    sApSlot[k] = '\0';
}

static const char* Link_ApValue(char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    if (!sApLoaded) {
        Ap_Load();
    }
    if (sApSlot[0] == '\0') {
        Ev_Append(out, 0, "Not set", maxLen);
        *r = *g = *b = 150;
    } else {
        Ev_Append(out, 0, sApSlot, maxLen);
        *r = 120;
        *g = 220;
        *b = 255;
    }
    return out;
}

static void Link_ApOpen(void) {
    sApWanted = true;
}

static void ApUi_OnSave(RecompuiResource res, const RecompuiEventData* e, void* user) {
    if (e->type == UI_EVENT_CLICK) {
        sApClickSave = true;
    }
}

static void ApUi_OnClose(RecompuiResource res, const RecompuiEventData* e, void* user) {
    if (e->type == UI_EVENT_CLICK) {
        sApClickClose = true;
    }
}

static void ApUi_Make(void) {
    static const RecompuiColor sBg = { 12, 10, 28, 235 };
    static const RecompuiColor sFg = { 230, 225, 255, 255 };
    RecompuiResource root;
    RecompuiResource panel;
    RecompuiResource label;
    RecompuiResource save;
    RecompuiResource close;

    sApCtx = recompui_create_context();
    recompui_open_context(sApCtx);
    root = recompui_context_root(sApCtx);
    panel = recompui_create_element(sApCtx, root);
    recompui_set_position(panel, POSITION_ABSOLUTE);
    recompui_set_left(panel, 20.0f, UNIT_PERCENT);
    recompui_set_top(panel, 25.0f, UNIT_PERCENT);
    recompui_set_width(panel, 60.0f, UNIT_PERCENT);
    recompui_set_padding(panel, 24.0f, UNIT_DP);
    recompui_set_border_radius(panel, 12.0f, UNIT_DP);
    recompui_set_background_color(panel, &sBg);
    label = recompui_create_label(sApCtx, panel,
                                  "Your Archipelago room, for your friends' spoiler heatmap. Server address and port "
                                  "(like archipelago.gg:38281):",
                                  LABELSTYLE_NORMAL);
    recompui_set_color(label, &sFg);
    sApAddrIn = recompui_create_textinput(sApCtx, panel);
    recompui_set_width(sApAddrIn, 100.0f, UNIT_PERCENT);
    label = recompui_create_label(sApCtx, panel, "Your slot name:", LABELSTYLE_NORMAL);
    recompui_set_color(label, &sFg);
    sApSlotIn = recompui_create_textinput(sApCtx, panel);
    recompui_set_width(sApSlotIn, 100.0f, UNIT_PERCENT);
    save = recompui_create_button(sApCtx, panel, "Save", BUTTONSTYLE_PRIMARY);
    close = recompui_create_button(sApCtx, panel, "Cancel", BUTTONSTYLE_SECONDARY);
    recompui_register_callback(save, ApUi_OnSave, NULL);
    recompui_register_callback(close, ApUi_OnClose, NULL);
    recompui_close_context(sApCtx);
    sApMade = true;
}

static void ApUi_Close(PlayState* play) {
    recompui_hide_context(sApCtx);
    sApOpen = false;
    Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
}

// Every frame: opens the window once the menu has closed, and saves what was typed.
static void ApUi_Update(PlayState* play) {
    if (!sApLoaded) {
        Ap_Load();
    }
    if (sApWanted && !gMenuOpen) {
        sApWanted = false;
        if (!sApMade) {
            ApUi_Make();
        }
        recompui_open_context(sApCtx);
        recompui_set_input_text(sApAddrIn, sApAddr);
        recompui_set_input_text(sApSlotIn, sApSlot);
        recompui_close_context(sApCtx);
        recompui_set_context_captures_input(sApCtx, 1);
        recompui_set_context_captures_mouse(sApCtx, 1);
        recompui_show_context(sApCtx);
        sApOpen = true;
        sApClickSave = sApClickClose = false;
        Player_SetCsActionWithHaltedActors(play, NULL, PLAYER_CSACTION_WAIT);
    }
    if (!sApOpen) {
        return;
    }
    if (sApClickSave) {
        char* addr;
        char* slot;
        char both[124];
        s32 n;
        s32 k;
        s32 j = 0;

        sApClickSave = false;
        recompui_open_context(sApCtx);
        addr = recompui_get_input_text(sApAddrIn);
        slot = recompui_get_input_text(sApSlotIn);
        recompui_close_context(sApCtx);
        // Keep only what can go in a status line (no '|', no line breaks).
        for (k = 0; (addr != NULL) && (addr[k] != '\0') && (j < (s32)sizeof(sApAddr) - 1); k++) {
            if ((addr[k] > ' ') && (addr[k] != '|') && (addr[k] < 127)) {
                sApAddr[j++] = addr[k];
            }
        }
        sApAddr[j] = '\0';
        j = 0;
        for (k = 0; (slot != NULL) && (slot[k] != '\0') && (j < (s32)sizeof(sApSlot) - 1); k++) {
            if ((slot[k] >= ' ') && (slot[k] != '|') && (slot[k] < 127)) {
                sApSlot[j++] = slot[k];
            }
        }
        sApSlot[j] = '\0';
        if (addr != NULL) {
            recomp_free(addr);
        }
        if (slot != NULL) {
            recomp_free(slot);
        }
        n = Ev_Append(both, 0, sApAddr, sizeof(both));
        n = Ev_Append(both, n, "|", sizeof(both));
        Ev_Append(both, n, sApSlot, sizeof(both));
        ger_store("ap_room", both);
        Menu_Notify((sApSlot[0] != '\0') ? "Archipelago room saved for your friends." : "Archipelago room cleared.", NOTE_FRIEND);
        ApUi_Close(play);
        return;
    }
    if (CHECK_BTN_ANY(play->state.input[0].press.button, BTN_B | BTN_START)) {
        sApClickClose = true;
    }
    if (sApClickClose) {
        sApClickClose = false;
        ApUi_Close(play);
    }
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
    if (!playing || gMenuOpen || sSaveUiOpen || sApOpen) {
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
        t = Ev_Append(big, t, (sFr[k].mode == FMODE_BLOCKED)   ? ":0"
                      : (sFr[k].mode == FMODE_HELPFUL) ? ":1"
                      : (sFr[k].mode == FMODE_EXP)     ? ":3"
                                                       : ":2",
                      sizeof(big));
    }
    n = Link_Line(n, "fmodes", big);

    // A placement window: event, seconds left, how many spots (Pressure Seals has several).
    if (Place_Open()) {
        t = Ev_AppendNum(big, 0, sActive, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        t = Ev_AppendNum(big, t, (PLACE_WINDOW - sEventFrames + FPS - 1) / FPS, sizeof(big));
        t = Ev_Append(big, t, ":", sizeof(big));
        Ev_AppendNum(big, t, EV_RUNNING(EV_SEALS) ? V3_PlaceCount() : 1, sizeof(big));
        n = Link_Line(n, "evplace", big);
    }
    n = Link_LineNum(n, "steer", Steer_Kind());
    n = Link_Line(n, "ap", sApAddr);
    n = Link_Line(n, "apslot", sApSlot);

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
        if (Ev_IsDoubled()) {
            Ev_Append(tmp, t, " x2", sizeof(tmp));
        } else if ((sCombo >= 0) && (sCombo != sActive)) {
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
            s32 up = Ev_PreviewNext(play);

            n = Link_LineNum(n, "next", sUntilNext / FPS);
            // (3.1.8) What's coming, so friends can see it and wager on it before it starts.
            if (up >= 0) {
                t = Ev_Append(tmp, 0, Events_Name(up), sizeof(tmp));
                if ((sFriendNext == up) && (sFriendCombo >= 0)) {
                    t = Ev_Append(tmp, t, (sFriendCombo == up) ? " x2" : " + ", sizeof(tmp));
                    if (sFriendCombo != up) {
                        Ev_Append(tmp, t, Events_Name(sFriendCombo), sizeof(tmp));
                    }
                }
                n = Link_Line(n, "nextev", tmp);
                n = Link_LineNum(n, "nextid", up);
            }
        }
    }
    // Every curse picked this cycle, newest last.
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < sMutCount; k++) {
        if (Pick_IsCurse(sMutOrder[k])) {
            t = Ev_Append(tmp, t, (t > 0) ? ", " : "", sizeof(tmp));
            t = Ev_Append(tmp, t, Pick_Name(sMutOrder[k]), sizeof(tmp));
        }
    }
    n = Link_Line(n, "curse", !gOpt[ID_EV_CURSE] ? "Off" : (t > 0) ? tmp : "None");
    n = Link_LineNum(n, "marks", sMarks);
    n = Link_Line(n, "rank", sRankNames[Rank_Of(sBountiesDone)]);
    sNemCur = MAX(Nem_AnySlot(), 0);
    if (sNem.pool >= 0) {
        // The default (or first) Nemesis: name|type|level|area
        Nem_Name(tmp, sizeof(tmp));
        t = Ui_StrLen(tmp);
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_Append(tmp, t, Pool_Name(sNem.pool), sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        t = Ev_AppendNum(tmp, t, sNem.level, sizeof(tmp));
        t = Ev_Append(tmp, t, "|", sizeof(tmp));
        Ev_Append(tmp, t, sZoneNames[sNem.zone % ZONE_COUNT], sizeof(tmp));
        n = Link_Line(n, "nem", tmp);
    } else {
        n = Link_Line(n, "nem", "-");
    }
    n = Link_LineNum(n, "nz", (sNem.pool >= 0) ? sNem.zone % ZONE_COUNT : -1);
    sNemCur = 0;
    {
        // Every Nemesis, ";" between them:
        // slot|owner|name|type|level|points|health,power,speed,tracking|picks|auto-spend s|auto-pick s|
        // trait mask|unique mask|area|here (0/1)|times it beat you|area id
        static char sNemsText[640];
        s32 i;

        t = 0;
        sNemsText[0] = '\0';
        for (i = 0; i < NEM_MAX; i++) {
            char nm[24];

            sNemCur = i;
            if (sNem.pool < 0) {
                continue;
            }
            t = Ev_Append(sNemsText, t, (t > 0) ? ";" : "", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, i, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, sNem.owner, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            Nem_Name(nm, sizeof(nm));
            t = Ev_Append(sNemsText, t, nm, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, Pool_Name(sNem.pool), sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, sNem.level, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, sNem.points, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            for (k = 0; k < NST_COUNT; k++) {
                t = Ev_Append(sNemsText, t, (k > 0) ? "," : "", sizeof(sNemsText));
                t = Ev_AppendNum(sNemsText, t, Nem_Stat(k), sizeof(sNemsText));
            }
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, sNem.picks, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, (sNem.points > 0) ? (sNem.pointTimer + FPS - 1) / FPS : 0, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, (sNem.picks > 0) ? (sNem.pickTimer + FPS - 1) / FPS : 0, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, Nem_Traits(), sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, Nem_Uniques(), sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, sZoneNames[sNem.zone % ZONE_COUNT], sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) ? "1" : "0",
                          sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, sNem.wins, sizeof(sNemsText));
            t = Ev_Append(sNemsText, t, "|", sizeof(sNemsText));
            t = Ev_AppendNum(sNemsText, t, sNem.zone % ZONE_COUNT, sizeof(sNemsText));
        }
        sNemCur = 0;
        n = Link_Line(n, "nems", sNemsText);
        k = Fr_FirstOnline();
        n = Link_Line(n, "nemfirst", (k >= 0) ? sFr[k].name : ""); // whose moves steer the default Nemesis
    }
    t = 0;
    tmp[0] = '\0';
    for (k = 0; k < sMutCount; k++) {
        t = Ev_Append(tmp, t, (k > 0) ? "," : "", sizeof(tmp));
        t = Ev_Append(tmp, t, Pick_Name(sMutOrder[k]), sizeof(tmp));
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


static void Beam_Add(PlayState* play, f32 x, f32 z) {
    Vec3f at;
    s32 k;
    s32 pick = 0;

    if (!Link_GroundAt(play, x, z, &at)) {
        return;
    }
    for (k = 1; k < (s32)ARRAY_COUNT(sBeams); k++) {
        if (sBeams[k].timer < sBeams[pick].timer) {
            pick = k;
        }
    }
    sBeams[pick].pos = at;
    sBeams[pick].timer = 4 * FPS;
}

// Beams, the disco and confetti (every frame, even paused: they're only looks).
static void Beam_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 k;

    // (The beams themselves are drawn as solid pillars of light: Beam_Draw3D.)
    for (k = 0; k < (s32)ARRAY_COUNT(sBeams); k++) {
        if (sBeams[k].timer > 0) {
            sBeams[k].timer--;
        }
    }
    if ((sConfettiTimer > 0) && (player != NULL)) {
        Vec3f p = player->actor.world.pos;
        Vec3f cvel;
        Vec3f cacc = { 0.0f, -0.25f, 0.0f };
        Color_RGBA8 cprim = { 255, 255, 255, 255 };
        Color_RGBA8 cenv;

        sConfettiTimer--;
        for (k = 0; k < 3; k++) {
            HueToRgb((s32)(Rand_ZeroOne() * 360.0f), &cenv.r, &cenv.g, &cenv.b);
            cenv.a = 255;
            cvel.x = Rand_CenteredFloat(8.0f);
            cvel.y = 6.0f + Rand_ZeroOne() * 4.0f;
            cvel.z = Rand_CenteredFloat(8.0f);
            p.y = player->actor.world.pos.y + 60.0f;
            EffectSsKirakira_SpawnDispersed(play, &p, &cvel, &cacc, &cprim, &cenv, 3000, 40);
        }
    }
    if (sDiscoTimer > 0) {
        static const u8 sStyles[4] = { ENV_RED, ENV_COLD, ENV_ORANGE, ENV_DARK };

        sDiscoTimer--;
        if (sActive < 0) {
            if (sDiscoTimer > 0) {
                Ev_ApplyEnv(play, sStyles[(sDiscoTimer / 12) % 4], 0.6f);
            } else if (!sNightDark && (sFriendDarkTimer == 0)) {
                Ev_ClearEnv(play);
                Ev_RestoreLighting(play);
            }
        }
    }
}

static char sStash[128]; // a command that came in while they were in a menu (it waits there)
static s32 sStashed = false;
static s32 sForcedWarned = false; // "Get ready..." shown for the stashed Forced Ambush

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
    Exp_Update(play, playing);
    if ((sFriendSizeTimer > 0) && playing && (--sFriendSizeTimer == 0)) {
        gFriendLinkSize = 0;
    }
    if ((sFakeTimer > 0) && playing && gOpt[ID_EV_WARNSOUND] && Ev_ShowWarning() && ((sFakeTimer % FPS) == 0) &&
        (sFakeTimer < 5 * FPS)) {
        Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
    }
    Prank_SoundUpdate();
    Beam_Update(play);
    FAmb_Update(play, playing);
    ApUi_Update(play);
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
        if (playing) {
            Nem_FriendTick(play);
        }
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
    // or while he's going down. A Forced Ambush doesn't: it pulls him out of
    // a menu (one command is looked at; anything else waits in the stash).
    if (Link_HoldReason(play, playing)[0] != '\0') {
        s32 ocarina = (play->msgCtx.msgMode == MSGMODE_OCARINA_PLAYING);

        if ((gMenuOpen || sSaveUiOpen || ocarina || (play->pauseCtx.state != PAUSE_STATE_OFF)) &&
            (gSaveContext.save.saveInfo.playerData.health > 0) && !sQuizHolding && (sLinkGap == 0)) {
            if (!sStashed && (ger_poll(sStash, sizeof(sStash)) > 0)) {
                sStashed = true;
            }
            // The mutator draft is left alone: the ambush lands right after the pick.
            if (sStashed && Link_StartsWith(Link_Strip(sStash), "forced_ambush|") && !Menu_IsDraft()) {
                if (gMenuOpen) {
                    Menu_ForceClose(play);
                }
                // Ocarina out: put away the way B does it.
                if (ocarina) {
                    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
                    play->msgCtx.ocarinaMode = OCARINA_MODE_END;
                    Message_CloseTextbox(play);
                }
                if (sSaveUiOpen) {
                    sSaveClickClose = true; // closes the way its own Close button does
                }
                // The pause menu closes the way Start would (only while it's just sitting open).
                if ((play->pauseCtx.state == PAUSE_STATE_MAIN) && (play->pauseCtx.mainState == PAUSE_MAIN_STATE_IDLE) &&
                    !play->pauseCtx.itemDescriptionOn) {
                    Interface_SetAButtonDoAction(play, DO_ACTION_NONE);
                    play->pauseCtx.state = PAUSE_STATE_UNPAUSE_SETUP;
                    Audio_PlaySfx(NA_SE_SY_WIN_CLOSE);
                }
                if (!sForcedWarned) {
                    sForcedWarned = true;
                    Menu_ShowBanner("FORCED AMBUSH", "Get ready...");
                }
            }
        }
        return;
    }
    if (sStashed) {
        sStashed = false;
        sForcedWarned = false;
        if (Link_Run(play, sStash)) {
            sLinkGap = FPS;
        }
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
        sNemCur = CLAMP(sNemHuntSlot, 0, NEM_MAX - 1);
        if ((sNem.pool < 0) || ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor))) {
            sNemHuntTimer = 0;
        } else if (((sNemHuntTimer % 10) == 0) && Link_TrySpawnNemNear(play)) {
            sNemHuntTimer = 0;
            Menu_ShowToast("Your Nemesis found you!");
        }
        sNemCur = 0;
    }
    Sur_Update(play);
    if (sLinkGap > 0) {
        return;
    }
    if ((ger_poll(cmd, sizeof(cmd)) > 0) && Link_Run(play, cmd)) {
        sLinkGap = FPS; // a second between commands, so each one lands
    }
}
