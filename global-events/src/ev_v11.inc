// ===========================================================================
// Global Events 1.1
//
// Included into events.c (after the event code, before the scheduler), so it
// shares that file's helpers. Adds:
//   - title cards in the game's own font, and the event roulette
//   - combo events, the cycle curse, Moon Pressure and Grudge
//   - the run report
//   - Tingle Air Raid, Goron Stampede and Cursed Tag
// Nothing here gives items or sets progress flags.
// ===========================================================================

// ---------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------

static s32 Ev_Itoa(char* out, s32 value) {
    char tmp[12];
    s32 n = 0;
    s32 len = 0;

    if (value < 0) {
        out[len++] = '-';
        value = -value;
    }
    do {
        tmp[n++] = '0' + (value % 10);
        value /= 10;
    } while ((value > 0) && (n < 10));
    while (n > 0) {
        out[len++] = tmp[--n];
    }
    out[len] = '\0';
    return len;
}

static s32 Ev_AppendNum(char* out, s32 n, s32 value, s32 maxLen) {
    char num[12];

    Ev_Itoa(num, value);
    return Ev_Append(out, n, num, maxLen);
}

const char* Events_ShortName(s32 ev) {
    switch (ev) {
        case EV_REDLIGHT:
            return "Red Light";
        case EV_STILL:
            return "Time w/ You";
        case EV_UPRISING:
            return "Uprising";
        case EV_PROCESSION:
            return "Procession";
        case EV_TINGLE:
            return "Tingle Raid";
        case EV_SIMON:
            return "Salesman Game";
        case EV_REFLEX:
            return "Reflex Test";
        case EV_STARFALL:
            return "Starfall";
        default:
            return Events_Name(ev);
    }
}

// ---------------------------------------------------------------------------
// Title cards
// ---------------------------------------------------------------------------

typedef enum { CARD_EVENT, CARD_COMBO, CARD_CURSE, CARD_ROULETTE } CardStyle;

#define CARD_FRAMES (5 * FPS)

static char sCardName[48];
static char sCardSub[48];
static s32 sCardStyle = CARD_EVENT;
static s32 sCardFadeIn = true;
static s32 sCurseCardTimer = 0;
static s32 sCurseCardPending = false;

// The event banner (same look as before 1.1), also used for the roulette
// and the cycle curse.
static Gfx* Card_Draw(PlayState* play, Gfx* gfx, s32 style, const char* name, const char* sub, s32 elapsed,
                      s32 left, s32 fadeIn) {
    static const char* sHeaders[4] = { "!! GLOBAL EVENT !!", "!! COMBO EVENT !!", "!! CURSE OF THE CYCLE !!",
                                       "!! GLOBAL EVENT !!" };
    const char* header = sHeaders[style & 3];
    s32 alpha = (left > 20) ? 220 : left * 11;
    u8 flash = (play->gameplayFrames & 4) ? 255 : 120;
    s32 curse = (style == CARD_CURSE);
    u8 lr = curse ? 170 : 230;
    u8 lg = curse ? 60 : 30;
    u8 lb = curse ? 230 : 30;
    GfxPrint printer;
    char line[52];
    s32 n;

    gfx = Ui_DrawRect(gfx, 40, 64, SCREEN_WIDTH - 40, 110, curse ? 10 : 20, 0, curse ? 20 : 0, alpha);
    gfx = Ui_DrawRect(gfx, 40, 64, SCREEN_WIDTH - 40, 66, lr, lg, lb, alpha);
    gfx = Ui_DrawRect(gfx, 40, 108, SCREEN_WIDTH - 40, 110, lr, lg, lb, alpha);

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2);

    if (curse) {
        GfxPrint_SetColor(&printer, 200, flash / 3, 255, alpha);
    } else {
        GfxPrint_SetColor(&printer, 255, flash / 3, flash / 3, alpha);
    }
    Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(header) * 8) / 2, 70, header);

    if (style == CARD_ROULETTE) {
        GfxPrint_SetColor(&printer, 255, (play->gameplayFrames & 2) ? 230 : 190, 80, alpha);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(name) * 8) / 2, 82, name);
    } else {
        line[0] = (char)GFXP_RAINBOW_ON_CHAR;
        n = Ev_Append(line, 1, name, sizeof(line) - 1);
        line[n++] = (char)GFXP_RAINBOW_OFF_CHAR;
        line[n] = '\0';
        GfxPrint_SetColor(&printer, 255, 255, 255, alpha);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(name) * 8) / 2, 82, line);
    }

    GfxPrint_SetColor(&printer, 230, 230, 230, alpha);
    Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sub) * 8) / 2, 94, sub);

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    return gfx;
}

static void Card_ComboName(char* out, s32 size, s32 ev, s32 combo) {
    s32 n;
    // Doubled: a pick of X combo X (the roulette), or the running doubled event.
    s32 doubled = (combo == ev) || ((combo < 0) && (ev == sActive) && Ev_IsDoubled());

    if (doubled) {
        combo = -1;
    }
    if (Curse_Is(CURSE_UNSEEN)) {
        Ev_Append(out, 0, (combo >= 0) ? "??? + ???" : "???", size);
        return;
    }
    n = Ev_Append(out, 0, (combo >= 0) ? Events_ShortName(ev) : Events_Name(ev), size);

    if (combo >= 0) {
        n = Ev_Append(out, n, " + ", size);
        Ev_Append(out, n, Events_ShortName(combo), size);
    } else if (doubled) {
        Ev_Append(out, n, " x2", size);
    }
}

// ---------------------------------------------------------------------------
// Run report
// ---------------------------------------------------------------------------

static u16 sStatSeen[EV_COUNT];
static u16 sStatWon[EV_COUNT];
static u16 sStatDied[EV_COUNT];
static u16 sStatCombos = 0;
static s32 sDiedThisEvent = false;
static s32 sEventFailed = false; // lost without dying (Moonlit Prey got away): no survival credit
static s32 sEndQuiet = false; // ended by the menu, a new cycle, or a failed setup: not a survival
static s32 sLastEnded = -1;
static s32 sLastEndedCombo = -1;
static s32 sPostEventFrames = 0;
static u16 sPrevHealth = 0;
static s32 sStreak = 0;
static s32 sBestStreak = 0;
static s32 sWrath = 0;
#define WRATH_MAX 5

// Event Log: the last 10 events, newest first.
typedef enum { LOG_OK, LOG_KO, LOG_STOPPED } LogResult;
#define LOG_COUNT 10
typedef struct {
    s8 ev;
    s8 combo;
    u8 result;
} LogEntry;
static LogEntry sLog[LOG_COUNT];
static s32 sLogCount = 0;

static void Log_Push(s32 ev, s32 combo, s32 result) {
    s32 i;

    for (i = LOG_COUNT - 1; i > 0; i--) {
        sLog[i] = sLog[i - 1];
    }
    sLog[0].ev = ev;
    sLog[0].combo = combo;
    sLog[0].result = result;
    sLogCount = MIN(sLogCount + 1, LOG_COUNT);
}
static s32 sAftershockPending = false;
static s32 sIsAftershock = false;
static void Curse_PickFavorite(void);

static void Stats_Reset(void) {
    s32 i;

    sStreak = sBestStreak = 0;
    sWrath = 0;
    sLogCount = 0;
    for (i = 0; i < EV_COUNT; i++) {
        sStatSeen[i] = sStatWon[i] = sStatDied[i] = 0;
    }
    sStatCombos = 0;
}

static void Stats_OnStart(s32 ev, s32 combo) {
    sDiedThisEvent = false;
    sEventFailed = false;
    if ((ev >= 0) && (ev < EV_COUNT) && (sStatSeen[ev] < 9999)) {
        sStatSeen[ev]++;
    }
    if ((combo >= 0) && (combo < EV_COUNT)) {
        if (sStatSeen[combo] < 9999) {
            sStatSeen[combo]++;
        }
        if (sStatCombos < 9999) {
            sStatCombos++;
        }
    }
}

static void Stats_OnEnd(void) {
    s32 prim = sInComboPass ? sComboPrimary : sActive;
    s32 lost = sDiedThisEvent || sEventFailed;

    if ((prim < 0) || (prim >= EV_COUNT)) {
        return;
    }
    V2_OnEventEnded(prim, sEndQuiet, lost);
    if (!sEndQuiet && !lost) {
        if (sStatWon[prim] < 9999) {
            sStatWon[prim]++;
        }
        if ((sCombo >= 0) && (sCombo < EV_COUNT) && (sStatWon[sCombo] < 9999)) {
            sStatWon[sCombo]++;
        }
    }
    // Aftershock and Encore only follow an event you got through.
    if (!sEndQuiet && !lost && !sIsAftershock) {
        sAftershockPending = true;
    }
    if (!sEndQuiet && !lost && !sIsEncore && !sIsAftershock) {
        sEncoreEvent = prim;
    }
    Log_Push(prim, sCombo, lost ? LOG_KO : sEndQuiet ? LOG_STOPPED : LOG_OK);
    if (sEventFailed) {
        sStreak = 0;
    }
    if (!sEndQuiet && !lost) {
        sStreak++;
        sBestStreak = MAX(sBestStreak, sStreak);
        if (gOpt[ID_EV_STREAKS] && ((sStreak % 5) == 0)) {
            static char sStreakToast[24];
            s32 n = Ev_AppendNum(sStreakToast, 0, sStreak, sizeof(sStreakToast));

            Ev_Append(sStreakToast, n, " in a row!", sizeof(sStreakToast));
            Menu_ShowToast(sStreakToast);
            Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
        }
        if (Curse_Is(CURSE_WRATH) && !sWrathEvent) {
            sWrath++;
            if (sWrath >= WRATH_MAX) {
                sWrathPending = true;
                sWrath = 0;
                Menu_ShowToast("The moon grows furious...");
            }
        }
    }
    sWrathEvent = false;
    sIsAftershock = false;
    sIsEncore = false;
    sLastEnded = prim;
    sLastEndedCombo = sCombo;
    sPostEventFrames = 5 * FPS; // a bomb can still go off right after
}

static void Stats_AddDeath(s32 ev) {
    if ((ev >= 0) && (ev < EV_COUNT) && (sStatDied[ev] < 9999)) {
        sStatDied[ev]++;
    }
}

// Link's health hitting zero counts as a death to the running event (or one
// that just ended), fairy or not.
static void Stats_CheckDeath(PlayState* play) {
    u16 hp = gSaveContext.save.saveInfo.playerData.health;

    if ((sPrevHealth > 0) && (hp == 0)) {
        if (sActive >= 0) {
            Stats_AddDeath(sActive);
            Stats_AddDeath(sCombo);
            sDiedThisEvent = true;
            // A bottled fairy brings Link right back: the event carries on.
            sEndOnDeath = !Inventory_HasItemInBottle(ITEM_FAIRY);
            sStreak = 0;
            sMercyPending = gOpt[ID_EV_MERCY];
        } else if (sPostEventFrames > 0) {
            Stats_AddDeath(sLastEnded);
            Stats_AddDeath(sLastEndedCombo);
            sStreak = 0;
            if ((sLogCount > 0) && (sLog[0].result == LOG_OK)) {
                sLog[0].result = LOG_KO;
            }
            if (gOpt[ID_EV_MERCY]) {
                sUntilNext *= 2;
            }
        }
    }
    sPrevHealth = hp;
}

static s32 Stats_TopKiller(void) {
    s32 best = -1;
    s32 i;

    for (i = 0; i < EV_COUNT; i++) {
        if ((sStatDied[i] > 0) && ((best < 0) || (sStatDied[i] > sStatDied[best]))) {
            best = i;
        }
    }
    return best;
}

static s32 Stats_Sum(u16* arr) {
    s32 total = 0;
    s32 i;

    for (i = 0; i < EV_COUNT; i++) {
        total += arr[i];
    }
    return total;
}

// ---------------------------------------------------------------------------
// Cycle curse: one twist for the whole three days, rolled at each new cycle.
// Curses only change how events behave, never Link's controls.
// ---------------------------------------------------------------------------

static u16 sPrevClock = 0;

static s32 Curse_Is(s32 curse) {
    // Picked in the Moon Draft this cycle (retired curses do nothing).
    return gOpt[ID_V2_MUT] && gOpt[ID_EV_CURSE] && (curse >= 0) && (curse < CURSE_COUNT) &&
           (sCurseMask & (1u << curse)) && Curse_CanUse(curse);
}

static s32 Curse_CanUse(s32 curse);

static const char* Curse_Name(s32 curse) {
    switch (curse) {
        case CURSE_RESTLESS:
            return "Restless Moon";
        case CURSE_LINGER:
            return "Lingering Dread";
        case CURSE_FIERCE:
            return "Fierce Moon";
        case CURSE_TWIN:
            return "Twin Omens";
        case CURSE_FAVORITE:
            return "Moon's Favorite";
        case CURSE_BLOODNIGHT:
            return "Blood Nights";
        case CURSE_NIGHTTERROR:
            return "Night Terrors";
        case CURSE_SILENT:
            return "Silent Moon";
        case CURSE_UNSEEN:
            return "Unseen Omen";
        case CURSE_AFTERSHOCK:
            return "Aftershocks";
        case CURSE_DOOM:
            return "Impending Doom";
        case CURSE_LEGION:
            return "Legion";
        case CURSE_GLASS:
            return "Glass Moon";
        case CURSE_ENCORE:
            return "Encore";
        case CURSE_METEOR:
            return "Meteor Shower";
        case CURSE_AMBUSH:
            return "Ambush";
        case CURSE_BELL:
            return "Bell Toll";
        case CURSE_HEX:
            return "Hex";
        case CURSE_WRATH:
            return "Moon's Wrath";
        case CURSE_FLOAT:
            return "Weightless Moon";
        default:
            return "None";
    }
}

