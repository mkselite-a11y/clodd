#include "mf.h"

// ---------------------------------------------------------------------------
// The Moon Menu: an in-game text overlay. D-pad or stick to move, A to pick,
// Left/Right to change values, B to go back, Start to close.
// The world is frozen while it is open.
// ---------------------------------------------------------------------------

enum {
    PG_MAIN,
    PG_EVENTS,
    PG_EVENT_DETAIL,
    PG_CURSE,
    PG_NEMESIS,
    PG_BOUNTY,
    PG_SHOP,
    PG_CHRONICLE,
    PG_FRIENDS,
    PG_SETTINGS,
    PG_STATS,
    PG_DEBUG,
};

#define ROWS_VISIBLE 14
#define ROW_H 11
#define LIST_Y 40

static s32 sPage = PG_MAIN;
static s32 sCursor[16];
static s32 sScroll[16];
static s32 sDetailEvent = 0;
static s32 sRepeat = 0;
static s32 sHeldDir = 0;
static s32 sOpenedFrame = 0;
static const char* sFlash = NULL;
static s32 sFlashTimer = 0;

static const MfColor cBg = { 12, 4, 28, 225 };
static const MfColor cHead = { 40, 16, 80, 255 };
static const MfColor cSel = { 90, 50, 160, 200 };
static const MfColor cText = { 225, 225, 245, 255 };
static const MfColor cDim = { 150, 150, 180, 255 };
static const MfColor cGold = { 255, 225, 130, 255 };
static const MfColor cRed = { 255, 120, 120, 255 };
static const MfColor cGreen = { 130, 255, 160, 255 };

void Menu_Open(s32 page) {
    gRt.menuOpen = true;
    sPage = (page == PAGE_BOUNTY) ? PG_BOUNTY : (page == PAGE_SHOP) ? PG_SHOP : PG_MAIN;
    sOpenedFrame = gRt.frame;
    Mf_Sfx(NA_SE_SY_DECIDE);
}

void Menu_Close(void) {
    gRt.menuOpen = false;
    Mf_Sfx(NA_SE_SY_CANCEL);
    Save_MarkDirty();
}

static void Flash(const char* msg) {
    sFlash = msg;
    sFlashTimer = SEC(2);
}

// ---------------------------------------------------------------------------
// Row counts per page
// ---------------------------------------------------------------------------

static const char* sMainItems[] = {
    "Global Events", "This Cycle's Curse", "Your Nemesis", "Bounty Board", "Moon Pouch Shop",
    "Chronicle of Termina", "Friends", "Settings", "Stats", "Debug", "Close",
};

typedef enum {
    SET_EVENTS, SET_INTERVAL, SET_JITTER, SET_MIN, SET_MAX, SET_NEMESIS, SET_PROMO, SET_BOUNTIES,
    SET_DUNGEONS, SET_CROSSZONE, SET_BUDGET, SET_BANNERS, SET_RUMORS, SET_DIFFICULTY, SET_FRIENDS,
    SET_FRIEND_HURT, SET_FRIEND_CD, SET_RESET_EVENTS, SET_COUNT
} SettingRow;

static const char* sDebugItems[] = {
    "Start random event", "Stop current event", "+100 Moon Marks", "Promote random nemesis",
    "Nemesis +1 random stat", "Nemesis gains a trait", "Summon nemesis here", "Remove nemesis",
    "Reroll this cycle's curse", "Refresh bounty board", "Move claimed bounties here",
    "World tick now", "Give 1 of every pouch item", "Reset ALL mod data",
};

static s32 MainCount(void) {
    return gRt.debugMenu ? ARRAY_LEN(sMainItems) : ARRAY_LEN(sMainItems) - 1;
}

static s32 MainItemAt(s32 row) {
    // Skip "Debug" when hidden.
    if (!gRt.debugMenu && row >= 9) {
        return row + 1;
    }
    return row;
}

static s32 RowCount(void) {
    switch (sPage) {
        case PG_MAIN: return MainCount();
        case PG_EVENTS: return NUM_EVENTS;
        case PG_EVENT_DETAIL: return 7;
        case PG_CURSE: return NUM_CURSES;
        case PG_BOUNTY: return MAX_BOUNTIES;
        case PG_SHOP: return NUM_POUCH_ITEMS;
        case PG_CHRONICLE: return MF_MAX(1, gMf.logCount);
        case PG_SETTINGS: return SET_COUNT;
        case PG_DEBUG: return ARRAY_LEN(sDebugItems);
        default: return 1;
    }
}

// ---------------------------------------------------------------------------
// Value editing
// ---------------------------------------------------------------------------

static void Adjust8(u8* v, s32 delta, s32 lo, s32 hi) {
    *v = (u8)MF_CLAMP((s32)*v + delta, lo, hi);
}

