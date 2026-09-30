#include "mf.h"

// ---------------------------------------------------------------------------
// Friend website link. The native library does the networking on its own
// thread; here we hand it a JSON snapshot of the run and apply the commands
// friends send back. Commands are plain text lines: NAME|arg|arg|...
// ---------------------------------------------------------------------------

char gFriendsLastMsg[64];

static s32 sPushTimer = 0;
static s32 sConfigTimer = 0;
static s32 sCooldown = 0;
static u8 sEnabled = false;
static s32 sStatus = 0;
static char sJson[4096];

static void ReadNetConfig(void) {
    char* url = recomp_get_config_string("server_url");
    char* room = recomp_get_config_string("room_code");
    char* key = recomp_get_config_string("host_key");
    s32 on = (recomp_get_config_u32("friends_enabled") == 1);

    sEnabled = on && url != NULL && url[0] != '\0';
    mfn_configure(url != NULL ? url : "", room != NULL ? room : "", key != NULL ? key : "", sEnabled);
    if (url != NULL) {
        recomp_free_config_string(url);
    }
    if (room != NULL) {
        recomp_free_config_string(room);
    }
    if (key != NULL) {
        recomp_free_config_string(key);
    }
}

void Friends_Init(void) {
    gFriendsLastMsg[0] = '\0';
    ReadNetConfig();
}

s32 Friends_Status(void) {
    return sStatus;
}

// ---------------------------------------------------------------------------
// JSON writer (tiny, append-only)
// ---------------------------------------------------------------------------

static s32 sLen;

static void J(const char* s) {
    while (*s != '\0' && sLen < (s32)sizeof(sJson) - 1) {
        sJson[sLen++] = *s++;
    }
    sJson[sLen] = '\0';
}

static void JStr(const char* s) {
    J("\"");
    while (s != NULL && *s != '\0' && sLen < (s32)sizeof(sJson) - 3) {
        char c = *s++;
        if (c == '"' || c == '\\') {
            sJson[sLen++] = '\\';
            sJson[sLen++] = c;
        } else if ((u8)c >= 0x20 && (u8)c < 0x7F) {
            sJson[sLen++] = c;
        }
    }
    sJson[sLen] = '\0';
    J("\"");
}

static void JInt(s32 v) {
    char buf[16];
    buf[0] = '\0';
    Str_CatInt(buf, v, sizeof(buf));
    J(buf);
}

static void JKey(const char* k) {
    if (sLen > 0 && sJson[sLen - 1] != '{' && sJson[sLen - 1] != '[') {
        J(",");
    }
    JStr(k);
    J(":");
}