static void Curse_DescribeId(s32 curse, char* out, s32 size) {
    switch (curse) {
        case CURSE_RESTLESS:
            Ev_Append(out, 0, "Events come 50% more often.", size);
            break;
        case CURSE_LINGER:
            Ev_Append(out, 0, "Events last 50% longer.", size);
            break;
        case CURSE_FIERCE:
            Ev_Append(out, 0, "Events hit at Brutal.", size);
            break;
        case CURSE_TWIN:
            Ev_Append(out, 0, "Combo events are much more common.", size);
            break;
        case CURSE_FAVORITE: {
            s32 n = Ev_Append(out, 0, (sCurseFavorite >= 0) ? Events_ShortName(sCurseFavorite) : "One event", size);

            Ev_Append(out, n, " comes up half the time.", size);
            break;
        }
        case CURSE_BLOODNIGHT:
            Ev_Append(out, 0, "A Blood Moon rises every night at 6 PM.", size);
            break;
        case CURSE_NIGHTTERROR:
            Ev_Append(out, 0, "Events come twice as often at night.", size);
            break;
        case CURSE_SILENT:
            Ev_Append(out, 0, "No warning or roulette before events.", size);
            break;
        case CURSE_UNSEEN:
            Ev_Append(out, 0, "Events hide their names.", size);
            break;
        case CURSE_AFTERSHOCK:
            Ev_Append(out, 0, "A short Bomb Rain follows every event.", size);
            break;
        case CURSE_DOOM:
            Ev_Append(out, 0, "Moon Pressure is on for this cycle.", size);
            break;
        case CURSE_LEGION:
            Ev_Append(out, 0, "Blood Moon hordes are twice as big.", size);
            break;
        case CURSE_GLASS:
            Ev_Append(out, 0, "You take double damage during events.", size);
            break;
        case CURSE_ENCORE:
            Ev_Append(out, 0, "Every event comes back a minute later.", size);
            break;
        case CURSE_METEOR:
            Ev_Append(out, 0, "Boulders fall on you during every event.", size);
            break;
        case CURSE_AMBUSH:
            Ev_Append(out, 0, "New areas may greet you with a Blood Moon.", size);
            break;
        case CURSE_BELL:
            Ev_Append(out, 0, "Events strike every 3 hours on the clock.", size);
            break;
        case CURSE_HEX:
            Ev_Append(out, 0, "Cursed Tag joins every event.", size);
            break;
        case CURSE_WRATH:
            Ev_Append(out, 0, "Every 5 survived, the next is a harder combo.", size);
            break;
        case CURSE_FLOAT:
            Ev_Append(out, 0, "Low gravity whenever an event is running.", size);
            break;
        default:
            Ev_Append(out, 0, "No curse picked yet.", size);
            break;
    }
}

static void Curse_Describe(char* out, s32 size) {
    Curse_DescribeId(sCurse, out, size);
}

static s32 Curse_CanUse(s32 curse) {
    s32 i;

    switch (curse) {
        // V3: retired curses (they overlapped others).
        case CURSE_NIGHTTERROR:
        case CURSE_UNSEEN:
        case CURSE_AFTERSHOCK:
        case CURSE_METEOR:
        case CURSE_DOOM:
        case CURSE_LEGION:
        case CURSE_AMBUSH:
        case CURSE_WRATH:
        case CURSE_FLOAT:
            return false;
        case CURSE_TWIN:
            return gOpt[ID_EV_COMBOS];
        case CURSE_BLOODNIGHT:
            return gOpt[ID_EV_ON_FIRST + EV_BLOOD_MOON] && Ev_AnyPoolEnabled();
        case CURSE_HEX:
            return gOpt[ID_EV_ON_FIRST + EV_CURSETAG];
        default:
            break;
    }
    if (curse == CURSE_FAVORITE) {
        for (i = 0; i < EV_COUNT; i++) {
            if (gOpt[ID_EV_ON_FIRST + i] && !Events_IsHidden(i)) {
                return true;
            }
        }
        return false;
    }
    return true;
}

static void Curse_PickFavorite(void) {
    s32 i;

    sCurseFavorite = -1;
    if (sCurse == CURSE_FAVORITE) {
        s32 events[EV_COUNT];
        s32 n = 0;

        for (i = 0; i < EV_COUNT; i++) {
            if (gOpt[ID_EV_ON_FIRST + i] && !Events_IsHidden(i)) {
                events[n++] = i;
            }
        }
        sCurseFavorite = (n > 0) ? events[(s32)(Rand_ZeroOne() * n) % n] : -1;
    }
}

// Cheat build: left/right on the Curse row swaps your newest curse pick for another (or none).
static void Pick_SwapCurse(s32 from, s32 to);
void Events_CycleCurse(s32 dir) {
    s32 next = sCurse + ((dir < 0) ? -1 : 1);

    if (next < -1) {
        next = CURSE_COUNT - 1;
    } else if (next >= CURSE_COUNT) {
        next = -1;
    }
    Pick_SwapCurse(sCurse, next);
    sCurseCardPending = false;
}

// Text shown in the menu footer while an info row is highlighted.
static const char* Events_Description(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN: return "Bombs fall from high up. Get under cover.";
        case EV_RUPOOR_RAIN: return "Rupoors rain down. Each one costs rupees.";
        case EV_BLOOD_MOON: return "A horde of one enemy type from your pool.";
        case EV_JOKE: return "Harmless critters flood the area.";
        case EV_MOONFALL: return "Boulders land where you're heading. Turn!";
        case EV_STALKER: return "Something hunts you and hits very hard.";
        case EV_NEMESIS: return "One big, tough enemy. Kill it to move on.";
        case EV_PHANTOM: return "Invisible soldiers. The Lens shows them.";
        case EV_TREASURE: return "Find the hidden rupee in time or be frozen.";
        case EV_HUNGER: return "Your health drains. Every kill heals you.";
        case EV_SEARCH: return "Avoid the spotlights or enemies come.";
        case EV_FATE: return "Salesmen creep closer when you look away.";
        case EV_REDLIGHT: return "Freeze when the moon watches or get zapped.";
        case EV_ECHO: return "A ghost walks your path. One touch kills.";
        case EV_KAMIKAZE: return "Fire Keese dive at you and explode.";
        case EV_CHAMPION: return "Duel a champion. Beat it to move on.";
        case EV_INFIGHT: return "Two enemy groups brawl right next to you.";
        case EV_STILL: return "Stand still and time stops. So does its clock.";
        case EV_IMPOSTER: return "Some carpenters are ReDeads in disguise.";
        case EV_UPRISING: return "Rainbow carpenters attack. Hit them to stop.";
        case EV_PROCESSION: return "A column of ReDeads marches. Don't cross it.";
        case EV_POLTERGEIST: return "Rocks circle you, flash red, then fly at you.";
        case EV_BULLETHELL: return "Mad Scrubs surround you and fire volleys.";
        case EV_MIRAGE: return "Tough foes come with fakes. Find the real ones.";
        case EV_MIRROR: return "Dancers copy your every move.";
        case EV_SWARM: return "A huge Keese swarm fills the sky.";
        case EV_MAJORA: return "Majora's Mask throws boss masks at your spot.";
        case EV_LEVIATHAN: return "A giant fish circles in the sky. Harmless.";
        case EV_TINGLE: return "Tingle drops bombs. Pop his balloon to stop him.";
        case EV_CURSETAG: return "Hit an enemy to pass the curse before it blows.";
        case EV_QUIZ: return "Four questions. Wrong costs half your hearts.";
        case EV_LOWGRAV: return "Low gravity: you jump higher and fall slowly.";
        case EV_PREY: return "Kill both glowing foes for rupees, or get hurt.";
        default: return V3_IsEvent(ev) ? V3_Counter(ev) : NULL;
    }
}

static s32 Ev_ForSettingsPage(s32 id) {
    switch (id) {
        case ID_SUB_EV_NEMESIS: return EV_NEMESIS;
        case ID_SUB_EV_PHANTOM: return EV_PHANTOM;
        case ID_SUB_EV_TREASURE: return EV_TREASURE;
        case ID_SUB_EV_HUNGER: return EV_HUNGER;
        case ID_SUB_EV_SEARCH: return EV_SEARCH;
        case ID_SUB_EV_FATE: return EV_FATE;
        case ID_SUB_EV_REDLIGHT: return EV_REDLIGHT;
        case ID_SUB_EV_ECHO: return EV_ECHO;
        case ID_SUB_EV_KAMIKAZE: return EV_KAMIKAZE;
        case ID_SUB_EV_CHAMPION: return EV_CHAMPION;
        case ID_SUB_EV_STILL: return EV_STILL;
        case ID_SUB_EV_IMPOSTER: return EV_IMPOSTER;
        case ID_SUB_EV_UPRISING: return EV_UPRISING;
        case ID_SUB_EV_PROCESSION: return EV_PROCESSION;
        case ID_SUB_EV_POLTERGEIST: return EV_POLTERGEIST;
        case ID_SUB_EV_BULLETHELL: return EV_BULLETHELL;
        case ID_SUB_EV_MIRAGE: return EV_MIRAGE;
        case ID_SUB_EV_MIRROR: return EV_MIRROR;
        case ID_SUB_EV_SWARM: return EV_SWARM;
        case ID_SUB_EV_MAJORA: return EV_MAJORA;
        case ID_SUB_EV_LEVIATHAN: return EV_LEVIATHAN;
        case ID_SUB_EV_TINGLE: return EV_TINGLE;
        case ID_SUB_EV_CURSETAG: return EV_CURSETAG;
        default: return -1;
    }
}

// "Choose Events >>" shows how many are on, like "25/30".
s32 Events_EnabledCount(s32* total) {
    s32 on = 0;
    s32 i;

    *total = 0;
    for (i = 0; i < EV_COUNT; i++) {
        if (!Events_IsHidden(i)) {
            (*total)++;
            if (gOpt[ID_EV_ON_FIRST + i]) {
                on++;
            }
        }
    }
    return on;
}

const char* Events_InfoDesc(s32 id) {
    static char sDesc[96];

    if ((id >= ID_SUB_V2) && (id <= ID_V2_LAST)) {
        return V2_Desc(id);
    }
    if (Ev_ForSettingsPage(id) >= 0) {
        return Events_Description(Ev_ForSettingsPage(id));
    }
    if ((id >= ID_EV_ON_FIRST) && (id < ID_EV_ON_FIRST + EV_COUNT)) {
        return Events_Description(id - ID_EV_ON_FIRST);
    }
    if ((id >= ID_EV_GO_FIRST) && (id < ID_EV_GO_FIRST + EV_COUNT)) {
        return "Start it now. Events you start pay no Marks.";
    }
    switch (id) {
        case ID_EV_MASTER: return "Random events on or off. Stays on across saves.";
        case ID_THEME: return "Color of this menu. Rainbow cycles.";
        case ID_SOUNDS: return "Clicks and chimes in this menu.";
#ifdef GE_CHEATS
        case ID_MOONJUMP: return "Hold L to rise. L also opens the Moon Pouch.";
#endif
#ifdef GE_CHEATS
        case ID_ALL_OFF: return "Stops events and every cheat. Moon Marks stay.";
#else
        case ID_ALL_OFF: return "Stops events. Moon Marks and Nemesis stay.";
#endif
        case ID_SUB_EVENTS: return "Events, curses, mutators and event settings.";
        case ID_EV_STATUS: return "What's running right now.";
        case ID_EV_TRIGGER: return "Start a random event right now.";
        case ID_EV_END: return "Stop the current event. 60 Moon Marks unless you started it.";
        case ID_EV_INTERVAL: return "About how long between events.";
        case ID_EV_DURATION: return "How long timed events last.";
        case ID_EV_INTENSITY: return "How hard events hit: Mild, Normal or Brutal.";
        case ID_SUB_EV_FEATURES: return "Roulette, combos, curses, timer and sounds.";
        case ID_SUB_EV_LIST: return "Event timing and settings, and which events can happen.";
        case ID_SUB_EV_START: return "Start any event right now.";
        case ID_SUB_EV_SETTINGS: return "Fine-tune each event.";
        case ID_SUB_EV_REPORT: return "Stats for this save: survived, deaths, combos.";
        case ID_EV_ROULETTE: return "A quick spin shows the event before it starts.";
        case ID_EV_COMBOS: return "Now and then two events run at once.";
        case ID_EV_CURSE: return "Curses can show up in the Moon Draft.";
        case ID_EV_PRESSURE: return "Events get worse as the moon gets closer.";
        case ID_RR_RESET: return "Clear the run report.";
        case ID_EV_SAFEZONES: return "No events in shops, minigames or boss rooms.";
        case ID_EV_TIMERSTYLE: return "Full, Compact or Hidden event timer.";
        case ID_EV_NEXTCLOCK: return "Always show time to the next event.";
        case ID_EV_WARNSOUND: return "Ticks during the 5 second warning.";
        case ID_SUB_EV_LOG: return "Your last 10 events and how they went.";
        case ID_EV_STREAKS: return "Cheers every 5 events survived in a row.";
        case ID_EV_HOURS: return "Events any time, only by day or only by night.";
        case ID_EV_MERCY: return "After an event kills you, the next one waits twice as long.";
        case ID_EV_WILD: return "Each event rolls its own random intensity.";
        case ID_SUB_EV_POOL: return "Enemies that events can use.";
        case ID_SUB_EV_CRITTERS: return "Critters the Joke Event can use.";
        case ID_RR_SEEN: return "Events that started this save.";
        case ID_RR_SURVIVED: return "Events that ended with you alive.";
        case ID_RR_DEATHS: return "Times an event took your last heart.";
        case ID_RR_COMBOS: return "Two-at-once events seen.";
        case ID_RR_STREAK: return "Events survived in a row right now.";
        case ID_RR_BEST: return "Your longest streak this save.";
        case ID_RR_WRATH: return "Moon's Wrath curse: at 5, a harder combo.";
        case ID_EV_ALL_ON: return "Turn every event on.";
        case ID_EV_ALL_OFF: return "Turn every event off.";
        default: break;
    }
    if (id != ID_EV_CURSE_INFO) {
        return NULL;
    }
    if (!gOpt[ID_EV_CURSE]) {
        return "Curses are off (Event Features).";
    }
    Curse_Describe(sDesc, sizeof(sDesc));
    {
        s32 c;
        s32 more = -1;

        for (c = 0; c < CURSE_COUNT; c++) {
            more += (sCurseMask >> c) & 1;
        }
        if (more > 0) {
            s32 n = Ui_StrLen(sDesc);

            Ev_Append(sDesc + n, 0, " Your Picks lists the rest.", (s32)sizeof(sDesc) - n);
        }
    }
    return sDesc;
}