static void Adjust16(u16* v, s32 delta, s32 lo, s32 hi) {
    *v = (u16)MF_CLAMP((s32)*v + delta, lo, hi);
}

static void EditSetting(s32 row, s32 dir) {
    GlobalSettings* s = &gMf.settings;
    switch (row) {
        case SET_EVENTS: s->eventsEnabled ^= 1; break;
        case SET_INTERVAL: Adjust16(&s->eventIntervalSecs, dir * 15, 45, 900); break;
        case SET_JITTER: Adjust8(&s->eventJitterSecs, dir * 5, 0, 120); break;
        case SET_MIN: Adjust8(&s->eventMinSecs, dir * 5, 10, 120); break;
        case SET_MAX: Adjust8(&s->eventMaxSecs, dir * 5, 10, 180); break;
        case SET_NEMESIS: s->nemesisEnabled ^= 1; break;
        case SET_PROMO: Adjust8(&s->promotionChance, dir * 5, 0, 100); break;
        case SET_BOUNTIES: s->bountiesEnabled ^= 1; break;
        case SET_DUNGEONS: s->allowDungeons ^= 1; break;
        case SET_CROSSZONE: s->crossZoneObjects ^= 1; break;
        case SET_BUDGET: Adjust16(&s->objectBudgetKB, dir * 16, 32, 400); break;
        case SET_BANNERS: s->hudBanners ^= 1; break;
        case SET_RUMORS: s->notifyRumors ^= 1; break;
        case SET_DIFFICULTY: Adjust8(&s->difficultyPct, dir * 10, 50, 250); break;
        case SET_FRIENDS: s->friendsAllowed ^= 1; break;
        case SET_FRIEND_HURT: s->friendHurtful ^= 1; break;
        case SET_FRIEND_CD: Adjust8(&s->friendCooldownSecs, dir * 5, 0, 120); break;
        case SET_RESET_EVENTS:
            if (dir == 0) {
                Events_ResetSettings();
                Flash("All event settings reset.");
            }
            break;
    }
    Save_MarkDirty();
}

static void SettingText(s32 row, char* label, char* value) {
    GlobalSettings* s = &gMf.settings;
    static const char* names[SET_COUNT] = {
        "Global Events", "Event interval (s)", "Interval jitter (s)", "Event min length (s)", "Event max length (s)",
        "Nemesis system", "Promotion chance %", "Bounties", "Events/nemesis in dungeons", "Cross-zone enemies",
        "Enemy object budget KB", "Big banners", "Rumor pop-ups", "Mod difficulty %", "Friend website",
        "Friends can hurt you", "Friend cooldown (s)", "Reset all event settings",
    };
    Str_Copy(label, names[row], 32);
    value[0] = '\0';
    switch (row) {
        case SET_EVENTS: Str_Cat(value, s->eventsEnabled ? "On" : "Off", 12); break;
        case SET_INTERVAL: Str_CatInt(value, s->eventIntervalSecs, 12); break;
        case SET_JITTER: Str_CatInt(value, s->eventJitterSecs, 12); break;
        case SET_MIN: Str_CatInt(value, s->eventMinSecs, 12); break;
        case SET_MAX: Str_CatInt(value, s->eventMaxSecs, 12); break;
        case SET_NEMESIS: Str_Cat(value, s->nemesisEnabled ? "On" : "Off", 12); break;
        case SET_PROMO: Str_CatInt(value, s->promotionChance, 12); break;
        case SET_BOUNTIES: Str_Cat(value, s->bountiesEnabled ? "On" : "Off", 12); break;
        case SET_DUNGEONS: Str_Cat(value, s->allowDungeons ? "Yes" : "No", 12); break;
        case SET_CROSSZONE: Str_Cat(value, s->crossZoneObjects ? "On" : "Off", 12); break;
        case SET_BUDGET: Str_CatInt(value, s->objectBudgetKB, 12); break;
        case SET_BANNERS: Str_Cat(value, s->hudBanners ? "On" : "Off", 12); break;
        case SET_RUMORS: Str_Cat(value, s->notifyRumors ? "On" : "Off", 12); break;
        case SET_DIFFICULTY: Str_CatInt(value, s->difficultyPct, 12); break;
        case SET_FRIENDS: Str_Cat(value, s->friendsAllowed ? "Allowed" : "Blocked", 12); break;
        case SET_FRIEND_HURT: Str_Cat(value, s->friendHurtful ? "Yes" : "No", 12); break;
        case SET_FRIEND_CD: Str_CatInt(value, s->friendCooldownSecs, 12); break;
        case SET_RESET_EVENTS: Str_Cat(value, "(A)", 12); break;
    }
}