static void BuildState(PlayState* play) {
    char buf[64];
    s32 i;
    u16 time = gSaveContext.save.time;
    s32 mins = (s32)TIME_TO_MINUTES_F(time);
    static const char* forms[] = { "Fierce Deity", "Goron", "Zora", "Deku", "Human" };

    sLen = 0;
    sJson[0] = '\0';
    J("{");
    JKey("v"); JInt(1);
    JKey("hp"); JInt(gSaveContext.save.saveInfo.playerData.health);
    JKey("hpMax"); JInt(gSaveContext.save.saveInfo.playerData.healthCapacity);
    JKey("marks"); JInt(gMf.moonMarks);
    JKey("day"); JInt(CURRENT_DAY);
    buf[0] = '\0';
    Str_CatInt(buf, mins / 60, sizeof(buf));
    Str_Cat(buf, (mins % 60) < 10 ? ":0" : ":", sizeof(buf));
    Str_CatInt(buf, mins % 60, sizeof(buf));
    JKey("time"); JStr(buf);
    JKey("zone"); JStr(gRt.zone != ZONE_NONE ? gZones[gRt.zone].name : "Indoors");
    JKey("lastZone"); JStr(gZones[gRt.lastZone % gZoneCount].name);
    JKey("form"); JStr(forms[gSaveContext.save.playerForm % 5]);
    JKey("dead"); JInt(gRt.playerDead);
    JKey("menu"); JInt(gRt.menuOpen);
    JKey("allowed"); JInt(gMf.settings.friendsAllowed);
    JKey("hurtful"); JInt(gMf.settings.friendHurtful);
    JKey("cooldown"); JInt(gMf.settings.friendCooldownSecs);
    JKey("crowd"); JInt(Curse_Active(CURSE_CROWD));

    JKey("curse");
    if (gMf.curseId < NUM_CURSES) {
        J("{");
        JKey("name"); JStr(gCurses[gMf.curseId].name);
        JKey("desc"); JStr(gCurses[gMf.curseId].desc);
        JKey("warded"); JInt(gMf.curseWarded);
        J("}");
    } else {
        J("null");
    }

    JKey("event");
    if (gEv.id >= 0) {
        const EventDef* e = &gEvents[gEv.id];
        J("{");
        JKey("id"); JInt(gEv.id);
        JKey("name"); JStr(e->name);
        JKey("counter"); JStr(e->counter);
        JKey("kind"); JInt(e->kind);
        JKey("left"); JInt(MF_MAX(0, gEv.duration - gEv.timer) / FPS);
        JKey("by"); JStr(gEv.byFriend);
        J("}");
    } else {
        J("null");
    }
    JKey("nextEvent"); JInt(gEv.id >= 0 ? -1 : gEv.nextIn / FPS);

    JKey("nemesis");
    if (gMf.nemesis.state != NEM_NONE) {
        NemesisRec* n = &gMf.nemesis;
        char name[48];
        Nemesis_Name(name, sizeof(name), n->nameA, n->nameB, -1);
        J("{");
        JKey("name"); JStr(name);
        JKey("species"); JStr(gSpecies[n->species].name);
        JKey("level"); JInt(n->level);
        JKey("state"); JStr(n->state == NEM_DORMANT ? "dormant" : "hunting");
        JKey("zone"); JStr(gZones[n->zone % gZoneCount].name);
        JKey("kills"); JInt(n->kills);
        JKey("escapes"); JInt(n->escapes);
        JKey("present"); JInt(Actors_FindTagged(TAG_NEMESIS, -1) != NULL);
        JKey("stats"); J("[");
        for (i = 0; i < STAT_MAX; i++) {
            if (i > 0) {
                J(",");
            }
            JInt(n->stats[i]);
        }
        J("]");
        JKey("traits"); J("[");
        {
            s32 first = true;
            for (i = 0; i < NUM_TRAITS; i++) {
                if (HAS_TRAIT(n->traits, i)) {
                    if (!first) {
                        J(",");
                    }
                    JStr(gTraits[i].name);
                    first = false;
                }
            }
        }
        J("]");
        J("}");
    } else {
        J("null");
    }

    JKey("bounties"); J("[");
    {
        s32 first = true;
        for (i = 0; i < MAX_BOUNTIES; i++) {
            BountyRec* b = &gMf.bounties[i];
            char name[40];
            if (!b->active) {
                continue;
            }
            if (!first) {
                J(",");
            }
            first = false;
            Bounty_Name(name, sizeof(name), b);
            J("{");
            JKey("name"); JStr(name);
            JKey("rank"); JStr(gRankNames[b->rank]);
            JKey("reward"); JInt(b->reward);
            JKey("zone"); JStr(gZones[b->zone % gZoneCount].name);
            JKey("claimed"); JInt(b->claimed);
            JKey("sponsor"); JStr(b->sponsor);
            J("}");
        }
    }
    J("]");

    JKey("stats"); J("{");
    JKey("survived"); JInt(gMf.eventsSurvived);
    JKey("failed"); JInt(gMf.eventsFailed);
    JKey("deaths"); JInt(gMf.deaths);
    JKey("bounties"); JInt(gMf.bountiesKilled);
    JKey("slain"); JInt(gMf.nemesesSlain);
    J("}");

    // The static catalogue lets the website build its buttons.
    JKey("events"); J("[");
    for (i = 0; i < NUM_EVENTS; i++) {
        if (i > 0) {
            J(",");
        }
        J("[");
        JStr(gEvents[i].name);
        J(",");
        JInt(gEvents[i].kind);
        J(",");
        JInt(gEvents[i].danger);
        J(",");
        JInt(gMf.events[i].enabled);
        J("]");
    }
    J("]");
    JKey("items"); J("[");
    for (i = 0; i < NUM_POUCH_ITEMS; i++) {
        if (i > 0) {
            J(",");
        }
        JStr(gPouchItems[i].name);
    }
    J("]");
    JKey("species"); J("[");
    for (i = 0; i < gSpeciesCount; i++) {
        if (i > 0) {
            J(",");
        }
        J("[");
        JStr(gSpecies[i].name);
        J(",");
        JInt(gSpecies[i].difficulty);
        J("]");
    }
    J("]");
    JKey("zones"); J("[");
    for (i = 0; i < gZoneCount; i++) {
        if (i > 0) {
            J(",");
        }
        JStr(gZones[i].name);
    }
    J("]");
    J("}");
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

static const char* NextField(const char* s, char* out, s32 max) {
    s32 n = 0;
    while (*s != '\0' && *s != '|') {
        if (n < max - 1) {
            out[n++] = *s;
        }
        s++;
    }
    out[n] = '\0';
    return (*s == '|') ? s + 1 : s;
}

static s32 FieldInt(const char* f) {
    return Str_ParseInt(&f);
}

static void Announce(const char* who, const char* what) {
    char buf[64];
    MfColor c = { 150, 220, 255, 255 };
    buf[0] = '\0';
    Str_Cat(buf, who, sizeof(buf));
    Str_Cat(buf, " ", sizeof(buf));
    Str_Cat(buf, what, sizeof(buf));
    Hud_Notify(buf, c);
}

static void HandleCommand(PlayState* play, const char* line) {
    char cmd[16];
    char a[48];
    char b[48];
    char c[48];
    char d[48];
    const char* p = line;
    s32 hurtful = gMf.settings.friendHurtful;

    p = NextField(p, cmd, sizeof(cmd));
    p = NextField(p, a, sizeof(a));
    p = NextField(p, b, sizeof(b));
    p = NextField(p, c, sizeof(c));
    p = NextField(p, d, sizeof(d));

    if (!gMf.settings.friendsAllowed) {
        return;
    }
    if (Str_StartsWith(cmd, "MSG")) {
        // MSG|friend|text
        char buf[64];
        MfColor col = { 255, 255, 180, 255 };
        buf[0] = '\0';
        Str_Cat(buf, a, sizeof(buf));
        Str_Cat(buf, ": ", sizeof(buf));
        Str_Cat(buf, b, sizeof(buf));
        Str_Copy(gFriendsLastMsg, buf, sizeof(gFriendsLastMsg));
        Hud_Notify(buf, col);
        Mf_Sfx(NA_SE_SY_MESSAGE_PASS);
        return;
    }
    if (Str_StartsWith(cmd, "EVENT")) {
        // EVENT|id|friend   (id -1 = random)
        s32 id = FieldInt(a);
        if (gEv.id >= 0 || gRt.playerDead) {
            return;
        }
        if (id < 0 || id >= NUM_EVENTS) {
            id = Events_RandomEligible(play, false);
        }
        if (id < 0 || (gEvents[id].kind == EVK_DANGER && !hurtful)) {
            return;
        }
        if (gEvents[id].eligible != NULL && !gEvents[id].eligible(play)) {
            Announce(b, "tried to summon an event that can't happen here.");
            return;
        }
        Events_Start(play, id, true, b);
        return;
    }
    if (Str_StartsWith(cmd, "HEAL")) {
        Mf_HealPlayer(MF_CLAMP(FieldInt(a), 1, 12));
        Announce(b, "healed you!");
        return;
    }
    if (Str_StartsWith(cmd, "SHIELD")) {
        gBuffTimers[PI_MOONSHIELD] = SEC(MF_CLAMP(FieldInt(a), 1, 10));
        Announce(b, "shielded you!");
        return;
    }
    if (Str_StartsWith(cmd, "GIFT")) {
        s32 item = FieldInt(a);
        if (item >= 0 && item < NUM_POUCH_ITEMS) {
            char buf[48];
            Pouch_Give(item, 1);
            buf[0] = '\0';
            Str_Cat(buf, "gifted you a ", sizeof(buf));
            Str_Cat(buf, gPouchItems[item].name, sizeof(buf));
            Announce(b, buf);
        }
        return;
    }
    if (Str_StartsWith(cmd, "MARKS")) {
        s32 amount = MF_CLAMP(FieldInt(a), 1, 25);
        char who[40];
        who[0] = '\0';
        Str_Cat(who, "gift from ", sizeof(who));
        Str_Cat(who, b, sizeof(who));
        Marks_Add(amount, who);
        return;
    }
    if (Str_StartsWith(cmd, "NEMBUFF") && hurtful && gMf.nemesis.state != NEM_NONE) {
        Nemesis_AddStat(FieldInt(a), 1);
        gMf.nemesis.friendBuffs++;
        Announce(b, "empowered your nemesis!");
        return;
    }
    if (Str_StartsWith(cmd, "NEMWEAK") && gMf.nemesis.state != NEM_NONE) {
        Nemesis_AddStat(FieldInt(a), -1);
        Announce(b, "weakened your nemesis!");
        return;
    }
    if (Str_StartsWith(cmd, "NEMSUMMON") && hurtful && gMf.nemesis.state != NEM_NONE) {
        Nemesis_Summon(play);
        Announce(a, "sent your nemesis after you!");
        return;
    }
    if (Str_StartsWith(cmd, "NEMCROWN") && hurtful && gMf.nemesis.state == NEM_NONE) {
        s32 species = FieldInt(a);
        if (species >= 0 && species < gSpeciesCount && (gSpecies[species].flags & SPF_ROAM)) {
            Nemesis_Promote(play, species, 6, NULL);
            Announce(b, "crowned a new nemesis!");
        }
        return;
    }
    if (Str_StartsWith(cmd, "RENAME") && gMf.nemesis.state != NEM_NONE) {
        gMf.nemesis.nameA = (u8)FieldInt(a);
        gMf.nemesis.nameB = (u8)FieldInt(b);
        Announce(c, "renamed your nemesis.");
        Save_MarkDirty();
        return;
    }
    if (Str_StartsWith(cmd, "BOUNTY")) {
        // BOUNTY|species|zone|reward|friend
        if (Bounty_Add(FieldInt(a), FieldInt(b), FieldInt(c), d)) {
            Announce(d, "posted a bounty!");
        }
        return;
    }
    if (Str_StartsWith(cmd, "CURSE") && hurtful) {
        Curse_Roll(true);
        Announce(a, "rerolled your curse!");
        return;
    }
    if (Str_StartsWith(cmd, "CHILL") && hurtful) {
        gChillTimer = SEC(MF_CLAMP(FieldInt(a), 1, 8));
        Announce(b, "froze your feet!");
        return;
    }
}

void Friends_Update(PlayState* play) {
    char line[160];
    s32 n;
    s32 guard = 0;

    if (++sConfigTimer >= SEC(10)) {
        sConfigTimer = 0;
        ReadNetConfig();
    }
    sStatus = mfn_status();
    if (!sEnabled) {
        return;
    }
    if (++sPushTimer >= SEC(1)) {
        sPushTimer = 0;
        BuildState(play);
        mfn_push_state(sJson);
    }
    if (sCooldown > 0) {
        sCooldown--;
    }
    // Apply commands only during normal gameplay, so nothing lands mid-cutscene.
    if (!gRt.gameplayOk) {
        return;
    }
    while (guard++ < 4 && (n = mfn_poll_command(line, sizeof(line))) > 0) {
        line[MF_MIN(n, (s32)sizeof(line) - 1)] = '\0';
        HandleCommand(play, line);
    }
}