// ---------------------------------------------------------------------------
// Moon Pressure: events speed up and toughen as the moon gets closer.
// ---------------------------------------------------------------------------

static s32 Ev_IsNight(void) {
    u16 time = gSaveContext.save.time;

    return (time >= 0xC000) || (time < 0x4000); // 6 PM to 6 AM
}

static s32 Pressure_Stage(void) {
    s32 day = gSaveContext.save.day;

    if (true || (day <= 1)) { // V3: Moon Pressure retired
        return 0;
    }
    if (day == 2) {
        return 1;
    }
    // Day 3 runs past midnight: the last six hours are the Final Hours.
    return (gSaveContext.save.time < 0x4000) ? 3 : 2;
}

static s32 Ev_IntensityBonus(void) {
    s32 bonus = 0; // V3: Moon Pressure no longer changes difficulty

    if (Curse_Is(CURSE_FIERCE)) {
        bonus++;
    }
    if (sWrathEvent) {
        bonus++;
    }
    if (sFriendEmpowered) {
        bonus++;
    }
    return bonus;
}

static f32 Ev_DurationMult(void) {
    return Curse_Is(CURSE_LINGER) ? 1.5f : 1.0f;
}

// Frames until the next random event. From 4 minutes up, the exact moment
// drifts by up to 30 seconds either way, so it can't be timed.
static s32 Ev_NextDelay(void) {
    static const f32 sPressure[4] = { 1.0f, 0.8f, 0.6f, 0.4f };
    f32 frames = IntervalFrames();

    if (Curse_Is(CURSE_ENCORE) && (sEncoreEvent >= 0)) {
        return 60 * FPS; // the encore comes a minute later
    }
    if (sMercyPending) {
        sMercyPending = false;
        frames *= 2.0f;
    }

    if (Curse_Is(CURSE_RESTLESS)) {
        frames *= 0.66f;
    }
    if (Curse_Is(CURSE_NIGHTTERROR) && Ev_IsNight()) {
        frames *= 0.5f;
    }
    frames *= sPressure[Pressure_Stage()];
    if ((gOpt[ID_EV_INTERVAL] % INTERVAL_MAX) >= INTERVAL_4) {
        frames += Rand_CenteredFloat(60.0f * FPS);
    }
    return MAX((s32)frames, 30 * FPS);
}

// ---------------------------------------------------------------------------
// Combo events: rarely, two run at once. Only events that just add hazards
// on top of whatever else is going on can be the second one.
// ---------------------------------------------------------------------------

static s32 Ev_IsOn(s32 ev) {
    return (ev >= 0) && ((sActive == ev) || (sCombo == ev));
}

static s32 Combo_CanBePrimary(s32 ev) {
    if (V3_IsEvent(ev)) {
        return false;
    }
    switch (ev) {
        case EV_NEMESIS:  // untimed
        case EV_CHAMPION: // untimed
        case EV_STILL:    // freezes everything when you stop
        case EV_REDLIGHT: // you must stand still
        case EV_GLARE:
        case EV_FATE: // grabs you
        case EV_QUIZ: // holds everything still
            return false;
        default:
            return !Events_IsHidden(ev);
    }
}

static s32 Combo_CanBeSecond(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN:
        case EV_RUPOOR_RAIN:
        case EV_MOONFALL:
        case EV_KAMIKAZE:
        case EV_POLTERGEIST:
        case EV_SWARM:
        case EV_MAJORA:
        case EV_TINGLE:
        case EV_LOWGRAV:
            return true;
        default:
            return false;
    }
}

// Friend combos only: any event you have on can ride along (the moon's own combos
// stick to the short list above).
static s32 Combo_CanBeSecondFriend(s32 ev, s32 primary) {
    return (ev >= 0) && (ev < EV_COUNT) && (ev != primary) && !Events_IsHidden(ev);
}

static s32 Combo_Pick(PlayState* play, s32 primary, s32 force) {
    s32 options[EV_COUNT];
    s32 count = 0;
    s32 i;
    f32 chance;

    if (!Combo_CanBePrimary(primary)) {
        return -1;
    }
    if (!gOpt[ID_EV_COMBOS]) {
        return -1;
    } else if (Curse_Is(CURSE_TWIN)) {
        chance = 1.0f / 3.0f;
    } else {
        chance = 1.0f / 8.0f;
    }
    if (!force && (Rand_ZeroOne() >= chance)) {
        return -1;
    }
    for (i = 0; i < EV_COUNT; i++) {
        if ((i != primary) && Combo_CanBeSecond(i) && !Events_IsHidden(i) && gOpt[ID_EV_ON_FIRST + i] &&
            Ev_CanRun(play, i) && V3_RandomOk(i)) {
            options[count++] = i;
        }
    }
    return (count > 0) ? options[(s32)(Rand_ZeroOne() * count) % count] : -1;
}

// ---------------------------------------------------------------------------
// Ending an event without it counting as survived.
// ---------------------------------------------------------------------------

static void Ev_Abort(PlayState* play) {
    sEndQuiet = true;
    Ev_End(play);
    sEndQuiet = false;
}

// ---------------------------------------------------------------------------
// Event roulette: a quick slot-machine spin before the event starts.
// ---------------------------------------------------------------------------

#define ROULETTE_FRAMES 66
#define ROULETTE_HOLD 16 // the result stays up this long before it starts

static s32 sRouletteTimer = 0;
static s32 sRoulettePick = -1;
static s32 sRouletteCombo = -1;
static s32 sRouletteShown = -1;
static s32 sRouletteStep = 0;

static s32 Roulette_RandomName(void) {
    s32 events[EV_COUNT];
    s32 n = 0;
    s32 i;

    for (i = 0; i < EV_COUNT; i++) {
        if (gOpt[ID_EV_ON_FIRST + i] && !Events_IsHidden(i) && (i != sRouletteShown)) {
            events[n++] = i;
        }
    }
    if (n == 0) {
        return sRoulettePick;
    }
    return events[(s32)(Rand_ZeroOne() * n) % n];
}

// (3.1.8) The moon's next pick, rolled ahead so friends can see it (and wager on it)
// before it starts. Rolled again if it can't happen where you are now.
static s32 sMoonNext = -1;

static s32 Ev_MoonNextOk(PlayState* play, s32 ev) {
    return (ev >= 0) && gOpt[ID_EV_ON_FIRST + ev] && !Events_IsHidden(ev) && Ev_CanRun(play, ev) && V3_RandomOk(ev);
}

static s32 Ev_PreviewNext(PlayState* play) {
    if ((sFriendNext >= 0) && gOpt[ID_EV_ON_FIRST + sFriendNext]) {
        return sFriendNext;
    }
    if (!Ev_MoonNextOk(play, sMoonNext) && ((sMoonNext < 0) || ((play->gameplayFrames % 10) == 0))) {
        sMoonNext = Ev_PickRandom(play);
    }
    return sMoonNext;
}

static void Ev_BeginRandom(PlayState* play) {
    s32 ev = Ev_MoonNextOk(play, sMoonNext) ? sMoonNext : Ev_PickRandom(play); // (what friends were shown)
    s32 combo;
    s32 friendPick = false;

    sMoonNext = -1;

    if (ev < 0) {
        sUntilNext = 30 * FPS; // nothing can run here right now; try again soon
        return;
    }
    // Moon's Mercy (Moon Service): this one never happens.
    if (V2_UseMercy()) {
        sUntilNext = Ev_NextDelay();
        return;
    }
    // Friend Link: your friend chose this one (if it can run here and is on).
    // Foresight: the event you were shown is the one you get (or, if it can't
    // happen here, the moon picks as usual and the vision is spent).
    if ((sFriendNext >= 0) && !gOpt[ID_EV_ON_FIRST + sFriendNext]) {
        sFriendNext = sFriendCombo = -1; // you turned it off since
        sForesight = false;
    } else if ((sFriendNext >= 0) && Ev_CanRun(play, sFriendNext)) {
        ev = sFriendNext;
        friendPick = true;
        sStartByFriend = !sForesight;
        sFriendNext = -1;
        sForesight = false;
    } else if (sForesight) {
        sFriendNext = sFriendCombo = -1;
        sForesight = false;
        Menu_ShowToast("The foreseen event can't happen here.");
    }
    // Encore (curse): the last event comes back.
    if (!friendPick && Curse_Is(CURSE_ENCORE) && (sEncoreEvent >= 0)) {
        s32 encore = sEncoreEvent;

        sEncoreEvent = -1;
        if (gOpt[ID_EV_ON_FIRST + encore] && Ev_CanRun(play, encore)) {
            Ev_StartFull(play, encore, -1, false);
            sIsEncore = true;
            Ev_Append(sCardSub, 0, "Encore!", sizeof(sCardSub));
            return;
        }
    }
    sEncoreEvent = -1;
    if (friendPick) {
        combo = ((sFriendCombo >= 0) && (sFriendCombo != ev) && gOpt[ID_EV_COMBOS] &&
                 gOpt[ID_EV_ON_FIRST + sFriendCombo] && Combo_CanBePrimary(ev) && Combo_CanBeSecondFriend(sFriendCombo, ev) &&
                 Ev_CanRun(play, sFriendCombo))
                    ? sFriendCombo
                    : -1;
        if ((sFriendCombo == ev) && gOpt[ID_EV_COMBOS] && Ev_CanDouble(ev)) {
            combo = ev; // doubled (X combo X)
        }
        sFriendCombo = -1;
    } else {
        combo = Combo_Pick(play, ev, sWrathPending);
        if (combo < 0) {
            combo = Curse_ForcedCombo(play, ev);
        }
    }
    if (gOpt[ID_EV_ROULETTE] && !Curse_Is(CURSE_SILENT) && !Curse_Is(CURSE_UNSEEN)) {
        sRoulettePick = ev;
        sRouletteCombo = combo;
        sRouletteShown = -1;
        sRouletteShown = Roulette_RandomName();
        sRouletteTimer = ROULETTE_FRAMES;
        sRouletteStep = 1;
    } else {
        Ev_StartFull(play, ev, combo, false);
    }
}