static void EditEventDetail(s32 row, s32 dir) {
    EventSettings* es = &gMf.events[sDetailEvent];
    const EventDef* e = &gEvents[sDetailEvent];
    if (row == 0) {
        es->enabled ^= 1;
    } else if (row == 1) {
        Adjust8(&es->weight, dir, 0, 10);
    } else if (row >= 2 && row <= 4) {
        s32 i = row - 2;
        if (e->paramMax[i] > 0) {
            s32 step = (e->paramMax[i] >= 40) ? 5 : 1;
            Adjust8(&es->p[i], dir * step, 0, e->paramMax[i]);
        }
    }
    Save_MarkDirty();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static void DoDebug(PlayState* play, s32 row) {
    s32 i;
    switch (row) {
        case 0: Menu_Close(); Events_Start(play, Events_RandomEligible(play, false), true, NULL); break;
        case 1: Events_Stop(play, true); Flash("Event stopped."); break;
        case 2: gMf.moonMarks += 100; Flash("+100 Moon Marks."); break;
        case 3:
            if (gMf.nemesis.state == NEM_NONE) {
                Nemesis_Promote(play, Roster_RandomRoaming(9), 6, NULL);
                Flash("A nemesis has been chosen.");
            } else {
                Flash("You already have a nemesis.");
            }
            break;
        case 4: Nemesis_AddStat(-1, 1); Flash("Nemesis grew stronger."); break;
        case 5: Nemesis_GainTrait(); Flash("Nemesis evolved."); break;
        case 6: Nemesis_Summon(play); Flash("Nemesis summoned to this zone."); break;
        case 7: Actors_KillTagged(TAG_NEMESIS); gMf.nemesis.state = NEM_NONE; Flash("Nemesis removed."); break;
        case 8: Curse_Roll(false); Flash("Curse rerolled."); break;
        case 9: Bounty_Refresh(true); Flash("Board refreshed."); break;
        case 10:
            for (i = 0; i < MAX_BOUNTIES; i++) {
                if (gMf.bounties[i].active && gMf.bounties[i].claimed && gRt.zone != ZONE_NONE) {
                    gMf.bounties[i].zone = gRt.zone;
                }
            }
            Flash("Claimed bounties moved here (reload area if needed).");
            break;
        case 11: World_Tick(play); Flash("The world moved on."); break;
        case 12:
            for (i = 0; i < NUM_POUCH_ITEMS; i++) {
                Pouch_Give(i, 1);
            }
            Flash("Pouch stocked.");
            break;
        case 13: Save_Defaults(); Flash("All mod data reset."); break;
    }
    Save_MarkDirty();
}

static void Activate(PlayState* play, s32 row) {
    switch (sPage) {
        case PG_MAIN: {
            static const s32 targets[] = { PG_EVENTS, PG_CURSE, PG_NEMESIS, PG_BOUNTY, PG_SHOP, PG_CHRONICLE,
                                           PG_FRIENDS, PG_SETTINGS, PG_STATS, PG_DEBUG, -1 };
            s32 t = targets[MainItemAt(row)];
            if (t < 0) {
                Menu_Close();
            } else {
                sPage = t;
            }
            break;
        }
        case PG_EVENTS:
            sDetailEvent = row;
            sPage = PG_EVENT_DETAIL;
            sCursor[PG_EVENT_DETAIL] = 0;
            break;
        case PG_EVENT_DETAIL:
            if (row == 5) {
                Menu_Close();
                Events_Start(play, sDetailEvent, true, NULL);
            } else if (row == 6) {
                gMf.events[sDetailEvent].enabled = true;
                gMf.events[sDetailEvent].weight = 5;
                gMf.events[sDetailEvent].p[0] = gEvents[sDetailEvent].defaults[0];
                gMf.events[sDetailEvent].p[1] = gEvents[sDetailEvent].defaults[1];
                gMf.events[sDetailEvent].p[2] = gEvents[sDetailEvent].defaults[2];
                Flash("Defaults restored.");
            } else {
                EditEventDetail(row, 1);
            }
            break;
        case PG_BOUNTY: {
            BountyRec* b = &gMf.bounties[row];
            if (!b->active) {
                break;
            }
            if (b->claimed) {
                b->claimed = false;
                Flash("Contract released.");
            } else if (Bounty_ClaimedCount() >= MAX_CLAIMED) {
                Flash("You can hold 3 contracts at once.");
            } else {
                b->claimed = true;
                Flash("Contract claimed! Hunt it down.");
            }
            Save_MarkDirty();
            break;
        }
        case PG_SHOP:
            if (gMf.pouch[row] >= gPouchItems[row].maxStack) {
                Flash("Your pouch can't hold more of that.");
            } else if (Pouch_Buy(row)) {
                Mf_Sfx(NA_SE_SY_GET_RUPY);
                Flash("Bought!");
            } else {
                Mf_Sfx(NA_SE_SY_ERROR);
                Flash("Not enough Moon Marks.");
            }
            break;
        case PG_SETTINGS:
            EditSetting(row, 0);
            if (row != SET_RESET_EVENTS && row != SET_INTERVAL && row != SET_JITTER && row != SET_MIN &&
                row != SET_MAX && row != SET_PROMO && row != SET_BUDGET && row != SET_DIFFICULTY && row != SET_FRIEND_CD) {
                // toggles already flipped by EditSetting
            }
            break;
        case PG_DEBUG:
            DoDebug(play, row);
            break;
        default:
            break;
    }
}

static s32 BackPage(void) {
    switch (sPage) {
        case PG_EVENT_DETAIL: return PG_EVENTS;
        case PG_MAIN: return -1;
        default: return PG_MAIN;
    }
}

void Menu_Update(PlayState* play) {
    s32 count = RowCount();
    s32* cur = &sCursor[sPage];
    s32* scroll = &sScroll[sPage];
    s32 dir = 0;
    s32 lr = 0;

    if (sFlashTimer > 0) {
        sFlashTimer--;
    }
    // Ignore the input that opened the menu.
    if (gRt.frame - sOpenedFrame < 3) {
        return;
    }
    if (gRt.stickY > 40 || (gRt.btnCur & BTN_DUP)) {
        dir = -1;
    } else if (gRt.stickY < -40 || (gRt.btnCur & BTN_DDOWN)) {
        dir = 1;
    } else if (gRt.stickX > 40 || (gRt.btnCur & BTN_DRIGHT)) {
        lr = 1;
    } else if (gRt.stickX < -40 || (gRt.btnCur & BTN_DLEFT)) {
        lr = -1;
    }
    {
        s32 held = dir * 2 + lr;
        if (held == 0) {
            sHeldDir = 0;
            sRepeat = 0;
        } else if (held != sHeldDir) {
            sHeldDir = held;
            sRepeat = 8;
        } else if (--sRepeat > 0) {
            dir = 0;
            lr = 0;
        } else {
            sRepeat = 2;
        }
    }
    if (dir != 0 && count > 0) {
        *cur = (*cur + dir + count) % count;
        Mf_Sfx(NA_SE_SY_CURSOR);
    }
    if (lr != 0) {
        if (sPage == PG_SETTINGS) {
            EditSetting(*cur, lr);
            Mf_Sfx(NA_SE_SY_CURSOR);
        } else if (sPage == PG_EVENT_DETAIL && *cur <= 4) {
            EditEventDetail(*cur, lr);
            Mf_Sfx(NA_SE_SY_CURSOR);
        } else if (sPage == PG_EVENTS) {
            *cur = (*cur + lr * ROWS_VISIBLE + count * 4) % count;
        }
    }
    if (*cur < *scroll) {
        *scroll = *cur;
    } else if (*cur >= *scroll + ROWS_VISIBLE) {
        *scroll = *cur - ROWS_VISIBLE + 1;
    }

    if (gRt.btnPress & BTN_START) {
        Menu_Close();
        return;
    }
    if (gRt.btnPress & BTN_B) {
        s32 back = BackPage();
        if (back < 0) {
            Menu_Close();
        } else {
            sPage = back;
            Mf_Sfx(NA_SE_SY_CANCEL);
        }
        return;
    }
    if (gRt.btnPress & BTN_A) {
        Activate(play, *cur);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void Row(s32 i, s32 selected, MfColor c, const char* left, const char* right) {
    s32 y = LIST_Y + i * ROW_H;
    if (selected) {
        Draw2D_Rect(14, y - 2, SCREEN_WIDTH - 28, ROW_H, cSel);
    }
    Draw2D_Text(20, y, c, left);
    if (right != NULL && right[0] != '\0') {
        Draw2D_Text(SCREEN_WIDTH - 20 - Str_Len(right) * 8, y, c, right);
    }
}

static void Footer(const char* a, const char* b) {
    MfColor bg = { 25, 10, 50, 230 };
    Draw2D_Rect(10, 196, SCREEN_WIDTH - 20, 30, bg);
    if (a != NULL) {
        Draw2D_Text(16, 199, cText, a);
    }
    if (b != NULL) {
        Draw2D_Text(16, 210, cDim, b);
    }
}

static void Header(const char* title) {
    char buf[48];
    Draw2D_Rect(10, 10, SCREEN_WIDTH - 20, 24, cHead);
    Draw2D_Text(16, 13, Mf_Rainbow(gRt.frame * 3, 255), title);
    buf[0] = '\0';
    Str_CatInt(buf, gMf.moonMarks, sizeof(buf));
    Str_Cat(buf, " Marks", sizeof(buf));
    Draw2D_Text(SCREEN_WIDTH - 16 - Str_Len(buf) * 8, 13, cGold, buf);
    buf[0] = '\0';
    Str_Cat(buf, "Day ", sizeof(buf));
    Str_CatInt(buf, CURRENT_DAY, sizeof(buf));
    if (gMf.curseId < NUM_CURSES) {
        Str_Cat(buf, " - ", sizeof(buf));
        Str_Cat(buf, gCurses[gMf.curseId].name, sizeof(buf));
        if (gMf.curseWarded) {
            Str_Cat(buf, " (warded)", sizeof(buf));
        }
    }
    Draw2D_Text(16, 23, cDim, buf);
}

static void DrawMain(void) {
    s32 i;
    Header("MOON MENU");
    for (i = 0; i < MainCount(); i++) {
        Row(i, sCursor[PG_MAIN] == i, cText, sMainItems[MainItemAt(i)], NULL);
    }
    Footer("A: open   B/Start: close", "Ocarina: C^ C^ Cv Cv opens this anywhere");
}

static void DrawEvents(void) {
    s32 i;
    char right[16];
    Header("GLOBAL EVENTS");
    for (i = 0; i < ROWS_VISIBLE; i++) {
        s32 idx = sScroll[PG_EVENTS] + i;
        const EventDef* e;
        char left[40];
        MfColor c;
        if (idx >= NUM_EVENTS) {
            break;
        }
        e = &gEvents[idx];
        left[0] = '\0';
        Str_Cat(left, gMf.events[idx].enabled ? "[x] " : "[ ] ", sizeof(left));
        Str_Cat(left, e->name, sizeof(left));
        right[0] = '\0';
        if (e->kind == EVK_HARMLESS) {
            Str_Cat(right, "calm", sizeof(right));
            c = (MfColor){ 140, 190, 255, 255 };
        } else if (e->kind == EVK_BENEFIT) {
            Str_Cat(right, "boon", sizeof(right));
            c = cGreen;
        } else {
            Str_Cat(right, "risk ", sizeof(right));
            Str_CatInt(right, e->danger, sizeof(right));
            c = cText;
        }
        if (!gMf.events[idx].enabled) {
            c = cDim;
        }
        Row(i, sCursor[PG_EVENTS] == idx, c, left, right);
    }
    Footer(gEvents[sCursor[PG_EVENTS]].flavor, "A: settings & test   L/R: page");
}

static void DrawEventDetail(void) {
    const EventDef* e = &gEvents[sDetailEvent];
    EventSettings* es = &gMf.events[sDetailEvent];
    char v[16];
    s32 i;
    s32 sel = sCursor[PG_EVENT_DETAIL];

    Header(e->name);
    v[0] = '\0';
    Str_Cat(v, es->enabled ? "On" : "Off", sizeof(v));
    Row(0, sel == 0, cText, "Enabled", v);
    v[0] = '\0';
    Str_CatInt(v, es->weight, sizeof(v));
    Row(1, sel == 1, cText, "Frequency weight", v);
    for (i = 0; i < 3; i++) {
        v[0] = '\0';
        if (e->paramMax[i] > 0) {
            Str_CatInt(v, es->p[i], sizeof(v));
        } else {
            Str_Cat(v, "-", sizeof(v));
        }
        Row(2 + i, sel == 2 + i, (e->paramMax[i] > 0) ? cText : cDim, e->paramNames[i], v);
    }
    Row(5, sel == 5, cGold, "Force trigger now (debug)", NULL);
    Row(6, sel == 6, cDim, "Restore defaults", NULL);
    Draw2D_Text(20, LIST_Y + 8 * ROW_H, cGold, "Counter:");
    Draw2D_Text(20, LIST_Y + 9 * ROW_H, cText, e->counter);
    Draw2D_Text(20, LIST_Y + 10 * ROW_H + 4, cDim, e->flavor);
    Footer("Left/Right: change value   A: toggle/run", "B: back");
}

static void DrawCurse(void) {
    s32 i;
    Header("CURSES");
    for (i = 0; i < NUM_CURSES; i++) {
        s32 active = (gMf.curseId == i);
        MfColor c = active ? (gMf.curseWarded ? cGreen : cRed) : cDim;
        Row(i, sCursor[PG_CURSE] == i, c, gCurses[i].name, active ? "ACTIVE" : gCurses[i].system);
    }
    Footer(gCurses[sCursor[PG_CURSE]].desc, "A new curse is rolled at the start of every cycle.");
}

static s32 NemesisRevealed(void) {
    return (gMf.spyglassCycle == gSaveContext.save.saveInfo.playerData.threeDayResetCount + 1) ||
           (World_Distance(gMf.nemesis.zone, gRt.lastZone) <= 1);
}

static void DrawNemesis(void) {
    NemesisRec* n = &gMf.nemesis;
    char buf[64];
    s32 i;
    s32 y = LIST_Y;
    static const char* statNames[STAT_MAX] = { "Health", "Power", "Tracking", "Speed" };

    Header("YOUR NEMESIS");
    if (n->state == NEM_NONE) {
        Draw2D_Text(20, y, cText, "No enemy has earned that title... yet.");
        Draw2D_Text(20, y + 16, cDim, "An enemy is promoted when it:");
        Draw2D_Text(28, y + 28, cDim, "- kills you");
        Draw2D_Text(28, y + 38, cDim, "- hits you 3 times and lives");
        Draw2D_Text(28, y + 48, cDim, "- sees you run from a fight");
        Draw2D_Text(28, y + 58, cDim, "- survives at low health for 20s");
        Draw2D_Text(28, y + 68, cDim, "- outlasts a Moon ambush");
        Footer("Only one nemesis can hunt you at a time.", "B: back");
        return;
    }
    Nemesis_Name(buf, sizeof(buf), n->nameA, n->nameB, -1);
    Draw2D_Text(20, y, Mf_Rainbow(gRt.frame * 4, 255), buf);
    buf[0] = '\0';
    Str_Cat(buf, "Lv", sizeof(buf));
    Str_CatInt(buf, n->level, sizeof(buf));
    Str_Cat(buf, " ", sizeof(buf));
    Str_Cat(buf, gSpecies[n->species].name, sizeof(buf));
    Str_Cat(buf, n->state == NEM_DORMANT ? "  (dormant)" : "  (hunting)", sizeof(buf));
    if (n->scarred) {
        Str_Cat(buf, " scarred", sizeof(buf));
    }
    Draw2D_Text(20, y + 10, cText, buf);
    buf[0] = '\0';
    Str_Cat(buf, "Location: ", sizeof(buf));
    Str_Cat(buf, NemesisRevealed() ? gZones[n->zone].name : "unknown", sizeof(buf));
    Draw2D_Text(20, y + 20, NemesisRevealed() ? cGold : cDim, buf);
    for (i = 0; i < STAT_MAX; i++) {
        MfColor bar = { 200, 80, 255, 230 };
        MfColor bg = { 40, 40, 60, 200 };
        s32 by = y + 34 + i * 10;
        Draw2D_Text(20, by, cText, statNames[i]);
        Draw2D_Rect(100, by + 1, 100, 6, bg);
        Draw2D_Rect(100, by + 1, MF_MIN(100, n->stats[i] * 10), 6, bar);
        buf[0] = '\0';
        Str_CatInt(buf, n->stats[i], sizeof(buf));
        Draw2D_Text(206, by, cText, buf);
    }
    y += 78;
    Draw2D_Text(20, y, cGold, "Traits:");
    y += 10;
    for (i = 0; i < NUM_TRAITS && y < 186; i++) {
        if (HAS_TRAIT(n->traits, i)) {
            buf[0] = '\0';
            Str_Cat(buf, gTraits[i].name, sizeof(buf));
            Draw2D_Text(28, y, cText, buf);
            Draw2D_Text(128, y, cDim, gTraits[i].desc);
            y += 10;
        }
    }
    buf[0] = '\0';
    Str_Cat(buf, "Kills ", sizeof(buf));
    Str_CatInt(buf, n->kills, sizeof(buf));
    Str_Cat(buf, "  Meetings ", sizeof(buf));
    Str_CatInt(buf, n->encounters, sizeof(buf));
    Str_Cat(buf, "  You fled ", sizeof(buf));
    Str_CatInt(buf, n->escapes, sizeof(buf));
    Footer(buf, gPromotionReasons[n->promotionReason % 7]);
}

static void DrawBounty(void) {
    s32 i;
    char left[48];
    char right[24];
    s32 sel = sCursor[PG_BOUNTY];
    BountyRec* b = &gMf.bounties[sel];
    s32 spy = (gMf.spyglassCycle == gSaveContext.save.saveInfo.playerData.threeDayResetCount + 1);

    Header("BOUNTY BOARD");
    for (i = 0; i < MAX_BOUNTIES; i++) {
        BountyRec* r = &gMf.bounties[i];
        MfColor c = cText;
        if (!r->active) {
            Row(i, sel == i, cDim, "(empty)", NULL);
            continue;
        }
        left[0] = '\0';
        Str_Cat(left, r->claimed ? "* " : "  ", sizeof(left));
        Str_Cat(left, gRankNames[r->rank], sizeof(left));
        Str_Cat(left, " ", sizeof(left));
        {
            char nm[40];
            Bounty_Name(nm, sizeof(nm), r);
            Str_Cat(left, nm, sizeof(left));
        }
        right[0] = '\0';
        Str_CatInt(right, r->reward, sizeof(right));
        Str_Cat(right, "M", sizeof(right));
        if (r->claimed) {
            c = cGold;
        } else if (r->sponsored) {
            c = (MfColor){ 150, 220, 255, 255 };
        }
        Row(i, sel == i, c, left, right);
    }
    if (b->active) {
        char l1[64];
        char l2[64];
        s32 t;
        l1[0] = '\0';
        Str_Cat(l1, (b->claimed || spy || gZones[b->zone].danger >= 0) ? gZones[b->zone].name : "?", sizeof(l1));
        Str_Cat(l1, " - ", sizeof(l1));
        Str_Cat(l1, Bounty_MotiveName(b->motive), sizeof(l1));
        if (b->sponsored) {
            Str_Cat(l1, " - by ", sizeof(l1));
            Str_Cat(l1, b->sponsor, sizeof(l1));
        }
        l2[0] = '\0';
        for (t = 0; t < NUM_TRAITS; t++) {
            if (HAS_TRAIT(b->traits, t)) {
                Str_Cat(l2, gTraits[t].name, sizeof(l2));
                Str_Cat(l2, " ", sizeof(l2));
            }
        }
        if (l2[0] == '\0') {
            Str_Cat(l2, "No traits. ", sizeof(l2));
        }
        Str_Cat(l2, "A: claim/release", sizeof(l2));
        Draw2D_Text(20, LIST_Y + 9 * ROW_H, cDim, "Difficulty");
        {
            MfColor bar = { 255, 170, 60, 230 };
            Draw2D_Rect(110, LIST_Y + 9 * ROW_H + 1, gSpecies[b->species].difficulty * 12, 6, bar);
        }
        Footer(l1, l2);
    } else {
        Footer("Contracts: claim up to 3. Claimed targets", "appear when you enter their area.");
    }
}

static void DrawShop(void) {
    s32 i;
    char right[24];
    s32 sel = sCursor[PG_SHOP];
    Header("MOON POUCH SHOP");
    for (i = 0; i < ROWS_VISIBLE; i++) {
        s32 idx = sScroll[PG_SHOP] + i;
        MfColor c;
        if (idx >= NUM_POUCH_ITEMS) {
            break;
        }
        right[0] = '\0';
        Str_CatInt(right, Pouch_Price(idx), sizeof(right));
        Str_Cat(right, "M  ", sizeof(right));
        Str_CatInt(right, gMf.pouch[idx], sizeof(right));
        Str_Cat(right, "/", sizeof(right));
        Str_CatInt(right, gPouchItems[idx].maxStack, sizeof(right));
        c = ((s32)gMf.moonMarks >= Pouch_Price(idx)) ? cText : cDim;
        Row(i, sel == idx, c, gPouchItems[idx].name, right);
    }
    Footer(gPouchItems[sel].desc, "A: buy   Use in play: hold L, D-pad/C to pick, Down to use");
}

static void DrawChronicle(void) {
    s32 i;
    Header("CHRONICLE OF TERMINA");
    if (gMf.logCount == 0) {
        Draw2D_Text(20, LIST_Y, cDim, "Nothing has happened... yet.");
    }
    for (i = 0; i < ROWS_VISIBLE; i++) {
        s32 idx = sScroll[PG_CHRONICLE] + i;
        s32 slot;
        char line[40];
        if (idx >= gMf.logCount) {
            break;
        }
        slot = (gMf.logHead - 1 - idx + LOG_LINES * 2) % LOG_LINES;
        Str_Copy(line, gMf.log[slot], 37);
        Row(i, sCursor[PG_CHRONICLE] == idx, (idx == 0) ? cGold : cText, line, NULL);
    }
    {
        s32 slot = (gMf.logHead - 1 - sCursor[PG_CHRONICLE] + LOG_LINES * 2) % LOG_LINES;
        Footer(gMf.logCount > 0 ? gMf.log[slot] : "", "Newest first");
    }
}

static void DrawFriends(void) {
    char buf[64];
    s32 st = Friends_Status();
    static const char* states[] = { "Off", "Connecting", "Connected", "Server error", "Bad host key", "No native library" };

    Header("FRIENDS");
    buf[0] = '\0';
    Str_Cat(buf, "Status: ", sizeof(buf));
    Str_Cat(buf, states[MF_CLAMP(st, 0, 5)], sizeof(buf));
    Draw2D_Text(20, LIST_Y, st == 2 ? cGreen : cText, buf);
    Draw2D_Text(20, LIST_Y + 14, cDim, "Set Server URL, Room Code and Host Key");
    Draw2D_Text(20, LIST_Y + 24, cDim, "in Recomp's mod menu (Mods > Moonfall).");
    Draw2D_Text(20, LIST_Y + 40, cText, "Friends open your server URL, type the");
    Draw2D_Text(20, LIST_Y + 50, cText, "room code and a nickname, and can then:");
    Draw2D_Text(28, LIST_Y + 62, cDim, "- watch your hearts, area and nemesis");
    Draw2D_Text(28, LIST_Y + 72, cDim, "- trigger events or vote on the next one");
    Draw2D_Text(28, LIST_Y + 82, cDim, "- bless you, taunt you, gift pouch items");
    Draw2D_Text(28, LIST_Y + 92, cDim, "- post bounties and empower your nemesis");
    Draw2D_Text(20, LIST_Y + 108, cGold, "Last message:");
    Draw2D_Text(20, LIST_Y + 118, cText, gFriendsLastMsg[0] ? gFriendsLastMsg : "(none)");
    Footer(gMf.settings.friendsAllowed ? "Friend meddling is allowed" : "Friend meddling is blocked", "B: back");
}

static void DrawSettings(void) {
    s32 i;
    char label[32];
    char value[16];
    Header("SETTINGS");
    for (i = 0; i < ROWS_VISIBLE; i++) {
        s32 idx = sScroll[PG_SETTINGS] + i;
        if (idx >= SET_COUNT) {
            break;
        }
        SettingText(idx, label, value);
        Row(i, sCursor[PG_SETTINGS] == idx, cText, label, value);
    }
    Footer("Left/Right: change   A: toggle", "Network settings live in Recomp's mod config.");
}

static void DrawStats(void) {
    char buf[48];
    s32 y = LIST_Y;
    Header("STATS");
#define STAT_LINE(label, value)                  \
    buf[0] = '\0';                               \
    Str_Cat(buf, label, sizeof(buf));            \
    Str_CatInt(buf, (s32)(value), sizeof(buf));  \
    Draw2D_Text(20, y, cText, buf);              \
    y += 12;
    STAT_LINE("Moon Marks now:      ", gMf.moonMarks);
    STAT_LINE("Moon Marks lifetime: ", gMf.lifetimeMarks);
    STAT_LINE("Events survived:     ", gMf.eventsSurvived);
    STAT_LINE("Events failed:       ", gMf.eventsFailed);
    STAT_LINE("Bounties collected:  ", gMf.bountiesKilled);
    STAT_LINE("Nemeses slain:       ", gMf.nemesesSlain);
    STAT_LINE("Deaths:              ", gMf.deaths);
    STAT_LINE("Next event in (s):   ", gEv.id >= 0 ? 0 : gEv.nextIn / FPS);
#undef STAT_LINE
    Footer("Keep surviving.", "B: back");
}

static void DrawDebug(void) {
    s32 i;
    Header("DEBUG");
    for (i = 0; i < ROWS_VISIBLE; i++) {
        s32 idx = sScroll[PG_DEBUG] + i;
        if (idx >= (s32)ARRAY_LEN(sDebugItems)) {
            break;
        }
        Row(i, sCursor[PG_DEBUG] == idx, idx == 13 ? cRed : cText, sDebugItems[idx], NULL);
    }
    Footer("Testing tools. Individual events can be", "force-triggered from Global Events.");
}

void Menu_Draw(PlayState* play) {
    if (!gRt.menuOpen) {
        return;
    }
    Draw2D_Rect(6, 6, SCREEN_WIDTH - 12, SCREEN_HEIGHT - 12, cBg);
    switch (sPage) {
        case PG_MAIN: DrawMain(); break;
        case PG_EVENTS: DrawEvents(); break;
        case PG_EVENT_DETAIL: DrawEventDetail(); break;
        case PG_CURSE: DrawCurse(); break;
        case PG_NEMESIS: DrawNemesis(); break;
        case PG_BOUNTY: DrawBounty(); break;
        case PG_SHOP: DrawShop(); break;
        case PG_CHRONICLE: DrawChronicle(); break;
        case PG_FRIENDS: DrawFriends(); break;
        case PG_SETTINGS: DrawSettings(); break;
        case PG_STATS: DrawStats(); break;
        case PG_DEBUG: DrawDebug(); break;
    }
    if (sFlashTimer > 0 && sFlash != NULL) {
        MfColor bg = { 0, 0, 0, 200 };
        Draw2D_Rect(40, 100, SCREEN_WIDTH - 80, 16, bg);
        Draw2D_TextCentered(104, cGold, sFlash);
    }
}