static void Roulette_Update(PlayState* play) {
    s32 spin = ROULETTE_FRAMES - ROULETTE_HOLD;
    s32 elapsed = ROULETTE_FRAMES - sRouletteTimer;

    if (sRouletteTimer > ROULETTE_HOLD) {
        if (--sRouletteStep <= 0) {
            f32 u = (f32)elapsed / spin;

            sRouletteShown = Roulette_RandomName();
            sRouletteStep = 1 + (s32)(u * u * 7.0f); // slows down as it goes
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    } else if (sRouletteTimer == ROULETTE_HOLD) {
        sRouletteShown = sRoulettePick;
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    }
    if (--sRouletteTimer <= 0) {
        sRouletteTimer = 0;
        if (Ev_CanRun(play, sRoulettePick)) {
            if ((sRouletteCombo >= 0) && !Ev_CanRun(play, sRouletteCombo)) {
                sRouletteCombo = -1;
            }
            Ev_StartFull(play, sRoulettePick, sRouletteCombo, true);
        } else {
            sUntilNext = 10 * FPS; // it can't run here any more; try again soon
        }
    }
}

static Gfx* Roulette_Draw(PlayState* play, Gfx* gfx) {
    s32 elapsed = ROULETTE_FRAMES - sRouletteTimer;
    s32 landed = (sRouletteTimer <= ROULETTE_HOLD);
    char name[48];

    if (landed) {
        Card_ComboName(name, sizeof(name), sRoulettePick, sRouletteCombo);
    } else {
        Ev_Append(name, 0, (sRouletteShown >= 0) ? Events_Name(sRouletteShown) : "???", sizeof(name));
    }
    return Card_Draw(play, gfx, (landed && (sRouletteCombo >= 0)) ? CARD_COMBO : CARD_ROULETTE, name,
                     landed ? "" : "- - -", elapsed, CARD_FRAMES, true);
}

// ---------------------------------------------------------------------------
// Event title card
// ---------------------------------------------------------------------------

static void Card_ShowEvent(s32 ev, s32 combo, s32 fromRoulette) {
    Card_ComboName(sCardName, sizeof(sCardName), ev, combo);
    sCardStyle = ((combo >= 0) || Ev_IsDoubled()) ? CARD_COMBO : CARD_EVENT;
    sCardFadeIn = !fromRoulette;
    if (sWrathEvent) {
        Ev_Append(sCardSub, 0, "The moon's wrath!", sizeof(sCardSub));
        sCardStyle = CARD_COMBO;
    } else if (Curse_Is(CURSE_UNSEEN)) {
        Ev_Append(sCardSub, 0, "Something is happening...", sizeof(sCardSub));
    } else if (combo >= 0) {
        Ev_Append(sCardSub, 0, "Two at once!", sizeof(sCardSub));
    } else if (Ev_IsDoubled()) {
        Ev_Append(sCardSub, 0, "Doubled: twice as fierce!", sizeof(sCardSub));
    } else if ((ev == EV_BLOOD_MOON) && (sBloodMoonPick >= 0)) {
        Ev_Append(sCardSub, 0, Pool_Name(sBloodMoonPick), sizeof(sCardSub));
    } else if ((ev == EV_NEMESIS) && (sNemPick >= 0)) {
        Ev_Append(sCardSub, 0, Pool_Name(sNemPick), sizeof(sCardSub));
    } else {
        Ev_Append(sCardSub, 0, Events_Flavor(ev), sizeof(sCardSub));
    }
}

static Gfx* Card_DrawEvent(PlayState* play, Gfx* gfx) {
    const char* sub = sCardSub;

    // Blood Moon's enemy can change once the area is checked (Odolwa needs low ground).
    if ((sActive == EV_BLOOD_MOON) && (sCombo < 0) && (sBloodMoonPick >= 0) && !Curse_Is(CURSE_UNSEEN) &&
        !sIsAftershock) {
        sub = Pool_Name(sBloodMoonPick);
    }
    return Card_Draw(play, gfx, sCardStyle, sCardName, sub, CARD_FRAMES - sBannerTimer, sBannerTimer, sCardFadeIn);
}

// Aftershocks (curse): a short Bomb Rain right after each event you get through.
static s32 Aftershock_Update(PlayState* play) {
    if (!sAftershockPending) {
        return false;
    }
    sAftershockPending = false;
    if (!Curse_Is(CURSE_AFTERSHOCK) || !Ev_CanRun(play, EV_BOMB_RAIN)) {
        return false;
    }
    Ev_StartFull(play, EV_BOMB_RAIN, -1, false);
    sIsAftershock = true;
    sTotalFrames = sFramesLeft = 15 * FPS;
    Ev_Append(sCardName, 0, Curse_Is(CURSE_UNSEEN) ? "???" : "Aftershock", sizeof(sCardName));
    Ev_Append(sCardSub, 0, "The moon isn't done yet.", sizeof(sCardSub));
    return true;
}

static Gfx* Card_DrawCurse(PlayState* play, Gfx* gfx) {
    char sub[48];

    Curse_Describe(sub, sizeof(sub));
    return Card_Draw(play, gfx, CARD_CURSE, Curse_Name(sCurse), sub, CARD_FRAMES - sCurseCardTimer, sCurseCardTimer,
                     true);
}

// Blood Nights: a Blood Moon at 6 PM each night.
// Runs every frame so the clock is always fresh; only fires when nothing
// else is going on.
static void Curse_NightCheck(PlayState* play, s32 playing) {
    u16 now = gSaveContext.save.time;
    u16 prev = sPrevClock;

    sPrevClock = now;
    if (!playing || !gOpt[ID_EV_MASTER] || (sActive >= 0) || (sRouletteTimer > 0) || (sCurseCardTimer > 0)) {
        return;
    }
    if (sPauseReason != 0) {
        return;
    }
    if (Curse_Is(CURSE_BLOODNIGHT) && (prev < 0xC000) && (now >= 0xC000) && ((u16)(now - prev) < 0x800) &&
        Ev_CanRun(play, EV_BLOOD_MOON) && gOpt[ID_EV_ON_FIRST + EV_BLOOD_MOON]) {
        Ev_StartFull(play, EV_BLOOD_MOON, -1, false);
        return;
    }
    // Bell Toll: every 3 hours on the clock (midnight, 3, 6, 9...).
    if (Curse_Is(CURSE_BELL) && ((prev >> 13) != (now >> 13)) && ((u16)(now - prev) < 0x800)) {
        Audio_PlaySfx(NA_SE_SY_TIMESIGNAL_BELL);
        Ev_BeginRandom(play);
    }
}

// ===========================================================================
// New events
// ===========================================================================

// ---------------------------------------------------------------------------
// Falling bombs (Tingle Air Raid): each drops onto a spot marked on the
// ground and bursts there. Only Link gets hurt.
// ---------------------------------------------------------------------------

#define MAX_DROPS 12
#define BOMB_CAP_DL ((Gfx*)0x04015FA0)  // gBombCapDL
#define BOMB_BODY_DL ((Gfx*)0x04015DB0) // gBombBodyDL

typedef struct {
    Vec3f from;
    Vec3f to;
    CollisionPoly* poly;
    s16 timer;
    s16 frames;
    s16 active;
} BombDrop;

static BombDrop sDrops[MAX_DROPS];

static void Drops_Clear(void) {
    s32 i;

    for (i = 0; i < MAX_DROPS; i++) {
        sDrops[i].active = false;
        sDrops[i].poly = NULL;
    }
}

static void Drop_Launch(PlayState* play, Vec3f* from, Vec3f* target, s32 frames) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe = *target;
    f32 floorY;
    s32 i;

    for (i = 0; (i < MAX_DROPS) && sDrops[i].active; i++) {}
    if (i == MAX_DROPS) {
        return;
    }
    probe.y += 60.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if ((floorY <= BGCHECK_Y_MIN) || (floorY > from->y)) {
        return;
    }
    sDrops[i].from = *from;
    sDrops[i].to = *target;
    sDrops[i].to.y = floorY;
    sDrops[i].poly = poly;
    sDrops[i].timer = 0;
    sDrops[i].frames = MAX(frames, 1);
    sDrops[i].active = true;
}

static void Drop_Pos(BombDrop* d, Vec3f* out) {
    f32 u = CLAMP((f32)d->timer / d->frames, 0.0f, 1.0f);

    out->x = d->from.x + (d->to.x - d->from.x) * u;
    out->y = d->from.y + (d->to.y - d->from.y) * u * u; // speeds up as it falls
    out->z = d->from.z + (d->to.z - d->from.z) * u;
}

static void Drops_Update(PlayState* play) {
    s32 i;

    for (i = 0; i < MAX_DROPS; i++) {
        BombDrop* d = &sDrops[i];

        if (d->active && (++d->timer >= d->frames)) {
            Vec3f blast = d->to;

            blast.y += 5.0f;
            d->active = false;
            Ev_SpawnLinkBomb(play, &blast, 1);
            Actor_RequestQuake(play, 2, 8);
        }
    }
}

static void Drops_Draw(PlayState* play) {
    s32 any = false;
    s32 i;

    for (i = 0; i < MAX_DROPS; i++) {
        BombDrop* d = &sDrops[i];

        if (d->active) {
            u8 a = 90 + (u8)(130.0f * CLAMP((f32)d->timer / d->frames, 0.0f, 1.0f));

            Ev_DrawGroundCircle(play, &d->to, d->poly, 1.1f, 255, 50, 0, a);
            any = true;
        }
    }
    if (!any) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);
    Ev_SetSkyLights(play, 160, 160, 160);

    for (i = 0; i < MAX_DROPS; i++) {
        BombDrop* d = &sDrops[i];
        Vec3f pos;
        u8 red = (play->gameplayFrames & 2) ? 220 : 0;

        if (!d->active) {
            continue;
        }
        Drop_Pos(d, &pos);
        Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_NEW);
        Matrix_Scale(0.015f, 0.015f, 0.015f, MTXMODE_APPLY);

        OPEN_DISPS(play->state.gfxCtx);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, BOMB_CAP_DL);
        Matrix_ReplaceRotation(&play->billboardMtxF);
        Matrix_RotateXS(0x4000, MTXMODE_APPLY);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, red, 0, 40, 255);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, red, 0, 40, 255);
        gSPDisplayList(POLY_OPA_DISP++, BOMB_BODY_DL);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// ---------------------------------------------------------------------------
// Tingle Air Raid: Tingle circles overhead on his balloon and drops bombs on
// marked spots. Pop the balloon (arrow, hookshot, Zora fins, bubble) to end it.
// He's drawn by us from his model; no Tingle actor is ever spawned, so no map
// shop and nothing to buy.
// ---------------------------------------------------------------------------

#define TINGLE_SKEL ((FlexSkeletonHeader*)0x0600A6D0)    // gTingleSkel
#define TINGLE_FLOAT_ANIM ((AnimationHeader*)0x060005FC) // gTingleFloatIdleAnim
#define TINGLE_FALL_ANIM ((AnimationHeader*)0x06000840)  // gTingleFallAnim
#define TINGLE_EYES 0x6050                               // gTingleEyeOpenTex
#define TINGLE_LIMBS (32 + 1)
#define TINGLE_SCALE 0.02f

static SkelAnime sTgSkel;
static Vec3s sTgJoints[TINGLE_LIMBS + 3];
static Vec3s sTgMorph[TINGLE_LIMBS + 3];
static s32 sTgReady = false;
static s32 sTgFalling = false;
static s32 sTgGone = false; // popped: gone for the rest of this event
static Vec3f sTgPos;
static s16 sTgAngle = 0;
static s16 sTgYaw = 0;
static f32 sTgFallSpeed = 0.0f;
static s32 sTgFallFrames = 0;
static s32 sTgDropTimer = 0;
static s32 sTgBanner = 0;

static void Tingle_Setup(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_BAL);
    Player* player = GET_PLAYER(play);
    uintptr_t saved = gSegments[6];

    sTgReady = false;
    sTgFalling = false;
    if ((buf == NULL) || (player == NULL) || sTgGone) {
        return;
    }
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_InitFlex(play, &sTgSkel, TINGLE_SKEL, TINGLE_FLOAT_ANIM, sTgJoints, sTgMorph, TINGLE_LIMBS);
    gSegments[6] = saved;

    sTgAngle = (s16)(Rand_ZeroOne() * 0xFFFF);
    sTgPos.x = player->actor.world.pos.x + Math_SinS(sTgAngle) * 600.0f;
    sTgPos.y = Ev_SkyHeight(play, &player->actor.world.pos, 250.0f);
    sTgPos.z = player->actor.world.pos.z + Math_CosS(sTgAngle) * 600.0f;
    sTgYaw = Math_Vec3f_Yaw(&sTgPos, &player->actor.world.pos);
    sTgDropTimer = 2 * FPS;
    sTgReady = true;
    SoundSource_PlaySfxAtFixedWorldPos(play, &sTgPos, 30, NA_SE_VO_TIVO00);
}

// Anything Link shoots or throws that reaches him pops the balloon.
// V3: any projectile counts: arrows, nuts, bubbles, hookshot, Zora fins, and
// Link's own bombs or bombchus going off near him.
static s32 Tingle_Hit(PlayState* play) {
    static const u8 sCats[] = { ACTORCAT_ITEMACTION, ACTORCAT_EXPLOSIVES };
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            if ((actor->update != NULL) && !Tag_IsOurs(Tag_Of(actor)) &&
                ((sCats[c] == ACTORCAT_EXPLOSIVES) || (actor->id == ACTOR_EN_ARROW) ||
                 (actor->id == ACTOR_ARMS_HOOK) || (actor->id == ACTOR_EN_BOOM))) {
                f32 dy = actor->world.pos.y - sTgPos.y;
                f32 reach = (sCats[c] == ACTORCAT_EXPLOSIVES) ? 120.0f : 70.0f;

                if ((Ev_DistXZ(&actor->world.pos, &sTgPos) < reach) && (dy > -40.0f) && (dy < 200.0f)) {
                    return true;
                }
            }
            actor = actor->next;
        }
    }
    return false;
}

static void Tingle_SetAnim(AnimationHeader* anim, f32 speed) {
    void* buf = Obj_GetBuffer(OBJECT_BAL);
    uintptr_t saved = gSegments[6];

    if (buf == NULL) {
        return;
    }
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    Animation_Change(&sTgSkel, anim, speed, 0.0f, Animation_GetLastFrame(anim), ANIMMODE_LOOP, -4.0f);
    gSegments[6] = saved;
}

static void Tick_Tingle(PlayState* play) {
    static const u8 sEvery[LVL_MAX] = { 70, 50, 34 };
    static const u8 sFall[INTENSITY_MAX] = { 34, 30, 26 };
    Player* player = GET_PLAYER(play);
    void* buf = Obj_GetBuffer(OBJECT_BAL);
    uintptr_t saved;

    Drops_Update(play);
    if (sTgBanner > 0) {
        sTgBanner--;
    }
    if (!sTgReady || (player == NULL) || (buf == NULL)) {
        return;
    }
    saved = gSegments[6];
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_Update(&sTgSkel);
    gSegments[6] = saved;

    if (sTgFalling) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f probe = sTgPos;
        f32 floorY;

        sTgFallSpeed = MIN(sTgFallSpeed + 1.2f, 22.0f);
        sTgPos.y -= sTgFallSpeed;
        sTgYaw += 0x1400;
        probe.y += 40.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (((floorY > BGCHECK_Y_MIN) && (sTgPos.y <= floorY)) || (++sTgFallFrames > 60)) {
            if (floorY > BGCHECK_Y_MIN) {
                sTgPos.y = MAX(sTgPos.y, floorY);
            }
            Ev_Poof(play, &sTgPos, 0);
            SoundSource_PlaySfxAtFixedWorldPos(play, &sTgPos, 20, NA_SE_EN_GERUDOFT_DOWN);
            sTgReady = false;
            sTgGone = true;
            if (!sInComboPass && (sActive == EV_TINGLE)) {
                Menu_ShowToast("Tingle is down!");
                Ev_End(play);
            } else {
                Menu_ShowToast("Tingle is down!");
            }
        }
        return;
    }

    // Lazy circles over Link, bobbing on the balloon.
    {
        Vec3f want;

        sTgAngle += 0x80;
        want.x = player->actor.world.pos.x + Math_SinS(sTgAngle) * 240.0f;
        want.y = Ev_SkyHeight(play, &player->actor.world.pos, 250.0f) +
                 Math_SinS((s16)(sEventFrames * 0x320)) * 20.0f;
        want.z = player->actor.world.pos.z + Math_CosS(sTgAngle) * 240.0f;
        sTgPos.x += (want.x - sTgPos.x) * 0.05f;
        sTgPos.y += (want.y - sTgPos.y) * 0.05f;
        sTgPos.z += (want.z - sTgPos.z) * 0.05f;
        Math_SmoothStepToS(&sTgYaw, Math_Vec3f_Yaw(&sTgPos, &player->actor.world.pos), 4, 0x800, 0x80);
    }

    if (Tingle_Hit(play)) {
        sTgFalling = true;
        sTgFallSpeed = 0.0f;
        sTgFallFrames = 0;
        sTgBanner = 3 * FPS;
        SoundSource_PlaySfxAtFixedWorldPos(play, &sTgPos, 20, NA_SE_EV_MUJURA_BALLOON_BROKEN);
        SoundSource_PlaySfxAtFixedWorldPos(play, &sTgPos, 30, NA_SE_VO_TIVO01);
        Tingle_SetAnim(TINGLE_FALL_ANIM, 1.5f);
        return;
    }

    // Bombs away: one right where Link stands, the rest close by.
    if (--sTgDropTimer <= 0) {
        s32 count = LVL3(ID_TA_BOMBS, 1, 2, 3) * (Ev_IsDoubled() ? 2 : 1);
        Vec3f from = sTgPos;
        Vec3f target = player->actor.world.pos;
        s32 k;

        from.y += 10.0f;
        Drop_Launch(play, &from, &target, sFall[Intensity()]);
        for (k = 1; k < count; k++) {
            s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
            f32 dist = 100.0f + Rand_ZeroOne() * 60.0f;

            target.x = player->actor.world.pos.x + Math_SinS(yaw) * dist;
            target.y = player->actor.world.pos.y;
            target.z = player->actor.world.pos.z + Math_CosS(yaw) * dist;
            Drop_Launch(play, &from, &target, sFall[Intensity()] + k * 4);
        }
        sTgDropTimer = sEvery[OptLvl(ID_TA_RATE)];
        SoundSource_PlaySfxAtFixedWorldPos(play, &sTgPos, 20, NA_SE_EV_CHINCLE_SPELL_EFFECT);
    }
}

static void Tingle_Draw(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_BAL);
    uintptr_t saved = gSegments[6];
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe;
    f32 floorY;

    if (!sTgReady || (buf == NULL)) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, buf);
    gSPSegment(POLY_OPA_DISP++, 0x08, (u8*)buf + TINGLE_EYES);
    CLOSE_DISPS(play->state.gfxCtx);
    Ev_SetSkyLights(play, 170, 170, 170);

    Matrix_Push();
    Matrix_Translate(sTgPos.x, sTgPos.y, sTgPos.z, MTXMODE_NEW);
    Matrix_RotateYS(sTgYaw, MTXMODE_APPLY);
    Matrix_Scale(TINGLE_SCALE, TINGLE_SCALE, TINGLE_SCALE, MTXMODE_APPLY);
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_DrawFlexOpa(play, sTgSkel.skeleton, sTgSkel.jointTable, sTgSkel.dListCount, NULL, NULL, NULL);
    gSegments[6] = saved;
    Matrix_Pop();

    // His shadow.
    probe = sTgPos;
    probe.y += 20.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY > BGCHECK_Y_MIN) {
        probe.y = floorY;
        Ev_DrawGroundCircle(play, &probe, poly, 0.45f, 0, 0, 0, 110);
    }
}

// ---------------------------------------------------------------------------
// Goron Stampede: rolling Gorons charge through in straight lanes. Each lane
// is marked on the ground a moment before they come through.
// ---------------------------------------------------------------------------

#define MAX_LANES 3
#define LANE_MARKS 25
#define LANE_HALF 650.0f
#define LANE_WIDTH 45.0f
#define GORON_DL ((Gfx*)0x060091A8) // gGoronRolledUpDL
#define GORON_SCALE 0.02f
#define GORON_RADIUS 28.0f

typedef enum { LANE_OFF, LANE_WARN, LANE_ROLL } LaneState;

typedef struct {
    u8 state;
    u8 hit;
    u8 markCount;
    s16 timer;
    s16 roll;
    s16 yaw;
    Vec3f center;
    f32 dirX;
    f32 dirZ;
    f32 s;
    Vec3f ball;
    Vec3f marks[LANE_MARKS];
    CollisionPoly* polys[LANE_MARKS];
} Lane;

static Lane sLanes[MAX_LANES];
static s32 sLaneRest = 0;

static void Lanes_Clear(void) {
    s32 i;

    for (i = 0; i < MAX_LANES; i++) {
        sLanes[i].state = LANE_OFF;
        sLanes[i].markCount = 0;
    }
}

static void Lane_Start(PlayState* play, Lane* lane, Vec3f* center, s16 yaw, s32 warnFrames) {
    s32 k;

    lane->state = LANE_WARN;
    lane->timer = warnFrames;
    lane->hit = false;
    lane->center = *center;
    lane->yaw = yaw;
    lane->dirX = Math_SinS(yaw);
    lane->dirZ = Math_CosS(yaw);
    lane->markCount = 0;
    for (k = 0; k < LANE_MARKS; k++) {
        f32 s = -LANE_HALF + k * (2.0f * LANE_HALF / (LANE_MARKS - 1));
        CollisionPoly* poly;
        s32 bgId;
        Vec3f probe;
        f32 floorY;

        probe.x = center->x + lane->dirX * s;
        probe.y = center->y + 150.0f;
        probe.z = center->z + lane->dirZ * s;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if ((floorY > BGCHECK_Y_MIN) && (fabsf(floorY - center->y) < 250.0f)) {
            probe.y = floorY;
            lane->marks[lane->markCount] = probe;
            lane->polys[lane->markCount] = poly;
            lane->markCount++;
        }
    }
    lane->s = -(LANE_HALF + 60.0f);
    lane->ball.x = center->x + lane->dirX * lane->s;
    lane->ball.y = center->y;
    lane->ball.z = center->z + lane->dirZ * lane->s;
    lane->roll = 0;
}

static void Stampede_NewWave(PlayState* play) {
    static const u8 sWarn[INTENSITY_MAX] = { 32, 26, 22 };
    static const f32 sOffsets[3][3] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 125.0f, 0.0f }, { -130.0f, 0.0f, 130.0f } };
    Player* player = GET_PLAYER(play);
    s32 count = LVL3(ID_GS_LANES, 1, 2, 3);
    s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
    f32 perpX = Math_CosS(yaw);
    f32 perpZ = -Math_SinS(yaw);
    f32 jitter = Rand_CenteredFloat(40.0f);
    f32 side = (Rand_ZeroOne() < 0.5f) ? -1.0f : 1.0f;
    s32 i;
    s32 tries;

    // In a corridor running along the lane there'd be nowhere to step aside:
    // turn the lane across it instead, or skip this wave.
    for (tries = 0; tries < 2; tries++) {
        Vec3f from = player->actor.world.pos;
        Vec3f toA;
        Vec3f toB;
        Vec3f hit;
        CollisionPoly* poly;
        s32 bgId;
        s32 blockedA;
        s32 blockedB;

        from.y += 30.0f;
        toA = toB = from;
        toA.x += perpX * 110.0f;
        toA.z += perpZ * 110.0f;
        toB.x -= perpX * 110.0f;
        toB.z -= perpZ * 110.0f;
        blockedA = BgCheck_EntityLineTest1(&play->colCtx, &from, &toA, &hit, &poly, true, false, false, true, &bgId);
        blockedB = BgCheck_EntityLineTest1(&play->colCtx, &from, &toB, &hit, &poly, true, false, false, true, &bgId);
        if (!(blockedA && blockedB)) {
            break;
        }
        yaw += 0x4000;
        perpX = Math_CosS(yaw);
        perpZ = -Math_SinS(yaw);
    }
    if (tries == 2) {
        return;
    }

    for (i = 0; i < count; i++) {
        Vec3f center = player->actor.world.pos;
        f32 off = sOffsets[count - 1][i] * ((count == 2) ? side : 1.0f) + jitter;

        center.x += perpX * off;
        center.z += perpZ * off;
        Lane_Start(play, &sLanes[i], &center, yaw, sWarn[Intensity()] + i * 6);
    }
    Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
}

static void Lane_Update(PlayState* play, Lane* lane, Player* player) {
    static const u8 sDamage[INTENSITY_MAX] = { 0x10, 0x18, 0x20 };
    f32 speed = LVL3(ID_GS_SPEED, 16.0f, 22.0f, 28.0f);
    Vec3f prev = lane->ball;
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe;
    Vec3f hitPos;
    f32 floorY;

    if (lane->state == LANE_WARN) {
        if (--lane->timer <= 0) {
            lane->state = LANE_ROLL;
            SoundSource_PlaySfxAtFixedWorldPos(play, &lane->ball, 20, NA_SE_EN_GOLON_ROLLING);
        }
        return;
    }
    if (lane->state != LANE_ROLL) {
        return;
    }

    lane->s += speed;
    lane->ball.x = lane->center.x + lane->dirX * lane->s;
    lane->ball.z = lane->center.z + lane->dirZ * lane->s;
    lane->roll += (s16)(speed / GORON_RADIUS * (0x8000 / M_PIf));

    // Walls stop it.
    {
        Vec3f from = prev;
        Vec3f to = lane->ball;

        from.y += GORON_RADIUS;
        to.y = from.y;
        if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hitPos, &poly, true, false, false, true, &bgId)) {
            Ev_Poof(play, &hitPos, 0);
            Actor_RequestQuake(play, 2, 6);
            SoundSource_PlaySfxAtFixedWorldPos(play, &hitPos, 20, NA_SE_EV_ROCK_BROKEN);
            lane->state = LANE_OFF;
            return;
        }
    }

    // Rolls over the ground.
    probe = lane->ball;
    probe.y += 70.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY > BGCHECK_Y_MIN) {
        if (floorY > lane->ball.y + 60.0f) {
            Ev_Poof(play, &lane->ball, 0);
            lane->state = LANE_OFF;
            return;
        }
        if (floorY < lane->ball.y) {
            lane->ball.y = MAX(floorY, lane->ball.y - 18.0f);
        } else {
            lane->ball.y = floorY;
        }
    } else {
        lane->ball.y -= 12.0f; // over a pit
    }
    if (lane->ball.y < lane->center.y - 500.0f) {
        lane->state = LANE_OFF;
        return;
    }

    if ((sEventFrames % 3) == 0) {
        Vec3f dust = lane->ball;

        func_800B3030(play, &dust, &gZeroVec3f, &gZeroVec3f, 60, 0, 0);
    }
    if ((sEventFrames % 6) == 0) {
        SoundSource_PlaySfxAtFixedWorldPos(play, &lane->ball, 8, NA_SE_EN_GOLON_ROLLING);
    }

    if (!lane->hit && (Ev_DistXZ(&lane->ball, &player->actor.world.pos) < GORON_RADIUS + 16.0f) &&
        (fabsf(player->actor.world.pos.y - lane->ball.y) < 60.0f)) {
        lane->hit = true;
        func_800B8D50(play, &player->actor, 12.0f, Math_Vec3f_Yaw(&lane->ball, &player->actor.world.pos), 8.0f,
                      sDamage[Intensity()]);
        Actor_RequestQuake(play, 3, 10);
    }
    if (lane->s > LANE_HALF + 60.0f) {
        lane->state = LANE_OFF;
    }
}

static void Tick_Stampede(PlayState* play) {
    static const u8 sRest[INTENSITY_MAX] = { 60, 45, 32 };
    Player* player = GET_PLAYER(play);
    s32 busy = false;
    s32 i;

    if (player == NULL) {
        return;
    }
    for (i = 0; i < MAX_LANES; i++) {
        Lane_Update(play, &sLanes[i], player);
        if (sLanes[i].state != LANE_OFF) {
            busy = true;
        }
    }
    if (!busy && (--sLaneRest <= 0)) {
        Stampede_NewWave(play);
        sLaneRest = sRest[Intensity()];
    }
}

static s32 Stampede_Warning(void) {
    s32 i;

    for (i = 0; i < MAX_LANES; i++) {
        if (sLanes[i].state == LANE_WARN) {
            return true;
        }
    }
    return false;
}

static void Stampede_Draw(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_OF1D_MAP);
    uintptr_t saved = gSegments[6];
    s32 anyBall = false;
    s32 i;
    s32 k;

    for (i = 0; i < MAX_LANES; i++) {
        Lane* lane = &sLanes[i];
        u8 a;

        if (lane->state == LANE_OFF) {
            continue;
        }
        if (lane->state == LANE_WARN) {
            a = (play->gameplayFrames & 2) ? 220 : 140;
        } else {
            a = 90;
            anyBall = true;
        }
        for (k = 0; k < lane->markCount; k++) {
            Ev_DrawGroundCircle(play, &lane->marks[k], lane->polys[k], LANE_WIDTH / 100.0f, 255, 140, 0, a);
        }
    }
    if (!anyBall || (buf == NULL)) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, buf);
    CLOSE_DISPS(play->state.gfxCtx);
    Ev_SetSkyLights(play, 170, 160, 150);
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    for (i = 0; i < MAX_LANES; i++) {
        Lane* lane = &sLanes[i];

        if (lane->state != LANE_ROLL) {
            continue;
        }
        Matrix_Translate(lane->ball.x, lane->ball.y + GORON_RADIUS, lane->ball.z, MTXMODE_NEW);
        Matrix_RotateYS(lane->yaw, MTXMODE_APPLY);
        Matrix_Scale(GORON_SCALE, GORON_SCALE, GORON_SCALE, MTXMODE_APPLY);
        Matrix_RotateXS(lane->roll, MTXMODE_APPLY);
        OPEN_DISPS(play->state.gfxCtx);
        MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
        gSPDisplayList(POLY_OPA_DISP++, GORON_DL);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    gSegments[6] = saved;
}

// ---------------------------------------------------------------------------
// Cursed Tag: a curse sits on Link with a fuse. Hurt an enemy to pass it on;
// a few seconds later it bursts on that enemy (stand clear) and comes back.
// If the fuse runs out on Link, it bursts on him.
// ---------------------------------------------------------------------------

#define CT_HOLD_FRAMES (5 * FPS)
#define TAGF_HP_SEEN (1 << 3)

static s32 sCtOnLink = false;
static s32 sCtFuse = 0;
static Actor* sCtHolder = NULL;
static Vec3f sCtHolderPos;
static s32 sCtHolderTimer = 0;
static s32 sCtWarmup = 0;

static s32 CurseTag_FuseFrames(void) {
    return LVL3(ID_CT_FUSE, 5, 10, 15) * FPS;
}

static void CurseTag_GiveLink(void) {
    sCtOnLink = true;
    sCtFuse = CurseTag_FuseFrames();
    sCtHolder = NULL;
    sCtHolderTimer = 0;
}

static s32 CurseTag_Marked(PlayState* play, Actor* actor) {
    Player* player = GET_PLAYER(play);

    if (!Ev_IsOn(EV_CURSETAG)) {
        return false;
    }
    if (sCtOnLink) {
        return (player != NULL) && (actor == &player->actor);
    }
    return (actor == sCtHolder);
}

static s32 Ev_ActorInList(PlayState* play, Actor* target, s32 category) {
    Actor* actor = play->actorCtx.actorLists[category].first;

    while (actor != NULL) {
        if (actor == target) {
            return actor->update != NULL;
        }
        actor = actor->next;
    }
    return false;
}

static void Tick_CurseTag(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor;
    Actor* hit = NULL;
    s32 nearby = 0;
    s32 want = LVL3(ID_CT_ENEMIES, 3, 4, 6);

    Drops_Update(play);
    if (player == NULL) {
        return;
    }

    // Which enemy just got hurt? (Health went down since last frame.)
    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL)) {
            u8 hp = actor->colChkInfo.health;

            if ((hp > 0) && (actor->xzDistToPlayer < 1000.0f)) {
                nearby++;
            }
            if ((t->flags & TAGF_HP_SEEN) && (hp < t->hp) && (hit == NULL) && (actor->xzDistToPlayer < 400.0f) &&
                (actor->id != ACTOR_EN_RR) && (actor->id != ACTOR_EN_RD) && (actor->id != ACTOR_EN_WALLMAS) &&
                (actor->id != ACTOR_EN_FLOORMAS)) { // never grabbers: they'd hold you in the blast
                hit = actor;
            }
            t->hp = hp;
            t->flags |= TAGF_HP_SEEN;
        }
        actor = actor->next;
    }
    if (sCtWarmup > 0) {
        sCtWarmup--;
        hit = NULL;
    }

    if (sCtOnLink) {
        if (hit != NULL) {
            // Passed on!
            sCtOnLink = false;
            sCtHolder = hit;
            sCtHolderPos = hit->world.pos;
            sCtHolderTimer = CT_HOLD_FRAMES;
            Audio_PlaySfx(NA_SE_EN_KOUME_MAGIC);
        } else {
            sCtFuse--;
            if ((sCtFuse <= 5 * FPS) && ((sCtFuse % FPS) == 0) && (sCtFuse > 0)) {
                Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
            }
            if (sCtFuse <= 0) {
                // A bomb falls onto the spot where Link stands, marked on the
                // ground first: keep moving and it misses.
                Vec3f target = player->actor.world.pos;
                Vec3f from = target;

                from.y = Ev_SkyHeight(play, &target, 300.0f);
                Drop_Launch(play, &from, &target, 26);
                Audio_PlaySfx(NA_SE_EN_PO_LAUGH);
                sCtFuse = CurseTag_FuseFrames();
            }
        }
    } else {
        if ((sCtHolder != NULL) && Ev_ActorInList(play, sCtHolder, ACTORCAT_ENEMY)) {
            sCtHolderPos = sCtHolder->world.pos;
        } else {
            sCtHolder = NULL; // it's gone: the curse waits where it fell
        }
        if (--sCtHolderTimer <= 0) {
            Vec3f pos = sCtHolderPos;

            pos.y += 10.0f;
            Ev_SpawnLinkBomb(play, &pos, 1);
            CurseTag_GiveLink();
            Audio_PlaySfx(NA_SE_EN_PO_LAUGH);
        } else if ((sCtHolderTimer % FPS) == 0) {
            SoundSource_PlaySfxAtFixedWorldPos(play, &sCtHolderPos, 10, NA_SE_SY_WARNING_COUNT_N);
        }
    }

    // Keep a few enemies around to pass it to.
    if ((sEventFrames % (2 * FPS)) == 0) {
        if (nearby < want) {
            sSpawnsLeft = want - nearby;
            sSpawnFailFrames = 0;
        } else {
            sSpawnsLeft = 0;
        }
    }
    if ((sSpawnsLeft > 0) && (sSpawnFailFrames < 60) && ((sEventFrames % 5) == 0)) {
        const SpawnDef* def =
            &sPool[sHungerFodder[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sHungerFodder)) % ARRAY_COUNT(sHungerFodder)]];

        Ev_SpawnDef(play, def, def->params, 220.0f, 550.0f);
    }
}

// ---------------------------------------------------------------------------
// Pop Quiz: a song plays; name it from four choices with the D-pad or C
// buttons. A wrong answer costs half your heart containers' worth of health.
// Two questions, then it's over. Everything holds still while you think.
// ---------------------------------------------------------------------------

typedef struct {
    u16 seqId;
    const char* name;
} QuizSong;

static const QuizSong sQuizSongs[] = {
    { NA_BGM_TERMINA_FIELD, "Termina Field" },
    { NA_BGM_CLOCK_TOWN_DAY_1, "Clock Town Day 1" },
    { NA_BGM_CLOCK_TOWN_DAY_2, "Clock Town Day 2" },
    { NA_BGM_CLOCK_TOWN_DAY_3, "Clock Town Day 3" },
    { NA_BGM_FINAL_HOURS, "Final Hours" },
    { NA_BGM_WOODFALL_TEMPLE, "Woodfall Temple" },
    { NA_BGM_SNOWHEAD_TEMPLE, "Snowhead Temple" },
    { NA_BGM_GREAT_BAY_TEMPLE, "Great Bay Temple" },
    { NA_BGM_STONE_TOWER_TEMPLE, "Stone Tower Temple" },
    { NA_BGM_SWAMP_REGION, "Southern Swamp" },
    { NA_BGM_MOUNTAIN_REGION, "Mountain Village" },
    { NA_BGM_GREAT_BAY_REGION, "Great Bay Coast" },
    { NA_BGM_IKANA_REGION, "Ikana Canyon" },
    { NA_BGM_IKANA_CASTLE, "Ikana Castle" },
    { NA_BGM_DEKU_PALACE, "Deku Palace" },
    { NA_BGM_PIRATES_FORTRESS, "Pirates' Fortress" },
    { NA_BGM_GORON_VILLAGE, "Goron Village" },
    { NA_BGM_ZORA_HALL, "Zora Hall" },
    { NA_BGM_ROMANI_RANCH, "Romani Ranch" },
    { NA_BGM_MUSIC_BOX_HOUSE, "Music Box House" },
    { NA_BGM_ASTRAL_OBSERVATORY, "Astral Observatory" },
    { NA_BGM_MILK_BAR, "Milk Bar" },
    { NA_BGM_SHOP, "Shop" },
    { NA_BGM_FAIRY_FOUNTAIN, "Fairy's Fountain" },
    { NA_BGM_MINI_BOSS, "Mini-Boss Battle" },
    { NA_BGM_BOSS, "Boss Battle" },
    { NA_BGM_CAVERN, "Cavern" },
    { NA_BGM_KAMARO_DANCE, "Kamaro's Dance" },
    { NA_BGM_HAPPY_MASK_SALESMAN, "Mask Salesman" },
    { NA_BGM_MAYORS_OFFICE, "Mayor's Office" },
    { NA_BGM_SWORD_TRAINING_HALL, "Swordsman's School" },
    { NA_BGM_KOTAKE_POTION_SHOP, "Potion Shop" },
    { NA_BGM_SHOOTING_GALLERY, "Shooting Gallery" },
    { NA_BGM_GORON_RACE, "Goron Race" },
    { NA_BGM_INSIDE_A_HOUSE, "Inside a House" },
};

#define QUIZ_SONGS ((s32)ARRAY_COUNT(sQuizSongs))
#define QUIZ_QUESTIONS 4
#define QUIZ_LISTEN (2 * FPS)    // the song plays this long before you can answer
#define QUIZ_RESULT (2 * FPS)    // the answer stays up this long
#define QUIZ_READ (15)           // text questions: a moment to read before answers count
#define QUIZ_ANSWER (12 * FPS)   // then this long to answer, or it counts as wrong

typedef enum { QUIZ_WAIT, QUIZ_ASK, QUIZ_RESULT_SHOWN } QuizPhase;

static s32 sQuizPhase = QUIZ_WAIT;
static s32 sQuizNum = 0;
static s32 sQuizTimer = 0;
static s32 sQuizOpts[4];
static s32 sQuizCorrect = 0;
static s32 sQuizPicked = -1;
static s32 sQuizHolding = false;
static s32 sQuizLastSong = -1;
// V3: every question is text now: songs, trivia, and questions about where you are.
static char sQuizQ[40];
static char sQuizA[4][30];
static s32 sQuizIsSong = false;
static s32 V3_QuizQuestion(PlayState* play, char* q, char opts[4][30]); // fills opts[0] = right answer

// Friend Link: a friend's own questions ("question;right;wrong;wrong;wrong"), used by
// the next Pop Quiz in order. Gone once that quiz is over.
static char sFQuiz[4][104];
static s32 sFQuizCount = 0;
static char sFQuizFrom[6];

static s32 Quiz_FriendQuestion(s32 num, char* q, char opts[4][30]) {
    const char* p;
    s32 field = 0;
    s32 n = 0;

    if ((num < 0) || (num >= sFQuizCount) || (sFQuiz[num][0] == '\0')) {
        return false;
    }
    q[0] = '\0';
    for (field = 0; field < 4; field++) {
        opts[field][0] = '\0';
    }
    field = 0;
    for (p = sFQuiz[num]; *p != '\0'; p++) {
        if (*p == ';') {
            field++;
            n = 0;
            if (field > 4) {
                break;
            }
            continue;
        }
        if ((field == 0) && (n < 39)) {
            q[n++] = *p;
            q[n] = '\0';
        } else if ((field > 0) && (n < 29)) {
            opts[field - 1][n++] = *p;
            opts[field - 1][n] = '\0';
        }
    }
    return (field >= 4) && (q[0] != '\0') && (opts[0][0] != '\0') && (opts[1][0] != '\0') &&
           (opts[2][0] != '\0') && (opts[3][0] != '\0');
}

// Waiting on your answer (the event clock holds still meanwhile).
static s32 Quiz_Asking(void) {
    return EV_RUNNING(EV_QUIZ) && (sQuizPhase == QUIZ_ASK);
}

static void Quiz_Reset(void) {
    sQuizPhase = QUIZ_WAIT;
    sQuizNum = 0;
    sQuizTimer = FPS;
    sQuizPicked = -1;
    sQuizHolding = false;
}

static void Quiz_Release(PlayState* play) {
    if (sQuizHolding) {
        sQuizHolding = false;
        if (GET_PLAYER(play) != NULL) {
            Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
        }
    }
    sQuizPhase = QUIZ_WAIT;
}

static void Quiz_PlaySong(u16 seqId) {
    if (!sMusicPlaying) {
        Ev_StartMusic(seqId);
    } else {
        // Already playing a quiz song: swap it, keeping the area's song for later.
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, seqId);
        sEventSeqId = seqId;
        sMusicChanged = true;
        sMusicStopDelay = 0;
    }
}

static void Quiz_NewQuestion(PlayState* play) {
    s32 i;
    s32 j;
    char opts[4][30];

    if (Quiz_FriendQuestion(sQuizNum, sQuizQ, opts)) {
        sQuizIsSong = false; // a friend wrote this one
    } else {
        sQuizIsSong = (Rand_ZeroOne() < 0.35f) || !V3_QuizQuestion(play, sQuizQ, opts);
    }
    if (sQuizIsSong) {
        s32 ids[4];

        // The song, never the same one twice in a row, and three different wrong answers.
        do {
            ids[0] = (s32)(Rand_ZeroOne() * QUIZ_SONGS) % QUIZ_SONGS;
        } while (ids[0] == sQuizLastSong);
        sQuizLastSong = ids[0];
        for (i = 1; i < 4; i++) {
            s32 ok;

            do {
                ids[i] = (s32)(Rand_ZeroOne() * QUIZ_SONGS) % QUIZ_SONGS;
                ok = true;
                for (j = 0; j < i; j++) {
                    if (ids[j] == ids[i]) {
                        ok = false;
                    }
                }
            } while (!ok);
        }
        for (i = 0; i < 4; i++) {
            Ev_Append(opts[i], 0, sQuizSongs[ids[i]].name, sizeof(opts[i]));
        }
        Ev_Append(sQuizQ, 0, "Name this song!", sizeof(sQuizQ));
        Quiz_PlaySong(sQuizSongs[ids[0]].seqId);
    }
    // Shuffle, keeping track of the right one (it starts in slot 0).
    for (i = 0; i < 4; i++) {
        sQuizOpts[i] = i;
    }
    for (i = 3; i > 0; i--) {
        s32 k = (s32)(Rand_ZeroOne() * (i + 1)) % (i + 1);
        s32 tmp = sQuizOpts[i];

        sQuizOpts[i] = sQuizOpts[k];
        sQuizOpts[k] = tmp;
    }
    for (i = 0; i < 4; i++) {
        Ev_Append(sQuizA[i], 0, opts[sQuizOpts[i]], sizeof(sQuizA[i]));
        if (sQuizOpts[i] == 0) {
            sQuizCorrect = i;
        }
    }
    sQuizPicked = -1;
    sQuizTimer = sQuizIsSong ? 0 : (QUIZ_LISTEN - QUIZ_READ); // songs play first; text gets a moment to read
    sQuizPhase = QUIZ_ASK;
}

// Link has to be standing on the ground to be held still.
static s32 Quiz_CanHold(Player* player) {
    return (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
           !(player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_200000 | PLAYER_STATE1_8000000)) &&
           (player->csAction == PLAYER_CSACTION_NONE) && (player->actor.parent == NULL);
}

static void Quiz_Answer(PlayState* play, s32 pick) {
    Player* player = GET_PLAYER(play);

    sQuizPicked = pick;
    if (pick == sQuizCorrect) {
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    } else {
        // Half your heart containers, taken straight off (the randomizer's damage
        // settings don't apply to this).
        s32 loss = gSaveContext.save.saveInfo.playerData.healthCapacity / 2;

        sQuizMistakes++;
        s32 hp = gSaveContext.save.saveInfo.playerData.health;

        if (hp - loss <= 0) {
            Quiz_Release(play); // let the game play out Link falling (or a fairy saving him)
        }
        gSaveContext.save.saveInfo.playerData.health = MAX(hp - loss, 0);
        Audio_PlaySfx(NA_SE_SY_ERROR);
        if (player != NULL) {
            Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 200, COLORFILTER_BUFFLAG_OPA, 12);
        }
    }
    sQuizPhase = QUIZ_RESULT_SHOWN;
    sQuizTimer = 0;
}

static void Tick_Quiz(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];

    if (player == NULL) {
        return;
    }
    if (sBannerTimer > 0) {
        return; // let the banner finish first
    }

    switch (sQuizPhase) {
        case QUIZ_WAIT:
            if ((--sQuizTimer <= 0) && Quiz_CanHold(player) &&
                Player_SetCsActionWithHaltedActors(play, NULL, PLAYER_CSACTION_WAIT)) {
                sQuizHolding = true;
                Quiz_NewQuestion(play);
            }
            break;

        case QUIZ_ASK: {
            u16 press = input->press.button;
            s32 pick = -1;

            sQuizTimer++;
            if (sQuizTimer > QUIZ_LISTEN) {
                if (press & (BTN_DUP | BTN_CUP)) {
                    pick = 0;
                } else if (press & (BTN_DLEFT | BTN_CLEFT)) {
                    pick = 1;
                } else if (press & (BTN_DRIGHT | BTN_CRIGHT)) {
                    pick = 2;
                } else if (press & (BTN_DDOWN | BTN_CDOWN)) {
                    pick = 3;
                }
            }
            if ((pick < 0) && (sQuizTimer > QUIZ_LISTEN + QUIZ_ANSWER)) {
                pick = (sQuizCorrect + 1) & 3; // too slow: counts as wrong
                Menu_ShowToast("Too slow!");
            }
            if (pick >= 0) {
                Quiz_Answer(play, pick);
            }
            break;
        }

        case QUIZ_RESULT_SHOWN:
            if (++sQuizTimer >= QUIZ_RESULT) {
                sQuizNum++;
                if (sQuizNum >= QUIZ_QUESTIONS) {
                    Quiz_Release(play);
                    Menu_ShowToast("Pop Quiz over!");
                    sFQuizCount = 0; // a friend's questions are used once
                    Ev_End(play);
                    return;
                }
                if (sQuizHolding) {
                    Quiz_NewQuestion(play);
                } else {
                    sQuizPhase = QUIZ_WAIT; // hold Link again once he's back on his feet
                    sQuizTimer = FPS;
                }
            }
            break;
    }

    // While a question is up, the buttons answer the quiz and nothing else.
    if (sQuizHolding) {
        input->cur.button = 0;
        input->press.button = 0;
        input->rel.button = 0;
        input->cur.stick_x = input->cur.stick_y = 0;
        input->press.stick_x = input->press.stick_y = 0;
        input->rel.stick_x = input->rel.stick_y = 0;
    }
}

static Gfx* Quiz_Draw(PlayState* play, Gfx* gfx) {
    static const char* sArrows[4] = { "^ ", "< ", "> ", "v " };
    GfxPrint printer;
    char line[40];
    s32 i;
    s32 n;

    if (sQuizPhase == QUIZ_WAIT) {
        // Waiting for Link to be on solid ground.
        const char* wait = "POP QUIZ: find solid ground";

        gfx = Ui_DrawRect(gfx, (SCREEN_WIDTH - 27 * 8) / 2 - 6, 56, (SCREEN_WIDTH + 27 * 8) / 2 + 6, 70, 10, 10,
                          30, 170);
        bzero(&printer, sizeof(printer));
        GfxPrint_Init(&printer);
        GfxPrint_Open(&printer, gfx);
        gDPSetTextureFilter(printer.dList++, G_TF_POINT);
        printer.flags |= (1 << 2);
        GfxPrint_SetColor(&printer, 255, 210, 60, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - 27 * 8) / 2, 59, wait);
        gfx = GfxPrint_Close(&printer);
        GfxPrint_Destroy(&printer);
        return gfx;
    }
    gfx = Ui_DrawRect(gfx, 12, 56, SCREEN_WIDTH - 12, 150, 10, 10, 30, 200);
    gfx = Ui_DrawRect(gfx, 12, 56, SCREEN_WIDTH - 12, 58, 255, 210, 60, 230);
    gfx = Ui_DrawRect(gfx, 12, 148, SCREEN_WIDTH - 12, 150, 255, 210, 60, 230);

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2);

    n = Ev_Append(line, 0, "POP QUIZ  ", sizeof(line));
    n = Ev_AppendNum(line, n, sQuizNum + 1, sizeof(line));
    n = Ev_Append(line, n, "/", sizeof(line));
    Ev_AppendNum(line, n, QUIZ_QUESTIONS, sizeof(line));
    GfxPrint_SetColor(&printer, 255, 210, 60, 255);
    Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(line) * 8) / 2, 62, line);
    GfxPrint_SetColor(&printer, 230, 230, 230, 255);
    Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sQuizQ) * 8) / 2, 74, sQuizQ);

    for (i = 0; i < 4; i++) {
        u8 r = 255;
        u8 g = 255;
        u8 b = 255;

        n = Ev_Append(line, 0, sArrows[i], sizeof(line));
        Ev_Append(line, n, sQuizA[i], sizeof(line));
        if (sQuizPhase == QUIZ_RESULT_SHOWN) {
            if (i == sQuizCorrect) {
                r = 80;
                b = 110; // the right answer
            } else if (i == sQuizPicked) {
                g = 70;
                b = 70; // your wrong pick
            } else {
                r = g = b = 120;
            }
        } else if (sQuizTimer <= QUIZ_LISTEN) {
            r = g = b = 150; // listen first
        }
        GfxPrint_SetColor(&printer, r, g, b, 255);
        Ui_Print(&printer, 24, 90 + i * 12, line);
    }

    if (sQuizPhase == QUIZ_RESULT_SHOWN) {
        const char* verdict = (sQuizPicked == sQuizCorrect) ? "Correct!" : "Wrong! Half your hearts!";

        GfxPrint_SetColor(&printer, (sQuizPicked == sQuizCorrect) ? 90 : 255,
                          (sQuizPicked == sQuizCorrect) ? 255 : 80, 90, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(verdict) * 8) / 2, 139, verdict);
    } else {
        char hint[28];
        s32 left = (QUIZ_LISTEN + QUIZ_ANSWER - sQuizTimer + FPS - 1) / FPS;
        s32 hn;

        if (sQuizTimer <= QUIZ_LISTEN) {
            Ev_Append(hint, 0, sQuizIsSong ? "Listen..." : "Read...", sizeof(hint));
        } else {
            hn = Ev_Append(hint, 0, "D-Pad or C: answer (", sizeof(hint));
            hn = Ev_AppendNum(hint, hn, MAX(left, 0), sizeof(hint));
            Ev_Append(hint, hn, "s)", sizeof(hint));
        }
        GfxPrint_SetColor(&printer, (left <= 3) && (sQuizTimer > QUIZ_LISTEN) ? 255 : 170, 170, 170, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(hint) * 8) / 2, 139, hint);
    }
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    return gfx;
}

// ---------------------------------------------------------------------------
// Hooks into the shared event flow (called from the wave 2 and 4 hooks)
// ---------------------------------------------------------------------------

static void Wave5_OnStart(PlayState* play, s32 ev) {
    switch (ev) {
        case EV_TINGLE:
            sTgGone = false;
            sTgReady = false;
            sTgBanner = 0;
            Drops_Clear();
            break;
        case EV_STAMPEDE:
            Lanes_Clear();
            sLaneRest = 2 * FPS;
            break;
        case EV_CURSETAG:
            CurseTag_GiveLink();
            sCtWarmup = 3;
            break;
        case EV_QUIZ:
            Quiz_Reset();
            break;
        case EV_PREY:
            Prey_OnStart(play);
            break;
        default:
            break;
    }
}

static void Wave5_SceneSetup(PlayState* play) {
    switch (sActive) {
        case EV_TINGLE:
            Drops_Clear();
            if (!Ev_IsOutdoors(play)) {
                sTgReady = false; // he can't follow you inside
                if (!sInComboPass) {
                    Menu_ShowToast("Tingle can't follow you inside.");
                    Ev_Abort(play);
                }
            } else if (sTgGone && !sInComboPass) {
                Menu_ShowToast("Tingle is down!");
                Ev_End(play);
            } else if (!sTgGone) {
                Tingle_Setup(play);
                if (!sTgReady && !sInComboPass) {
                    Menu_ShowToast("Tingle couldn't make it.");
                    Ev_Abort(play);
                }
            }
            break;
        case EV_STAMPEDE:
            Lanes_Clear();
            sLaneRest = 2 * FPS;
            break;
        case EV_CURSETAG:
            sCtWarmup = 3;
            if (!sCtOnLink) {
                CurseTag_GiveLink(); // whoever had it stayed behind
            }
            break;
        case EV_PREY:
            Prey_SceneSetup(play);
            break;
        default:
            break;
    }
}

static void Wave5_Tick(PlayState* play) {
    switch (sActive) {
        case EV_TINGLE:
            Tick_Tingle(play);
            break;
        case EV_STAMPEDE:
            Tick_Stampede(play);
            break;
        case EV_CURSETAG:
            Tick_CurseTag(play);
            break;
        case EV_QUIZ:
            Tick_Quiz(play);
            break;
        case EV_PREY:
            Tick_Prey(play);
            break;
        default:
            break;
    }
}

static void Wave5_OnEnd(PlayState* play) {
    Prey_OnEnd(play);
    Quiz_Release(play);
    sTgReady = false;
    sTgFalling = false;
    sTgGone = false;
    Drops_Clear();
    Lanes_Clear();
    sCtOnLink = false;
    sCtHolder = NULL;
}

static void Wave5_ResetForPlayInit(void) {
    sQuizHolding = false; // new area, new Link: nothing to let go of
    Prey_ResetForPlayInit();
    if (sQuizPhase != QUIZ_WAIT) {
        sQuizPhase = QUIZ_WAIT;
        sQuizTimer = FPS;
    }
    sTgReady = false;
    Drops_Clear();
    Lanes_Clear();
    sCtHolder = NULL;
}

static void Wave5_OnRoomChange(PlayState* play) {
    Prey_OnRoomChange(play);
    if (sTgFalling) {
        sTgGone = true; // popped just before leaving: he stays down
        sTgFalling = false;
    }
    Drops_Clear();
    Lanes_Clear();
    sCtHolder = NULL;
}

static void Wave5_DrawWorld(PlayState* play) {
    if (sActive < 0) {
        return;
    }
    if (Ev_IsOn(EV_TINGLE)) {
        Tingle_Draw(play);
    }
    if (Ev_IsOn(EV_TINGLE) || Ev_IsOn(EV_CURSETAG)) {
        Drops_Draw(play);
    }
    if (Ev_IsOn(EV_STAMPEDE)) {
        Stampede_Draw(play);
    }
    Prey_DrawWorld(play);
}

static s32 Wave5_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b, f32* meter) {
    if (Ev_IsOn(EV_CURSETAG)) {
        s32 n;

        if (sCtOnLink) {
            n = Ev_Append(line, 0, "CURSED! Hit an enemy ", size);
            Ev_AppendNum(line, n, (sCtFuse + FPS - 1) / FPS, size);
            *r = 210;
            *g = 110;
            *b = 255;
            *meter = CLAMP((f32)sCtFuse / MAX(CurseTag_FuseFrames(), 1), 0.0f, 1.0f);
        } else {
            n = Ev_Append(line, 0, "Passed! It bursts in ", size);
            Ev_AppendNum(line, n, (sCtHolderTimer + FPS - 1) / FPS, size);
            *r = 255;
            *g = 170;
            *b = 60;
            *meter = CLAMP((f32)sCtHolderTimer / CT_HOLD_FRAMES, 0.0f, 1.0f);
        }
        return true;
    }
    if (Ev_IsOn(EV_TINGLE)) {
        if (sTgFalling || (sTgGone && (sTgBanner > 0))) {
            Ev_Append(line, 0, "Tingle is down!", size);
            *r = 120;
            *g = 255;
            *b = 120;
            return true;
        }
        if (EV_RUNNING(EV_TINGLE)) {
            Ev_Append(line, 0, "Pop his balloon!", size);
            *r = 255;
            *g = 230;
            *b = 90;
            return true;
        }
    }
    if (Ev_IsOn(EV_STAMPEDE) && Stampede_Warning()) {
        Ev_Append(line, 0, "Clear the lane!", size);
        *r = 255;
        *g = (play->gameplayFrames & 4) ? 200 : 130;
        *b = 40;
        return true;
    }
    // Last, so a Cursed Tag fuse or Tingle's line on top of it still shows.
    return Prey_SubLine(play, line, size, r, g, b);
}

// ---------------------------------------------------------------------------
// 1.6: active hours, next-event clock, curse effects
// ---------------------------------------------------------------------------

// Safe zones were removed in 2.5.1: nothing is ever held back for being in a
// shop, minigame or boss room. Kept as a stub so older call sites stay simple.
static s32 Ev_InSafeZone(PlayState* play) {
    return false;
}

static s32 Ev_PauseReason(PlayState* play) {
    s32 hours = gOpt[ID_EV_HOURS] % LVL_MAX;

    if ((hours == LVL_MID) && Ev_IsNight()) {
        return 2; // Day Only: waiting for morning
    }
    if ((hours == LVL_HIGH) && !Ev_IsNight()) {
        return 3; // Night Only: waiting for dusk
    }
    return 0;
}

// Foresight shows the next event's name (never its time) until it starts.
static s32 NextClock_Wanted(void) {
    return sForesight && (sFriendNext >= 0) && gOpt[ID_EV_MASTER] && (sActive < 0) && (sRouletteTimer == 0) &&
           (sCurseCardTimer == 0) && !Curse_Is(CURSE_SILENT) && !Curse_Is(CURSE_BELL) &&
           ((gOpt[ID_EV_TIMERSTYLE] % LVL_MAX) != LVL_HIGH) && !Ev_ShowWarning();
}

// Hex: Cursed Tag rides along with every event.
static s32 Curse_ForcedCombo(PlayState* play, s32 primary) {
    s32 second = Curse_Is(CURSE_HEX) ? EV_CURSETAG : -1;

    if ((second < 0) || (second == primary) || !Combo_CanBePrimary(primary) || !gOpt[ID_EV_ON_FIRST + second] ||
        !Ev_CanRun(play, second)) {
        return -1;
    }
    return second;
}

// Meteor Shower, on top of whatever event is running.
static void Curse_EventTick(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if ((sActive < 0) || (player == NULL) || EV_RUNNING(EV_QUIZ)) {
        return;
    }
    if (Curse_Is(CURSE_METEOR) && !EV_RUNNING(EV_REDLIGHT) && !EV_RUNNING(EV_GLARE) && !EV_RUNNING(EV_STILL) &&
        !EV_RUNNING(EV_FATE) && ((sEventFrames % (5 * FPS)) == (3 * FPS))) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f target = player->actor.world.pos;
        f32 floorY;

        target.y += 50.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &target);
        if (floorY > BGCHECK_Y_MIN) {
            target.y = floorY;
            SkyRock_Launch(play, &target, 36, 0.3f); // lands where you stood: keep moving
        }
    }
}

// Glass Moon: whatever hurts you during an event hurts twice as much.
static u16 sHpBeforeChange = 0;

RECOMP_HOOK("Health_ChangeBy") void Events_BeforeHealthChange(PlayState* play, s16 amount) {
    sHpBeforeChange = gSaveContext.save.saveInfo.playerData.health;
    DD_BeforeHealthChange();
}

void Events_AfterHealthChange(void) {
    DD_AfterHealthChange();
    Health_React(sHpBeforeChange);
    sHealthHookSeen = true;
}

// Low gravity: the Low Gravity event, or any event under Weightless Moon.
f32 Events_LowGravity(void) {
    static const f32 sFactor[INTENSITY_MAX] = { 0.5f, 0.62f, 0.75f };

    if ((sActive < 0) || (sPauseReason == 1) || !sSceneReady || EV_RUNNING(EV_QUIZ)) {
        return 0.0f;
    }
    if (Ev_IsOn(EV_LOWGRAV) || Curse_Is(CURSE_FLOAT)) {
        return sFactor[Intensity()];
    }
    return 0.0f;
}

// Ambush: stepping into a new area can start a short Blood Moon.
static s32 sAmbushTimer = 0;

static void Ambush_OnNewArea(void) {
    sAmbushTimer = Curse_Is(CURSE_AMBUSH) ? 3 * FPS : 0;
}

static s32 Ambush_Update(PlayState* play) {
    if ((sAmbushTimer <= 0) || (--sAmbushTimer > 0)) {
        return false;
    }
    if (!Curse_Is(CURSE_AMBUSH) || (Rand_ZeroOne() >= 0.3f) || !gOpt[ID_EV_ON_FIRST + EV_BLOOD_MOON] ||
        !Ev_CanRun(play, EV_BLOOD_MOON)) {
        return false;
    }
    Ev_StartFull(play, EV_BLOOD_MOON, -1, false);
    sIsAftershock = true; // short and extra: no aftershock or encore of its own
    sTotalFrames = sFramesLeft = 20 * FPS;
    Ev_Append(sCardSub, 0, "Ambush!", sizeof(sCardSub));
    return true;
}

// ---------------------------------------------------------------------------
// Menu info rows
// ---------------------------------------------------------------------------

const char* Events_InfoLabel(s32 id) {
    static char sLabel[28];

    switch (id) {
        case ID_EV_CURSE_INFO:
            return "Curse";
        case ID_RR_SEEN:
            return "Events Seen";
        case ID_RR_SURVIVED:
            return "Survived";
        case ID_RR_DEATHS:
            return "Deaths To Events";
        case ID_RR_COMBOS:
            return "Combos Seen";
        case ID_RR_KILLER:
            return "Top Killer";
        case ID_RR_STREAK:
            return "Current Streak";
        case ID_RR_BEST:
            return "Best Streak";
        case ID_RR_WRATH:
            return "Wrath Meter";
        case ID_RR_KILLER_NAME: {
            s32 k = Stats_TopKiller();
            s32 n = Ev_Append(sLabel, 0, "  ", sizeof(sLabel));

            Ev_Append(sLabel, n, (k >= 0) ? Events_ShortName(k) : "none yet", sizeof(sLabel));
            return sLabel;
        }
        default:
            break;
    }
    if ((id >= ID_RR_EV_FIRST) && (id <= ID_RR_EV_LAST)) {
        return Events_ShortName(id - ID_RR_EV_FIRST);
    }
    if ((id >= ID_LOG_FIRST) && (id <= ID_LOG_LAST)) {
        s32 i = id - ID_LOG_FIRST;
        s32 n;

        if (i >= sLogCount) {
            return "-";
        }
        n = Ev_AppendNum(sLabel, 0, i + 1, sizeof(sLabel));
        n = Ev_Append(sLabel, n, ". ", sizeof(sLabel));
        n = Ev_Append(sLabel, n, Events_ShortName(sLog[i].ev), sizeof(sLabel));
        if (sLog[i].combo >= 0) {
            Ev_Append(sLabel, n, "+", sizeof(sLabel));
        }
        sLabel[18] = '\0'; // leave room for the result
        return sLabel;
    }
    return NULL;
}

const char* Events_InfoText(s32 id, char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    s32 n;

    out[0] = '\0';
    *r = *g = *b = 255;
    switch (id) {
        case ID_EV_STATUS:
            if ((sActive < 0) && (sRouletteTimer > 0)) {
                Ev_Append(out, 0, "Spinning...", maxLen);
                *g = 220;
                *b = 90;
                return out;
            }
            Events_StatusText(out, maxLen);
            if (out[0] == 'O') {
                *r = *g = *b = 150; // "Off"
            } else if (out[0] == 'N') {
                *g = 220; // "Next in"
                *b = 90;
            } else {
                *g = 80; // an event is running
                *b = 80;
            }
            return out;
        case ID_EV_CURSE_INFO:
            if (!gOpt[ID_EV_CURSE]) {
                Ev_Append(out, 0, "Off", maxLen);
                *r = *g = *b = 150;
            } else if (sCurse < 0) {
#ifdef GE_CHEATS
                Ev_Append(out, 0, "< None >", maxLen);
#else
                Ev_Append(out, 0, "None picked", maxLen);
#endif
                *r = *g = *b = 150;
            } else {
                s32 more = -1;
                s32 c;

                for (c = 0; c < CURSE_COUNT; c++) {
                    more += (sCurseMask >> c) & 1;
                }
#ifdef GE_CHEATS
                n = Ev_Append(out, 0, "< ", maxLen);
                n = Ev_Append(out, n, Curse_Name(sCurse), maxLen);
                n = Ev_Append(out, n, " >", maxLen);
#else
                n = Ev_Append(out, 0, Curse_Name(sCurse), maxLen);
#endif
                if (more > 0) {
                    n = Ev_Append(out, n, " +", maxLen);
                    Ev_AppendNum(out, n, more, maxLen);
                }
                *r = 210;
                *g = 140;
                *b = 255;
            }
            return out;
        case ID_RR_SEEN:
            Ev_Itoa(out, Stats_Sum(sStatSeen));
            return out;
        case ID_RR_SURVIVED:
            Ev_Itoa(out, Stats_Sum(sStatWon));
            *r = 90;
            *b = 120;
            return out;
        case ID_RR_DEATHS:
            Ev_Itoa(out, Stats_Sum(sStatDied));
            *g = 90;
            *b = 90;
            return out;
        case ID_RR_STREAK:
            Ev_Itoa(out, sStreak);
            *r = 90;
            *b = 120;
            return out;
        case ID_RR_BEST:
            Ev_Itoa(out, sBestStreak);
            *b = 90;
            return out;
        case ID_RR_WRATH:
            if (!Curse_Is(CURSE_WRATH)) {
                Ev_Append(out, 0, "No curse", maxLen);
                *r = *g = *b = 150;
            } else if (sWrathPending) {
                Ev_Append(out, 0, "FURIOUS", maxLen);
                *g = 60;
                *b = 60;
            } else {
                n = Ev_AppendNum(out, 0, sWrath, maxLen);
                n = Ev_Append(out, n, "/", maxLen);
                Ev_AppendNum(out, n, WRATH_MAX, maxLen);
                *g = 150;
                *b = 90;
            }
            return out;
        case ID_RR_COMBOS:
            Ev_Itoa(out, sStatCombos);
            *g = 180;
            *b = 80;
            return out;
        case ID_RR_KILLER:
            return out;
        case ID_RR_KILLER_NAME: {
            s32 k = Stats_TopKiller();

            if (k >= 0) {
                n = Ev_AppendNum(out, 0, sStatDied[k], maxLen);
                Ev_Append(out, n, " KO", maxLen);
                *g = 90;
                *b = 90;
            }
            return out;
        }
        default:
            break;
    }
    if ((id >= ID_LOG_FIRST) && (id <= ID_LOG_LAST)) {
        s32 i = id - ID_LOG_FIRST;

        if (i < sLogCount) {
            static const char* sResults[3] = { "OK", "KO", "Stopped" };

            Ev_Append(out, 0, sResults[sLog[i].result % 3], maxLen);
            if (sLog[i].result == LOG_OK) {
                *r = 90;
                *b = 120;
            } else if (sLog[i].result == LOG_KO) {
                *g = 80;
                *b = 80;
            } else {
                *r = *g = *b = 150;
            }
        }
        return out;
    }
    if ((id >= ID_RR_EV_FIRST) && (id <= ID_RR_EV_LAST)) {
        s32 ev = id - ID_RR_EV_FIRST;

        if (sStatSeen[ev] == 0) {
            Ev_Append(out, 0, "-", maxLen);
            *r = *g = *b = 130;
            return out;
        }
        n = Ev_AppendNum(out, 0, sStatWon[ev], maxLen);
        n = Ev_Append(out, n, " ok ", maxLen);
        n = Ev_AppendNum(out, n, sStatDied[ev], maxLen);
        Ev_Append(out, n, " KO", maxLen);
        if (sStatDied[ev] > 0) {
            *g = 130;
            *b = 110;
        } else {
            *r = 130;
            *b = 150;
        }
        return out;
    }
    return out;
}
