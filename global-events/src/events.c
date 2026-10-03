#include "common.h"
#include "z64recomp_api.h"
#include "zelda_arena.h"
#include "z64quake.h"
#include "recompui.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/actors/ovl_En_Fall/z_en_fall.h"
#include "overlays/actors/ovl_En_Rr/z_en_rr.h"
#include "overlays/actors/ovl_En_Osn/z_en_osn.h"

_Static_assert(__builtin_offsetof(EnRr, unk_1E2) == 0x1E2, "EnRr layout");
_Static_assert(__builtin_offsetof(EnBom, timer) == 0x1F0, "EnBom layout");

// A few fields of actors whose full headers need model files this build
// doesn't have. Offsets are from the decomp's struct definitions.
#define ACTOR_FIELD(actor, type, offset) (*(type*)((u8*)(actor) + (offset)))
#define ENDG_PATH(actor) ACTOR_FIELD(actor, Path*, 0x1DC)                 // EnDg.path
#define ENDG_PARAMS(path, index) (((path) << 10) | ((index) << 5))
#define ENIK_CYL_ACFLAGS(actor) ACTOR_FIELD(actor, u8, 0x3AC + __builtin_offsetof(Collider, acFlags)) // EnIk.colliderCylinder
#define ENRD_ACTION(actor) ACTOR_FIELD(actor, u8, 0x3EF)                  // EnRd.action
#define ENARROW_STUCK_ACTOR(actor) ACTOR_FIELD(actor, Actor*, 0x264)     // EnArrow.unk_264
#define BOSS01_PHASE_FRAMES(actor) ACTOR_FIELD(actor, s32, 0x148)         // Boss01.phaseFrameCounter
#define BOSS01_SUBCAM(actor) ACTOR_FIELD(actor, s16, 0x9E2)               // Boss01.subCamId

// ===========================================================================
// Global Events
//
// Every few minutes of play, one random enabled event runs for a while:
// Bomb Rain, Rupoor Rain, Blood Moon, Joke Event, Moonfall, The Stalker and
// Poe Hunt. Everything here is done with hooks and actor spawning, so it works
// alongside other mods (including the randomizer). Nothing here gives items
// or sets progress flags.
// ===========================================================================

#define FPS 20 // game logic runs at 20 updates per second

// Stored-music variables from the game's audio code.
extern u16 sPrevMainBgmSeqId;
extern u16 sPrevAmbienceSeqId;

void EnFall_Fireball_SetPerVertexAlpha(f32 fireballAlpha);

// ReDead actions (from z_en_rd.c)
#define RD_ACTION_STUNNED 1
#define RD_ACTION_GRABBING 10
#define RD_ACTION_DAMAGE 11
#define RD_ACTION_DEAD 12

// ---------------------------------------------------------------------------
// Per-actor tag data (recomp actor extension), so we know which actors we
// spawned and can clean them up.
// ---------------------------------------------------------------------------

typedef enum {
    TAG_NONE,
    TAG_EVENT,    // enemies and critters spawned by an event
    TAG_LINKBOMB, // bomb whose explosion only hurts Link
    TAG_RUPOOR,
    TAG_METEOR,
    TAG_STALKER,
    TAG_OURMOON,  // moon spawned for Moonfall where the area has none
    TAG_NEMESIS,  // the Nemesis
    TAG_FATE,     // Terrible Fate's Mask Salesman (moved by us, never updates)
    TAG_ECHO,     // Echo's ghost (moved by us, never updates)
    TAG_KAMIKAZE, // Kamikaze Keese
    TAG_WOLF,     // Wolf Pack
    TAG_TEAM_A,   // Infighting, first group
    TAG_TEAM_B,   // Infighting, second group
    TAG_IMPOSTER, // Imposters (carpenter puppet)
    TAG_UPRISING, // Carpenter Uprising (rainbow carpenter puppet)
    TAG_PROCESSION, // Procession of the Dead (marching ReDead puppet)
    TAG_ROCK,     // Poltergeist rock
    TAG_BULLET,   // Bullet Hell nut
    TAG_MIRAGE_REAL,
    TAG_MIRAGE_FAKE,
    TAG_MIRROR,   // Mirror Dance dancer
    TAG_SWARM,    // Swarm Night Keese
    TAG_SKYROCK,  // burning boulder falling from the sky (Moonfall, Majora Looms)
    TAG_BOUNTY,   // 2.0: bounty target
    TAG_NEMV2,    // 2.0: your Nemesis
    TAG_V2,       // 2.0: extras (mutators, summons, splits)
    TAG_ENEMYBOMB, // 2.0: explosion that only hurts enemies
    TAG_BOARD,    // 2.0: the Bounty Board carpenter
    TAG_DECOY,    // 2.6: Moon Pouch Decoy (a frozen carpenter)
    TAG_KEEP      // existing actor that must be left alone
} ActorTag;

#define TAGF_WAS_ALIVE (1 << 0)
#define TAGF_REWARDED (1 << 1)
#define TAGF_DUEL_HOLD (1 << 2) // enemy that was already here when a Champion Duel began

typedef struct {
    u8 tag;
    u8 dead;
    u8 origSaved;
    u8 flags;
    s16 timer;
    s16 timer2;
    u32 drawFunc;
    Vec3f origPos;
    f32 savedGlow;
    u8 glowSaved;
    u8 state;  // per-event state (fourth wave)
    u8 group;  // per-event index or group (fourth wave)
    s16 timer3;
    u8 hp; // Cursed Tag: health last frame
    u8 hp2;      // 2.0: health last frame
    u8 v2flags;
    u8 maxHp;
    u16 traits;  // 2.0: bounty / Nemesis traits
    s16 v2timer;
    u8 v3flags;   // V3
    u8 stolen;    // Thief: Moon Marks it took from you
    u16 reserve;  // Nemesis at Health critical mass: health beyond the game's limit
    u8 linkHits;  // times it hurt you (crowning a Nemesis)
    s16 v3timer;  // lunges, shield breaks
} TagData;
#define TAGF3_FLED (1 << 0)
#define TAGF3_EXP (1 << 1) // spawned by an Experimental friend: worth nothing
static s32 gChillTimer = 0; // chilled: the control stick only goes half as far

static ActorExtensionId sTagExt;
static s32 sTagExtValid = false;

static TagData* Tag_Get(Actor* actor) {
    if (!sTagExtValid || (actor == NULL)) {
        return NULL;
    }
    return (TagData*)z64recomp_get_extended_actor_data(actor, sTagExt);
}

static u8 Tag_Of(Actor* actor) {
    TagData* t = Tag_Get(actor);

    return (t != NULL) ? t->tag : TAG_NONE;
}

static s32 Tag_IsOurs(u8 tag) {
    return (tag != TAG_NONE) && (tag != TAG_KEEP);
}

// ---------------------------------------------------------------------------
// 1.1 features (defined in ev_v11.inc, included further down)
// ---------------------------------------------------------------------------

static s32 sCombo = -1;         // second event of a combo, or -1
// Is this event running, as either half of a combo?
#define EV_RUNNING(e) ((sActive == (e)) || (sCombo == (e)))
// Combo halves that are cleared: 1 the first, 2 the second. A combo only ends
// once every half with an end goal is cleared (friend combos can pair anything).
static s32 sComboHalt = 0;
static s32 sEndQuiet; // (set in ev_v11.inc)
static s32 sEventFailed; // (ev_v11.inc)
static s32 sFQuizCount;  // (ev_v11.inc)
#define END_PRICE 60      // Moon Marks to end an event the moon sent
static s32 sInComboPass = false; // running the combo's second event right now
static s32 sComboPrimary = -1;  // the first event, while the second one runs

// Doubled: a friend paired an event with itself (X combo X). It runs once, as
// one event (sCombo stays -1), throwing twice as much at you for the same time.
// Only friends double an event (friend start paths and Experimental op 40):
// the moon's own combos never pair an event with itself.
static s32 sDoubled = false;

// Events that can be doubled, and what doubling does to them.
static s32 Ev_CanDouble(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN:   // bombs twice as often
        case EV_RUPOOR_RAIN: // rupoors twice as often
        case EV_BLOOD_MOON:  // twice the horde (Odolwa stays one)
        case EV_MOONFALL:    // rocks twice as often
        case EV_SEARCH:      // twice the backup when a light catches you
        case EV_TINGLE:      // twice the bombs per drop
        case EV_SCYTHE:      // two sweeps on the go
        case EV_MOTHS:       // moths arrive twice as fast
        case EV_VOLLEY:      // volleys twice as often
            return true;
        default:
            return false; // (rules, puzzles and one-off threats don't double)
    }
}

static s32 sActive; // (defined below)

// Is the running event doubled?
static s32 Ev_IsDoubled(void) {
    return sDoubled && (sActive >= 0);
}
static s32 sCurse = -1;         // the most recently picked curse (-1: none)
static u32 sCurseMask = 0;      // every curse picked this cycle (the Moon Draft)
static s32 sCurseFavorite = -1; // Moon's Favorite: the favored event

typedef enum {
    CURSE_RESTLESS,    // events come more often
    CURSE_LINGER,      // events last longer
    CURSE_FIERCE,      // events one step harder
    CURSE_TWIN,        // combos are common
    CURSE_FAVORITE,    // one event comes up a lot
    CURSE_BLOODNIGHT,  // a Blood Moon every night at 6 PM
    CURSE_NIGHTTERROR, // events twice as often at night
    CURSE_SILENT,      // no warning or roulette before events
    CURSE_UNSEEN,      // event names hidden
    CURSE_AFTERSHOCK,  // a short Bomb Rain after every event
    CURSE_DOOM,        // Moon Pressure for the cycle
    CURSE_LEGION,      // Blood Moon hordes twice as big
    CURSE_GLASS,       // double damage during events
    CURSE_ENCORE,      // every event comes back a minute later
    CURSE_METEOR,      // boulders fall on you during events
    CURSE_AMBUSH,      // new areas may start a short Blood Moon
    CURSE_BELL,        // events start every 3 hours on the clock
    CURSE_HEX,         // Cursed Tag joins every event
    CURSE_WRATH,       // every 5 survived, the next is a harder combo
    CURSE_FLOAT,       // low gravity whenever an event is running
    CURSE_COUNT
} CurseId;

static s32 Curse_Is(s32 curse);
static s32 Curse_CanUse(s32 curse);
// V3 events (ev_v3.inc)
static s32 V3_IsEvent(s32 ev);
static s32 V3_IsLong(s32 ev);
// Experimental friends (ev_exp.inc)
static s32 Exp_AnyActive(void);
static void Exp_Panic(PlayState* play);
static f32 Exp_DrawScale(Actor* actor);
static s32 Exp_Frozen(Actor* actor);
static const char* Exp_Run(PlayState* play, s32 op, const char* t);
static void Exp_Update(PlayState* play, s32 playing);
static f32 sDirMult = 1.0f; // an Experimental friend can scale the Director's budget
static s32 sDirPicks = 0;   // Moon Draft picks held (kept in step by ev_v2.inc)
static s32 V3_Cur(void);
static s32 Quiz_Asking(void);
static const char* V3_Name(s32 ev);
static const char* V3_Counter(s32 ev);
static s32 V3_Tier(s32 ev);
static s32 V3_CanRun(PlayState* play, s32 ev);
static s32 V3_RandomOk(s32 ev);
static void V3_OnStart(PlayState* play, s32 ev);
static void V3_SceneSetup(PlayState* play);
static void V3_Tick(PlayState* play);
static void V3_OnEnd(PlayState* play, s32 success);
static void V3_Always(PlayState* play);
static Gfx* V3_Draw2D(PlayState* play, Gfx* gfx);
static s32 V3_Failed(void);

// 1.6 feature state
static s32 sWrathPending = false; // the next event is a Wrath event
static s32 sWrathEvent = false;   // the running event is one
static s32 sWildLevel = 1;        // Wild Intensity: this event's roll
static s32 sIsEncore = false;
static s32 sEncoreEvent = -1;
static s32 sPauseReason = 0; // 0 none, 1 safe zone, 2 waiting for day, 3 waiting for night
static s32 sMercyPending = false;

// 2.0 state needed early
typedef enum {
    // Slots 4-7 were event mutators until 2.6 (the curses cover those); 7 is retired.
    MUT_THINSKIN, MUT_TOUGH, MUT_VENGEFUL, MUT_REINFORCE, MUT_ELITE, MUT_HEARTLOCK, MUT_WOUNDS, MUT_RETIRED,
    MUT_MANATAX, MUT_AMMOTAX, MUT_BUTTERFINGERS, MUT_RUPEEROT, MUT_COLDWATER, MUT_RESTLESS, MUT_DARKNIGHTS,
    MUT_EMBOLDENED, MUT_HUNTED, MUT_TEARS, MUT_AMBUSH, MUT_ESCORT, MUT_WEAKHEAL, MUT_BLEEDING, MUT_COUNT
} MutatorId;
static u32 sMutMask = 0;
static u64 sBanMask = 0; // events banned this cycle (Moon Shop)
static PlayState* gPlayState_v2 = NULL;
#define MUT_ON(m) (gOpt[ID_V2_MUT] && (sMutMask & (1u << (m))))
static void V2_OnEventStartHook(PlayState* play);
static void V2_OnEventEnded(s32 ev, s32 quiet, s32 died);
static void V2_AfterHealthChange(s32 before);
static void V2_AfterActorUpdate(PlayState* play, Actor* actor);
static void V2_OnNewArea(void);
static void V2_OnRoomChange(void);
static void V2_OnNewCycle(void);
static void V2_OnLoadSave(s32 newFile);
static void V2_Update(PlayState* play, s32 playing);
static void V2_DrawWorld(PlayState* play);
static Gfx* V2_Draw(PlayState* play, Gfx* gfx);
s32 V2_WantsDraw(void);
s32 V2_ShouldFreeze(Actor* actor);
static void V2_Perception(PlayState* play, Actor* actor);
s32 V2_RunAction(PlayState* play, s32 id);
static s32 sRoomChanging = false;
static s32 sQuizMistakes = 0;
static s32 sHealthHookSeen = false; // the Health_ChangeBy hook handled this frame's change
static void Health_React(s32 before);
static s32 Ev_ShowWarning(void);
static void Ev_FormatTime(char* out, s32 frames);
static s32 Curse_ForcedCombo(PlayState* play, s32 primary);
static Gfx* NextClock_Draw(PlayState* play, Gfx* gfx);
static void Treasure_RemoveLight(PlayState* play);
static void Search_RemoveLights(PlayState* play);
static s32 Odolwa_AreaOk(PlayState* play);
static s32 Ev_PickPoolEnemy(s32 allowBosses);
static void Ev_MarkRewards(PlayState* play);
static void Ev_KillNewRewards(PlayState* play);

static s32 Ev_IsOn(s32 ev);
static s32 Ev_IntensityBonus(void);
static f32 Ev_DurationMult(void);
static s32 Ev_NextDelay(void);
static void Ev_Abort(PlayState* play);
static void Stats_OnStart(s32 ev, s32 combo);
static void Stats_OnEnd(void);
static void Card_ShowEvent(s32 ev, s32 combo, s32 fromRoulette);
static s32 CurseTag_Marked(PlayState* play, Actor* actor);
static void Wave5_OnStart(PlayState* play, s32 ev);
static void Wave5_SceneSetup(PlayState* play);
static void Wave5_Tick(PlayState* play);
static void Wave5_OnEnd(PlayState* play);
static void Wave5_ResetForPlayInit(void);
static void Wave5_OnRoomChange(PlayState* play);
static void Wave5_DrawWorld(PlayState* play);
static s32 Wave5_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b, f32* meter);
static void Prey_OnStart(PlayState* play);
static void Prey_SceneSetup(PlayState* play);
static void Tick_Prey(PlayState* play);
static void Prey_OnEnd(PlayState* play);
static void Prey_OnTimeout(PlayState* play);
static void Prey_ResetForPlayInit(void);
static void Prey_OnRoomChange(PlayState* play);
static void Prey_DrawWorld(PlayState* play);
static s32 Prey_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b);
static void Prey_AfterActorUpdate(PlayState* play, Actor* actor);
static s32 V2_UseMercy(void);
// Friend Link (2.4): picks your friend made from the web panel.
static s32 sFriendNext = -1;   // next random event
static s32 sForesight = false; // Foresight (Moon Service): sFriendNext is shown to you and locked in
static s32 sFriendCombo = -1;
static s32 sFriendDraftA = -1; // next mutator draft
static s32 sFriendDraftB = -1;
static s32 sFriendNowId = 0;    // its Friend Link command id (refunded if it never starts)
static void Link_DropFriendNow(void);
static s32 sFriendNow = -1;     // "start now" that had to wait (event running, can't run here)
static s32 sStartByFriend = false; // V3: the event about to start was your friend's doing (+25% pay)
static s32 sEventByFriend = false;
#define sNemCustom (sNem.custom) // a name your friend gave the current Nemesis (ev_v2.inc)
static s32 sLinkDeaths = 0;     // this session, for the panel
static s32 sLinkSurvived = 0;
static s32 sEvSeq = 0;          // events ended this session (for wagers)
static char sEvResult[48] = "";
static s32 sManualTrigger = false; // menu "Trigger event": Moon's Mercy doesn't apply
static s32 sFriendEmpowerNext = false; // your friend made the next event one step harder
static s32 sFriendEmpowered = false;   // ...and this is it
static s32 sFriendDarkTimer = 0;       // Blackout
static const char* Link_StateDesc(void);
static const char* Link_StateValue(char* out, s32 maxLen, u8* r, u8* g, u8* b);
static void Link_Update(PlayState* play, s32 playing);
// V3 Friend Link: a friend steering an event (0 tether, 1 moonfall, 2 searchlight).
static s32 Steer_Get(s32 kind, f32* x, f32* z);
static void Ev_NoPoeSouls(PlayState* play, Actor* actor);
static s32 sEndOnDeath = false; // Link fell during an event: it ends

// ---------------------------------------------------------------------------
// Enemy pool and critters
// ---------------------------------------------------------------------------

typedef struct {
    s16 actorId;
    s16 objectId;
    s16 params;
    s16 yOffset; // spawn this far above the ground
    s16 rotZ;    // some actors read rot.z as a setting
} SpawnDef;

static const SpawnDef sPool[POOL_COUNT] = {
    { ACTOR_EN_FIREFLY, OBJECT_FIREFLY, 0x0002, 60, 0 },       // Keese
    { ACTOR_EN_FIREFLY, OBJECT_FIREFLY, 0x0000, 60, 0 },       // Fire Keese
    { ACTOR_EN_FIREFLY, OBJECT_FIREFLY, 0x0004, 60, 0 },       // Ice Keese
    { ACTOR_EN_WF, OBJECT_WF, 0x0000, 0, 0 },                  // Wolfos
    { ACTOR_EN_WF, OBJECT_WF, 0x0001, 0, 0 },                  // White Wolfos
    { ACTOR_EN_SKB, OBJECT_SKB, 0x0010, 0, 0 },                // Stalchild
    { ACTOR_EN_RD, OBJECT_RD, 0x7FFF, 0, 0 },                  // ReDead (no switch flag, no mourning)
    { ACTOR_EN_POH, OBJECT_PO, 0x0000, 50, 0 },                // Poe
    { ACTOR_EN_SLIME, OBJECT_SLIME, (s16)0xFE00, 0, 0 },       // Chuchu
    { ACTOR_EN_TITE, OBJECT_TITE, 0x0000, 0, 0 },              // Tektite
    { ACTOR_EN_BB, OBJECT_BB, (s16)0xFF00, 0, 0 },             // Blue Bubble
    { ACTOR_EN_BBFALL, OBJECT_BB, 0x0000, 0, 0 },              // Red Bubble
    { ACTOR_EN_DEKUBABA, OBJECT_DEKUBABA, 0x0000, 0, 0 },      // Deku Baba
    { ACTOR_EN_DODONGO, OBJECT_DODONGO, 0x0000, 0, 0 },        // Dodongo
    { ACTOR_EN_CROW, OBJECT_CROW, 0x0000, 80, 0 },             // Guay
    { ACTOR_EN_FZ, OBJECT_FZ, 0x3000, 0, 0 },                  // Freezard (can turn)
    { ACTOR_EN_VM, OBJECT_VM, 0x0000, 0, 0 },                  // Beamos
    { ACTOR_EN_AM, OBJECT_AM, 0x0000, 0, 0 },                  // Armos
    { ACTOR_EN_RAT, OBJECT_RAT, (s16)0x8000, 0, 0 },           // Real Bombchu
    { ACTOR_EN_RR, OBJECT_RR, 0x0000, 0, 0 },                  // Like Like
    { ACTOR_EN_WALLMAS, OBJECT_WALLMASTER, 0x0000, 0, 0 },     // Wallmaster
    { ACTOR_EN_FLOORMAS, OBJECT_WALLMASTER, 0x0000, 0, 0 },    // Floormaster
    { ACTOR_EN_PP, OBJECT_PP, 0x0001, 0, 0 },                  // Hiploop (no mask)
    { ACTOR_EN_DEKUNUTS, OBJECT_DEKUNUTS, 0x0000, 0, 0 },      // Mad Scrub
    { ACTOR_EN_KAME, OBJECT_TL, 0x0000, 0, 0 },                // Snapper
    { ACTOR_EN_GRASSHOPPER, OBJECT_GRASSHOPPER, 0x0000, 90, 0 }, // Dragonfly
    { ACTOR_EN_MKK, OBJECT_MKK, 0x0000, 0, 6 },                // Boe (rot.z = drop table)
    { ACTOR_EN_BAGUO, OBJECT_GMO, 0x0000, 0, 0 },              // Nejiron
    { ACTOR_EN_PEEHAT, OBJECT_PH, 0x0000, 0, 0 },              // Peahat
    { ACTOR_EN_NEO_REEBA, OBJECT_RB, 0x0000, 0, 0 },           // Leever
    { ACTOR_EN_JSO2, OBJECT_JSO, 0x0001, 0, 0 },               // Garo Master
    { ACTOR_EN_IK, OBJECT_IK, 0x0001, 0, 0 },                  // Iron Knuckle
    { ACTOR_EN_DINOFOS, OBJECT_DINOFOS, 0x0000, 0, 0 },        // Dinolfos
    { ACTOR_EN_BIGPO, OBJECT_BIGPO, (s16)0xFF00, 0, 0 },       // Big Poe (no switch flag)
    { ACTOR_BOSS_01, OBJECT_BOSS01, 0x0000, 0, 0 },            // Odolwa
    { ACTOR_EN_JSO, OBJECT_JSO, 0x0000, 0, 0 },                // Garo (no ring of fire; see Garo_BeforeInit)
};

#define POOL_ODOLWA 34
#define POOL_BIGPOE 33
#define POOL_GARO 35 // (appended after Odolwa so saved pool ids stay put)

const char* Pool_Name(s32 index) {
    static const char* sNames[POOL_COUNT] = {
        "Keese",       "Fire Keese",  "Ice Keese",   "Wolfos",       "White Wolfos", "Stalchild",
        "ReDead",      "Poe",         "Chuchu",      "Tektite",      "Blue Bubble",  "Red Bubble",
        "Deku Baba",   "Dodongo",     "Guay",        "Freezard",     "Beamos",       "Armos",
        "Real Bombchu", "Like Like",  "Wallmaster",  "Floormaster",  "Hiploop",      "Mad Scrub",
        "Snapper",     "Dragonfly",   "Boe",         "Nejiron",      "Peahat",       "Leever",
        "Garo Master", "Iron Knuckle", "Dinolfos",   "Big Poe",      "BOSS: Odolwa", "Garo",
    };

    return ((index >= 0) && (index < POOL_COUNT)) ? sNames[index] : "???";
}

typedef enum {
    CRIT_CUCCO,
    CRIT_DOG,
    CRIT_COW,
    CRIT_FAIRY,
    CRIT_FROG,
    CRIT_CARPENTER,
    CRIT_FISH,
    CRIT_BUGS
} CritterId;

static const SpawnDef sCritters[CRIT_COUNT] = {
    { ACTOR_EN_NIW, OBJECT_NIW, (s16)0xFFFF, 0, 0 },       // Cucco
    { ACTOR_EN_DG, OBJECT_DOG, (s16)0xFC00, 0, 0 },        // Dog (we give it a path)
    { ACTOR_EN_COW, OBJECT_COW, 0x0000, 0, 0 },            // Cow
    { ACTOR_EN_ELF, GAMEPLAY_KEEP, 0x000A, 35, 0 },        // Fairy (no healing, can't be bottled)
    { ACTOR_EN_MINIFROG, OBJECT_FR, 0x0001, 0, 0 },        // Frog
    { ACTOR_EN_DAIKU, OBJECT_DAIKU, 0x0001, 0, 0 },        // Carpenter
    { ACTOR_EN_FISH, GAMEPLAY_KEEP, 0x0000, 30, 0 },       // Fish (flops around)
    { ACTOR_EN_INSECT, GAMEPLAY_KEEP, 0x0001, 5, 0 },      // Bugs
};

const char* Critter_Name(s32 index) {
    static const char* sNames[CRIT_COUNT] = { "Cuccos", "Dogs", "Cows", "Fairies",
                                              "Frogs",  "Carpenters", "Fish", "Bugs" };

    return ((index >= 0) && (index < CRIT_COUNT)) ? sNames[index] : "???";
}

const char* Events_Name(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN: return "Bomb Rain";
        case EV_RUPOOR_RAIN: return "Rupoor Rain";
        case EV_BLOOD_MOON: return "Blood Moon";
        case EV_JOKE: return "Joke Event";
        case EV_MOONFALL: return "Moonfall";
        case EV_STALKER: return "The Stalker";
        case EV_POE_HUNT: return "Poe Hunt";
        case EV_NEMESIS: return "Nemesis";
        case EV_PHANTOM: return "Phantom Army";
        case EV_TREASURE: return "Treasure Hunt";
        case EV_HUNGER: return "Blood Hunger";
        case EV_SEARCH: return "Searchlights";
        case EV_FATE: return "Terrible Fate";
        case EV_REDLIGHT: return "Red Light Green Light";
        case EV_ECHO: return "Echo";
        case EV_GLARE: return "Moon's Glare";
        case EV_KAMIKAZE: return "Kamikaze Keese";
        case EV_WOLFPACK: return "Wolf Pack";
        case EV_CHAMPION: return "Champion Duel";
        case EV_INFIGHT: return "Infighting";
        case EV_STILL: return "Time Moves w/ You";
        case EV_IMPOSTER: return "Imposters";
        case EV_UPRISING: return "Carpenter Uprising";
        case EV_PROCESSION: return "Procession of the Dead";
        case EV_POLTERGEIST: return "Poltergeist";
        case EV_BULLETHELL: return "Bullet Hell";
        case EV_MIRAGE: return "Mirage";
        case EV_MIRROR: return "Mirror Dance";
        case EV_SWARM: return "Swarm Night";
        case EV_MAJORA: return "Majora Looms";
        case EV_LEVIATHAN: return "Sky Leviathan";
        case EV_TINGLE: return "Tingle Air Raid";
        case EV_STAMPEDE: return "Goron Stampede";
        case EV_CURSETAG: return "Cursed Tag";
        case EV_QUIZ: return "Pop Quiz";
        case EV_LOWGRAV: return "Low Gravity";
        case EV_PREY: return "Moonlit Prey";
        default: return V3_Name(ev);
    }
}

const char* Events_StartLabel(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN: return "Start: Bomb Rain";
        case EV_RUPOOR_RAIN: return "Start: Rupoor Rain";
        case EV_BLOOD_MOON: return "Start: Blood Moon";
        case EV_JOKE: return "Start: Joke Event";
        case EV_MOONFALL: return "Start: Moonfall";
        case EV_STALKER: return "Start: The Stalker";
        case EV_POE_HUNT: return "Start: Poe Hunt";
        case EV_NEMESIS: return "Start: Nemesis";
        case EV_PHANTOM: return "Start: Phantom Army";
        case EV_TREASURE: return "Start: Treasure Hunt";
        case EV_HUNGER: return "Start: Blood Hunger";
        case EV_SEARCH: return "Start: Searchlights";
        case EV_FATE: return "Start: Terrible Fate";
        case EV_REDLIGHT: return "Start: Red/Green Light";
        case EV_ECHO: return "Start: Echo";
        case EV_GLARE: return "Start: Moon's Glare";
        case EV_KAMIKAZE: return "Start: Kamikaze Keese";
        case EV_WOLFPACK: return "Start: Wolf Pack";
        case EV_CHAMPION: return "Start: Champion Duel";
        case EV_INFIGHT: return "Start: Infighting";
        case EV_STILL: return "Start: Time Moves w/ You";
        case EV_IMPOSTER: return "Start: Imposters";
        case EV_UPRISING: return "Start: Carpenter Uprising";
        case EV_PROCESSION: return "Start: Procession";
        case EV_POLTERGEIST: return "Start: Poltergeist";
        case EV_BULLETHELL: return "Start: Bullet Hell";
        case EV_MIRAGE: return "Start: Mirage";
        case EV_MIRROR: return "Start: Mirror Dance";
        case EV_SWARM: return "Start: Swarm Night";
        case EV_MAJORA: return "Start: Majora Looms";
        case EV_LEVIATHAN: return "Start: Sky Leviathan";
        case EV_TINGLE: return "Start: Tingle Air Raid";
        case EV_STAMPEDE: return "Start: Goron Stampede";
        case EV_CURSETAG: return "Start: Cursed Tag";
        case EV_QUIZ: return "Start: Pop Quiz";
        case EV_LOWGRAV: return "Start: Low Gravity";
        case EV_PREY: return "Start: Moonlit Prey";
        default: return V3_Name(ev);
    }
}

// Retired events: kept in the code, but never offered or picked.
s32 Events_IsHidden(s32 ev) {
    // (The Nemesis event was folded into Champion Duel in 2.9.)
    // V3: Majora Looms (Moonfall), Imposters (Mirage), Poltergeist, Swarm Night (Blood Moon).
    // 3.1.7: Bullet Hell is gone too. 3.1.8: Infighting and Sky Leviathan.
    return (ev == EV_POE_HUNT) || (ev == EV_GLARE) || (ev == EV_WOLFPACK) || (ev == EV_STAMPEDE) ||
           (ev == EV_KAMIKAZE) || (ev == EV_NEMESIS) || (ev == EV_MAJORA) || (ev == EV_IMPOSTER) ||
           (ev == EV_POLTERGEIST) || (ev == EV_SWARM) || (ev == EV_BULLETHELL) || (ev == EV_INFIGHT) ||
           (ev == EV_LEVIATHAN);
}

static const char* Events_Flavor(s32 ev);
static const char* Events_FlavorOf(s32 ev) {
    return Events_Flavor(ev);
}

static const char* Events_Flavor(s32 ev) {
    switch (ev) {
        case EV_BOMB_RAIN: return "Take cover!";
        case EV_RUPOOR_RAIN: return "Guard your wallet!";
        case EV_BLOOD_MOON: return "Kill the whole horde to end it early.";
        case EV_JOKE: return "What is happening?";
        case EV_MOONFALL: return "Change direction!";
        case EV_STALKER: return "It knows where you are.";
        case EV_POE_HUNT: return "They are coming for you.";
        case EV_NEMESIS: return "Kill it to move on.";
        case EV_PHANTOM: return "Only the Lens sees them.";
        case EV_TREASURE: return "Follow the beeps and the golden glow.";
        case EV_HUNGER: return "Kill to live.";
        case EV_SEARCH: return "Stay out of the light.";
        case EV_FATE: return "Don't look away.";
        case EV_REDLIGHT: return "Freeze when it watches.";
        case EV_ECHO: return "Your past follows you.";
        case EV_GLARE: return "Don't move when it glares.";
        case EV_KAMIKAZE: return "Incoming!";
        case EV_WOLFPACK: return "The pack is circling.";
        case EV_CHAMPION: return "Defeat the champion.";
        case EV_INFIGHT: return "Caught in the crossfire!";
        case EV_STILL: return "Move and time moves.";
        case EV_IMPOSTER: return "Trust no one.";
        case EV_UPRISING: return "Mutoh's crew has had enough!";
        case EV_PROCESSION: return "Don't cross their path.";
        case EV_POLTERGEIST: return "Something is here.";
        case EV_BULLETHELL: return "Dodge!";
        case EV_MIRAGE: return "Only one is real.";
        case EV_MIRROR: return "They copy your every move.";
        case EV_SWARM: return "The sky is alive.";
        case EV_MAJORA: return "It's watching you.";
        case EV_LEVIATHAN: return "Something swims above.";
        case EV_TINGLE: return "Pop his balloon to stop him!";
        case EV_STAMPEDE: return "Get out of the marked lanes!";
        case EV_CURSETAG: return "Hit an enemy to pass it on!";
        case EV_QUIZ: return "Answer with the D-pad!";
        case EV_LOWGRAV: return "Mind your step...";
        case EV_PREY: return "The moon has chosen its prey.";
        default: return V3_Counter(ev);
    }
}

// ---------------------------------------------------------------------------
// Settings helpers
// ---------------------------------------------------------------------------

// The chosen intensity, raised by Moon Pressure and the Fierce Moon curse.
static s32 sActive;
typedef struct {
    Vec3f pos;
    s16 timer;
} BombMark;
static BombMark sBombMarks[16];
static s32 sBombMarkCount = 0;

static s32 sDirSpent = 0;     // V3 Director: threat bought this event
static s32 sBloodSpawned = 0; // Blood Moon enemies spawned this event

static s32 Intensity(void) {
    // V3: one fixed tuning per event. Fierce Moon and a friend's Empower push it a tier up.
    s32 lvl = INTENSITY_NORMAL + Ev_IntensityBonus();

    return MIN(lvl, INTENSITY_MAX - 1);
}

static s32 IntervalFrames(void) {
    static const u8 sMinutes[INTERVAL_MAX] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30 };

    return sMinutes[gOpt[ID_EV_INTERVAL] % INTERVAL_MAX] * 60 * FPS;
}

static s32 DurationFrames(void) {
    static const u16 sSeconds[DURATION_MAX] = { 15, 30, 60, 120, 180, 240, 300 };

    return (s32)(sSeconds[gOpt[ID_EV_DURATION] % DURATION_MAX] * FPS * Ev_DurationMult() *
                 1.0f);
}

static s32 HordeSize(void) {
    static const u8 sSizes[HORDE_MAX] = { 10, 20, 30, 40 };

    s32 size = sSizes[gOpt[ID_POOL_HORDE] % HORDE_MAX];

    return Curse_Is(CURSE_LEGION) ? MIN(size * 2, 50) : size;
}

static s32 CritterAmount(void) {
    static const u8 sAmounts[CRITAMT_MAX] = { 30, 60, 100 };

    return sAmounts[gOpt[ID_CRIT_AMOUNT] % CRITAMT_MAX];
}

static f32 MeteorScale(void) {
    static const f32 sScales[METEOR_MAX] = { 0.015f, 0.03f, 0.06f };

    return sScales[gOpt[ID_MF_METEOR_SIZE] % METEOR_MAX];
}

void Events_SetDefaults(void) {
    s32 i;

    gOpt[ID_EV_MASTER] = true;
    gOpt[ID_EV_INTERVAL] = INTERVAL_2;
    gOpt[ID_EV_DURATION] = DURATION_30S;
    gOpt[ID_EV_INTENSITY] = INTENSITY_NORMAL;
    for (i = 0; i < EV_COUNT; i++) {
        gOpt[ID_EV_ON_FIRST + i] = true;
    }
    gOpt[ID_POOL_HORDE] = HORDE_20;
    for (i = 0; i < POOL_COUNT; i++) {
        gOpt[ID_POOL_FIRST + i] = !POOL_IS_BOSS(i); // bosses start off
    }
    gOpt[ID_CRIT_AMOUNT] = CRITAMT_100;
    for (i = 0; i < CRIT_COUNT; i++) {
        gOpt[ID_CRIT_FIRST + i] = true;
    }
    gOpt[ID_MF_METEOR_SIZE] = METEOR_MEDIUM;
    gOpt[ID_MF_SWOOP] = true;
    gOpt[ID_MF_MUSIC] = true;
    gOpt[ID_MF_QUAKE] = true;

    gOpt[ID_NEM_TYPE] = NEMTYPE_ANY;
    gOpt[ID_NEM_SIZE] = LVL_MID;
    gOpt[ID_NEM_GLOW] = true;
    gOpt[ID_PH_SIZE] = LVL_MID;
    gOpt[ID_PH_SOLDIERS] = SOLDIERS_MIXED;
    gOpt[ID_PH_SHIMMER] = LVL_MID;
    gOpt[ID_TH_DIST] = LVL_MID;
    gOpt[ID_TH_TIME] = STEP_3;
    gOpt[ID_TH_ARROW] = true;
    gOpt[ID_BH_HEAL] = LVL_LOW;
    gOpt[ID_BH_ENEMIES] = LVL_MID;
    gOpt[ID_SL_COUNT] = STEP_2;
    gOpt[ID_SL_SIZE] = LVL_MID;
    gOpt[ID_SL_BACKUP] = STEP_3;
    gOpt[ID_TF_COUNT] = LVL_LOW;
    gOpt[ID_TF_SPEED] = LVL_MID;
    gOpt[ID_TF_WALLS] = true;
    gOpt[ID_RL_REACT] = LVL_MID;
    gOpt[ID_RL_GREEN] = LVL_MID;
    gOpt[ID_RL_RED] = LVL_MID;
    gOpt[ID_EC_DELAY] = STEP_2;
    gOpt[ID_EC_CLOSER] = true;
    gOpt[ID_MG_LENGTH] = LVL_MID;
    gOpt[ID_MG_REACT] = LVL_MID;
    gOpt[ID_KK_SWARM] = LVL_MID;
    gOpt[ID_KK_SPEED] = LVL_MID;
    gOpt[ID_WP_SIZE] = LVL_MID;
    gOpt[ID_WP_WHITE] = LVL_MID;
    gOpt[ID_CD_WHO] = CHAMP_RANDOM;
    gOpt[ID_CD_HEALTH] = LVL_LOW;
    gOpt[ID_CD_FREEZE] = true;
    gOpt[ID_IF_SIZE] = LVL_MID;
    gOpt[ID_ST_COUNT] = LVL_MID;
    gOpt[ID_ST_STILL] = LVL_LOW; // frozen solid
    gOpt[ID_IM_CROWD] = LVL_MID;
    gOpt[ID_IM_SHARE] = LVL_MID;
    gOpt[ID_CU_COUNT] = LVL_MID;
    gOpt[ID_CU_HITS] = LVL_MID;
    gOpt[ID_PR_LENGTH] = LVL_MID;
    gOpt[ID_PG_ROCKS] = LVL_MID;
    gOpt[ID_BL_RING] = LVL_MID;
    gOpt[ID_BL_FAN] = LVL_MID;
    gOpt[ID_MI_GROUPS] = LVL_MID;
    gOpt[ID_MI_FAKES] = LVL_MID;
    gOpt[ID_MD_COUNT] = LVL_MID;
    gOpt[ID_SN_COUNT] = LVL_MID;
    gOpt[ID_MJ_SIZE] = LVL_MID;
    gOpt[ID_LV_SIZE] = LVL_MID;
    gOpt[ID_EV_ROULETTE] = true;
    gOpt[ID_EV_COMBOS] = true;
    gOpt[ID_EV_CURSE] = true;
    gOpt[ID_EV_PRESSURE] = false;
    gOpt[ID_TA_RATE] = LVL_MID;
    gOpt[ID_TA_BOMBS] = LVL_MID;
    gOpt[ID_GS_LANES] = LVL_MID;
    gOpt[ID_GS_SPEED] = LVL_MID;
    gOpt[ID_CT_FUSE] = LVL_LOW; // 5 seconds
    gOpt[ID_CT_ENEMIES] = LVL_MID;
    gOpt[ID_EV_SAFEZONES] = false;
    gOpt[ID_V2_MUT] = true;
    gOpt[ID_V2_BOUNTY] = true;
    gOpt[ID_V2_NEM] = true;
    gOpt[ID_V2_POUCH] = true;
    gOpt[ID_V2_AUTOSAVE] = true;
    gOpt[ID_V2_NEWS] = true;
    gOpt[ID_V2_BANNERS] = true;
    gOpt[ID_V2_TRACKER] = true;
    gOpt[ID_V2_REMOTE] = true;
    gOpt[ID_V2_LINK] = true;
    gOpt[ID_V2_NEMDIFF] = 1;
    gOpt[ID_EV_TIMERSTYLE] = LVL_LOW; // Full
    gOpt[ID_EV_NEXTCLOCK] = false;
    gOpt[ID_EV_WARNSOUND] = true;
    gOpt[ID_EV_STREAKS] = true;
    gOpt[ID_EV_HOURS] = LVL_LOW; // any time
    gOpt[ID_EV_MERCY] = false;
    gOpt[ID_EV_WILD] = false;
}

// ---------------------------------------------------------------------------
// Object loading. Enemies need their model/animation "object" file loaded in
// the area. We keep our own copy in mod memory and temporarily add it to the
// end of the area's object list. Right before the game loads a new room's
// objects, we take our entries back out so the game never places its own
// objects on top of ours.
// ---------------------------------------------------------------------------

#define MAX_INJECTIONS 8
#define MAX_CACHED_OBJECTS 48

typedef struct {
    s16 objectId;
    s16 slot;
    void* buffer;
    void* savedSegment;
    void* savedNextSegment;
} Injection;

static Injection sInjections[MAX_INJECTIONS];
static s32 sInjectionCount = 0;

typedef struct {
    s16 objectId;
    void* buffer;
} CachedObject;

static CachedObject sObjectCache[MAX_CACHED_OBJECTS];
static s32 sObjectCacheCount = 0;

static void* Obj_GetBuffer(s16 objectId) {
    s32 i;
    uintptr_t vromStart;
    size_t size;
    void* buf;

    for (i = 0; i < sObjectCacheCount; i++) {
        if (sObjectCache[i].objectId == objectId) {
            return sObjectCache[i].buffer;
        }
    }
    if ((sObjectCacheCount >= MAX_CACHED_OBJECTS) || (objectId <= 0) || (objectId >= OBJECT_ID_MAX)) {
        return NULL;
    }

    vromStart = gObjectTable[objectId].vromStart;
    size = gObjectTable[objectId].vromEnd - vromStart;
    if (size == 0) {
        return NULL;
    }
    buf = recomp_alloc(size);
    if (buf == NULL) {
        return NULL;
    }
    DmaMgr_RequestSync(buf, vromStart, size);

    sObjectCache[sObjectCacheCount].objectId = objectId;
    sObjectCache[sObjectCacheCount].buffer = buf;
    sObjectCacheCount++;
    return buf;
}

// Makes sure an object is available in the area; returns its slot or -1.
static s32 Obj_Ensure(PlayState* play, s16 objectId) {
    ObjectContext* objectCtx = &play->objectCtx;
    s32 slot = Object_GetSlot(objectCtx, objectId);
    s32 n;
    void* buf;
    Injection* inj;

    if (slot > OBJECT_SLOT_NONE) {
        return slot;
    }
    if ((sInjectionCount >= MAX_INJECTIONS) || (objectCtx->numEntries >= ARRAY_COUNT(objectCtx->slots) - 1)) {
        return -1;
    }
    buf = Obj_GetBuffer(objectId);
    if (buf == NULL) {
        return -1;
    }

    n = objectCtx->numEntries;
    inj = &sInjections[sInjectionCount++];
    inj->objectId = objectId;
    inj->slot = n;
    inj->buffer = buf;
    inj->savedSegment = objectCtx->slots[n].segment;          // the game's "next free" pointer
    inj->savedNextSegment = objectCtx->slots[n + 1].segment;

    objectCtx->slots[n].id = objectId;
    objectCtx->slots[n].segment = buf;
    objectCtx->slots[n + 1].segment = inj->savedSegment;     // game objects keep going in object space
    objectCtx->numEntries++;
    return n;
}

static s32 Obj_IsInjectedSlot(s32 slot) {
    s32 i;

    for (i = 0; i < sInjectionCount; i++) {
        if (sInjections[i].slot == slot) {
            return true;
        }
    }
    return false;
}

// Is any actor that's still alive drawing from this object slot?
static s32 Obj_SlotInUse(PlayState* play, s32 slot) {
    s32 c;

    for (c = 0; c < ACTORCAT_MAX; c++) {
        Actor* actor = play->actorCtx.actorLists[c].first;

        while (actor != NULL) {
            if ((actor->update != NULL) && (actor->objectSlot == slot)) {
                return true;
            }
            actor = actor->next;
        }
    }
    return false;
}

// Takes our object entries back out. With keepLive, entries still used by a
// living actor stay (2.0 bounties, the Nemesis and their friends outlive
// events; if their object went away the game would delete them on the spot).
static void Obj_RemoveEx(PlayState* play, s32 keepLive) {
    ObjectContext* objectCtx = &play->objectCtx;
    s32 i;
    s32 kept = 0;
    Injection keep[MAX_INJECTIONS];

    for (i = sInjectionCount - 1; i >= 0; i--) {
        Injection* inj = &sInjections[i];
        ObjectEntry* entry = &objectCtx->slots[inj->slot];

        // Only touch the entry if it's still ours.
        if ((entry->id != inj->objectId) || (entry->segment != inj->buffer)) {
            continue;
        }
        if (keepLive && Obj_SlotInUse(play, inj->slot)) {
            keep[kept++] = *inj;
            continue;
        }
        if (objectCtx->numEntries == inj->slot + 1) {
            // Still the last entry: undo cleanly.
            entry->id = 0;
            entry->segment = inj->savedSegment;
            objectCtx->slots[inj->slot + 1].segment = inj->savedNextSegment;
            objectCtx->numEntries--;
        } else {
            // Something was added after it; leave an empty entry that points
            // at the game's own free space.
            entry->id = 0;
            entry->segment = inj->savedSegment;
        }
    }
    // Whatever stayed goes back in the list, oldest first.
    sInjectionCount = 0;
    for (i = kept - 1; i >= 0; i--) {
        sInjections[sInjectionCount++] = keep[i];
    }
}

static void Obj_RemoveAll(PlayState* play) {
    Obj_RemoveEx(play, true);
}

// ---------------------------------------------------------------------------
// Spawning helpers
// ---------------------------------------------------------------------------

static const u8 sScanCategories[] = { ACTORCAT_ENEMY, ACTORCAT_NPC, ACTORCAT_PROP, ACTORCAT_ITEMACTION,
                                      ACTORCAT_MISC,  ACTORCAT_BOSS, ACTORCAT_EXPLOSIVES };

// True if one of our spawns is already standing very close to this spot.
static s32 Ev_SpotCrowded(PlayState* play, Vec3f* pos) {
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sScanCategories); c++) {
        Actor* actor = play->actorCtx.actorLists[sScanCategories[c]].first;

        while (actor != NULL) {
            if ((actor->update != NULL) && (Tag_Of(actor) == TAG_EVENT) &&
                (Math_Vec3f_DistXZ(&actor->world.pos, pos) < 45.0f)) {
                return true;
            }
            actor = actor->next;
        }
    }
    return false;
}

static s32 Ev_FindSpot(PlayState* play, f32 minDist, f32 maxDist, s16 centerYaw, s16 yawRange, s32 spaced,
                       Vec3f* out) {
    Player* player = GET_PLAYER(play);
    Vec3f base = player->actor.world.pos;
    s32 attempt;

    for (attempt = 0; attempt < 16; attempt++) {
        s16 yaw = centerYaw + (s16)Rand_CenteredFloat((f32)yawRange);
        f32 dist = minDist + Rand_ZeroOne() * (maxDist - minDist);
        Vec3f probe;
        Vec3f from;
        Vec3f to;
        Vec3f hit;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;
        f32 waterY;
        WaterBox* waterBox;

        probe.x = base.x + Math_SinS(yaw) * dist;
        probe.y = base.y + 120.0f;
        probe.z = base.z + Math_CosS(yaw) * dist;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if ((floorY <= BGCHECK_Y_MIN) || (floorY < base.y - 200.0f) || (floorY > base.y + 120.0f)) {
            continue;
        }

        from = base;
        from.y += 40.0f;
        to.x = probe.x;
        to.y = floorY + 40.0f;
        to.z = probe.z;
        if ((dist > 1.0f) &&
            BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
            continue;
        }
        if (WaterBox_GetSurface1(play, &play->colCtx, probe.x, probe.z, &waterY, &waterBox) &&
            (waterY > floorY + 10.0f)) {
            continue;
        }

        out->x = probe.x;
        out->y = floorY;
        out->z = probe.z;
        if (spaced && Ev_SpotCrowded(play, out)) {
            continue;
        }
        return true;
    }
    return false;
}

// Height to drop things from: high above the spot, but under any ceiling.
static f32 Ev_SkyHeight(PlayState* play, Vec3f* groundPos, f32 wantedHeight) {
    Player* player = GET_PLAYER(play);
    Vec3f pos = *groundPos;
    f32 ceilingY;
    CollisionPoly* poly;
    s32 bgId;

    pos.y += 20.0f;
    if (BgCheck_EntityCheckCeiling(&play->colCtx, &ceilingY, &pos, wantedHeight, &poly, &bgId, &player->actor)) {
        return MAX(ceilingY - 30.0f, groundPos->y + 30.0f);
    }
    return groundPos->y + wantedHeight;
}

static void Ev_TagActor(Actor* actor, u8 tag) {
    TagData* t = Tag_Get(actor);

    if (t != NULL) {
        t->tag = tag;
    }
    actor->room = -1;
}

static Actor* Ev_Spawn(PlayState* play, s16 actorId, s16 objectId, Vec3f* pos, s16 rotX, s16 yaw, s16 rotZ,
                       s32 params, u8 tag) {
    Actor* actor;

    if ((objectId > GAMEPLAY_KEEP) && (Obj_Ensure(play, objectId) < 0)) {
        return NULL;
    }
    actor = Actor_Spawn(&play->actorCtx, play, actorId, pos->x, pos->y, pos->z, rotX, yaw, rotZ, params);
    if (actor == NULL) {
        return NULL;
    }
    // Some actors remove themselves straight away in their setup.
    if (actor->update == NULL) {
        return NULL;
    }
    Ev_TagActor(actor, tag);
    return actor;
}

static s32 Ev_CountTagged(PlayState* play, s32 category, u8 tag) {
    Actor* actor = play->actorCtx.actorLists[category].first;
    s32 count = 0;

    while (actor != NULL) {
        if ((actor->update != NULL) && (Tag_Of(actor) == tag)) {
            count++;
        }
        actor = actor->next;
    }
    return count;
}

static s32 Ev_IsLiveActor(PlayState* play, Actor* target) {
    s32 category;

    if (target == NULL) {
        return false;
    }
    for (category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = play->actorCtx.actorLists[category].first;

        while (actor != NULL) {
            if (actor == target) {
                return actor->update != NULL;
            }
            actor = actor->next;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Removing our actors safely. Some enemies grab Link, freeze him, steal his
// shield or keep pointers to helper actors, so everything is released first
// and every helper is removed in the same frame.
// ---------------------------------------------------------------------------

#define MAX_KILLS 256

static Actor* sKillList[MAX_KILLS];
static s32 sKillCount = 0;

static s32 Kill_Contains(Actor* actor) {
    s32 i;

    for (i = 0; i < sKillCount; i++) {
        if (sKillList[i] == actor) {
            return true;
        }
    }
    return false;
}

static void Kill_Add(Actor* actor) {
    if ((sKillCount < MAX_KILLS) && !Kill_Contains(actor)) {
        sKillList[sKillCount++] = actor;
    }
}

static void Ev_GiveShieldBack(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_NONE) {
        SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_HERO);
        if (player != NULL) {
            Player_SetEquipmentData(play, player);
        }
    }
}

static void Ev_EndOdolwaCutscene(PlayState* play, Actor* boss) {
    if (BOSS01_SUBCAM(boss) != SUB_CAM_ID_DONE) {
        func_80169AFC(play, BOSS01_SUBCAM(boss), 0);
        BOSS01_SUBCAM(boss) = SUB_CAM_ID_DONE;
        Cutscene_StopManual(play, &play->csCtx);
        Player_SetCsActionWithHaltedActors(play, boss, PLAYER_CSACTION_END);
    }
}

// Releases Link and other actors from anything in the kill list, then kills
// everything in it.
static s32 sMinibossMusicCheck = 0;

static void Ev_KillListed(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* arrow;
    s32 i;

    if (sKillCount == 0) {
        return;
    }

    if (player != NULL) {
        // Grabbed / swallowed / attached.
        if ((player->actor.parent != NULL) && Kill_Contains(player->actor.parent)) {
            player->actor.parent = NULL;
            player->av2.actionVar2 = 100;
        }
        // Frozen by one of their cutscene actions (Wallmaster, Big Poe, ...).
        if ((player->csActor != NULL) && Kill_Contains(player->csActor)) {
            Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
        }
        // Held in a grab (ReDead, Like Like, mini Floormaster).
        if (player->stateFlags2 & PLAYER_STATE2_80) {
            for (i = 0; i < sKillCount; i++) {
                if (Math_Vec3f_DistXYZ(&sKillList[i]->world.pos, &player->actor.world.pos) < 120.0f) {
                    player->av2.actionVar2 = 100;
                    break;
                }
            }
        }
    }

    for (i = 0; i < sKillCount; i++) {
        Actor* actor = sKillList[i];
        TagData* t = Tag_Get(actor);

        if ((actor->id == ACTOR_EN_RR) && (((EnRr*)actor)->unk_1E2 != 0)) {
            ((EnRr*)actor)->unk_1E2 = 0;
            Ev_GiveShieldBack(play); // the Like Like still had Link's shield
        } else if ((actor->id == ACTOR_EN_ITEM00) && ((actor->params & 0xFF) == ITEM00_SHIELD_HERO)) {
            Ev_GiveShieldBack(play); // dropped shield nobody picked up
        } else if ((actor->id == ACTOR_BOSS_01) && (Tag_Of(actor) == TAG_EVENT)) {
            Ev_EndOdolwaCutscene(play, actor);
        }
        if (t != NULL) {
            t->dead = true;
        }
    }

    // Arrows stuck in something we remove would keep writing to it.
    arrow = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;
    while (arrow != NULL) {
        if ((arrow->id == ACTOR_EN_ARROW) && (ENARROW_STUCK_ACTOR(arrow) != NULL) &&
            Kill_Contains(ENARROW_STUCK_ACTOR(arrow))) {
            ENARROW_STUCK_ACTOR(arrow) = NULL;
        }
        arrow = arrow->next;
    }

    for (i = 0; i < sKillCount; i++) {
        // These start the mini-boss music when they wake up and only put the
        // area's music back when they die the normal way.
        if ((sKillList[i]->id == ACTOR_EN_IK) || (sKillList[i]->id == ACTOR_EN_BIGPO) ||
            (sKillList[i]->id == ACTOR_EN_DINOFOS)) {
            sMinibossMusicCheck = 4;
        }
        Actor_Kill(sKillList[i]);
    }
    sKillCount = 0;
}

// Kills one actor (and anything attached to it) with the same safety steps.
static void Ev_KillOne(PlayState* play, Actor* target) {
    sKillCount = 0;
    Kill_Add(target);
    if ((target->child != NULL) && Ev_IsLiveActor(play, target->child) &&
        (target->child->category != ACTORCAT_PLAYER)) {
        Kill_Add(target->child);
    }
    Ev_KillListed(play);
}

// "Kill All Enemies" from the World menu, with the same safety steps.
// Odolwa's moth swarm is left alone because Odolwa keeps using it.
s32 Events_KillAllEnemies(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    s32 count;

    sKillCount = 0;
    while (actor != NULL) {
        if ((actor->update != NULL) && (actor->id != ACTOR_EN_TANRON1)) {
            Kill_Add(actor);
        }
        actor = actor->next;
    }
    count = sKillCount;
    Ev_KillListed(play);
    return count;
}

// Kills everything we spawned, helpers they made, and anything drawing from
// an object we added.
static void Ev_KillOurs(PlayState* play) {
    s32 category;
    s32 pass;
    s32 i;

    sKillCount = 0;
    for (category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = play->actorCtx.actorLists[category].first;

        if (category == ACTORCAT_PLAYER) {
            continue;
        }
        while (actor != NULL) {
            u8 tag = Tag_Of(actor);

            if (!sRoomChanging && ((tag == TAG_BOUNTY) || (tag == TAG_NEMV2) || (tag == TAG_V2) || (tag == TAG_BOARD) ||
                                  (tag == TAG_DECOY))) {
                // 2.0 enemies outlive events; only leaving the room removes them.
            } else if (Tag_IsOurs(tag) || ((actor->objectSlot >= 0) && Obj_IsInjectedSlot(actor->objectSlot))) {
                Kill_Add(actor);
            }
            actor = actor->next;
        }
    }

    // Helpers: actors whose parent is ours, and actors ours point to as child.
    for (pass = 0; pass < 2; pass++) {
        for (category = 0; category < ACTORCAT_MAX; category++) {
            Actor* actor = play->actorCtx.actorLists[category].first;

            if (category == ACTORCAT_PLAYER) {
                continue;
            }
            while (actor != NULL) {
                if (!Kill_Contains(actor)) {
                    if ((actor->parent != NULL) && Kill_Contains(actor->parent)) {
                        Kill_Add(actor);
                    } else {
                        for (i = 0; i < sKillCount; i++) {
                            if (sKillList[i]->child == actor) {
                                Kill_Add(actor);
                                break;
                            }
                        }
                    }
                }
                actor = actor->next;
            }
        }
    }

    Ev_KillListed(play);
}

// ---------------------------------------------------------------------------
// Atmosphere: darkness and color tints, sky filter, a dim light on Link.
// ---------------------------------------------------------------------------

typedef enum { ENV_NONE, ENV_DARK, ENV_RED, ENV_ORANGE, ENV_COLD, ENV_PURPLE, ENV_GREEN } EnvStyle;

static s32 sSkySaved = false;
static u8 sSavedSkyFilterOn;
static u8 sSavedSkyFilterColor[4];
static s32 sEnvDirty = false;
static LightInfo sLinkLightInfo;
static LightNode* sLinkLight = NULL;

// Odolwa takes over the area's lighting; this is what it was before.
static s32 sOdolwaLightSaved = false;
static u8 sSavedLightSetting;
static u8 sSavedPrevLightSetting;
static u8 sSavedLightSettingOverride;
static u8 sSavedLightBlendOverride;
static f32 sSavedLightBlend;

static void Ev_SaveLighting(PlayState* play) {
    if (!sOdolwaLightSaved) {
        sSavedLightSetting = play->envCtx.lightSetting;
        sSavedPrevLightSetting = play->envCtx.prevLightSetting;
        sSavedLightSettingOverride = play->envCtx.lightSettingOverride;
        sSavedLightBlendOverride = play->envCtx.lightBlendOverride;
        sSavedLightBlend = play->envCtx.lightBlend;
        sOdolwaLightSaved = true;
    }
}

static void Ev_RestoreLighting(PlayState* play) {
    if (sOdolwaLightSaved) {
        play->envCtx.lightSetting = sSavedLightSetting;
        play->envCtx.prevLightSetting = sSavedPrevLightSetting;
        play->envCtx.lightSettingOverride = sSavedLightSettingOverride;
        play->envCtx.lightBlendOverride = sSavedLightBlendOverride;
        play->envCtx.lightBlend = sSavedLightBlend;
        sOdolwaLightSaved = false;
    }
}

static void Ev_ClearEnv(PlayState* play) {
    AdjLightSettings* adj = &play->envCtx.adjLightSettings;
    s32 i;

    if (sEnvDirty) {
        for (i = 0; i < 3; i++) {
            adj->ambientColor[i] = 0;
            adj->light1Color[i] = 0;
            adj->light2Color[i] = 0;
            adj->fogColor[i] = 0;
        }
        adj->fogNear = 0;
        sEnvDirty = false;
    }
    if (sSkySaved) {
        play->envCtx.customSkyboxFilter = sSavedSkyFilterOn;
        for (i = 0; i < 4; i++) {
            play->envCtx.skyboxFilterColor[i] = sSavedSkyFilterColor[i];
        }
        sSkySaved = false;
    }
    if (sLinkLight != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sLinkLight);
        sLinkLight = NULL;
    }
}

static void Ev_ApplyEnv(PlayState* play, EnvStyle style, f32 f) {
    CurrentEnvLightSettings* base = &play->envCtx.lightSettings;
    AdjLightSettings* adj = &play->envCtx.adjLightSettings;
    Player* player = GET_PLAYER(play);
    s32 i;

    if (style == ENV_NONE) {
        return;
    }
    if (!sSkySaved) {
        sSavedSkyFilterOn = play->envCtx.customSkyboxFilter;
        for (i = 0; i < 4; i++) {
            sSavedSkyFilterColor[i] = play->envCtx.skyboxFilterColor[i];
        }
        sSkySaved = true;
    }
    sEnvDirty = true;

    if (style == ENV_DARK) {
        static const s16 sFogNearTarget[INTENSITY_MAX] = { 960, 930, 890 };
        s16 target = sFogNearTarget[Intensity()];

        for (i = 0; i < 3; i++) {
            adj->ambientColor[i] = (s16)(-base->ambientColor[i] * 0.85f * f);
            adj->light1Color[i] = (s16)(-base->light1Color[i] * 0.9f * f);
            adj->light2Color[i] = (s16)(-base->light2Color[i] * 0.9f * f);
            adj->fogColor[i] = (s16)(-base->fogColor[i] * f);
        }
        adj->fogNear = (base->fogNear > target) ? (s16)((target - base->fogNear) * f) : 0;
        play->envCtx.customSkyboxFilter = true;
        play->envCtx.skyboxFilterColor[0] = 0;
        play->envCtx.skyboxFilterColor[1] = 0;
        play->envCtx.skyboxFilterColor[2] = 0;
        play->envCtx.skyboxFilterColor[3] = (u8)(235.0f * f);

        // Faint light around Link so you can still see yourself.
        if ((sLinkLight == NULL) && (player != NULL)) {
            sLinkLight = LightContext_InsertLight(play, &play->lightCtx, &sLinkLightInfo);
        }
        if ((sLinkLight != NULL) && (player != NULL)) {
            Lights_PointNoGlowSetInfo(&sLinkLightInfo, player->actor.world.pos.x, player->actor.world.pos.y + 40.0f,
                                      player->actor.world.pos.z, (u8)(90 * f), (u8)(90 * f), (u8)(120 * f), 220);
        }
    } else {
        static const s16 sRed[3][3] = { { 60, -40, -40 }, { 90, -60, -60 }, { 150, 0, 0 } };
        static const s16 sOrange[3][3] = { { 50, 5, -50 }, { 80, 20, -70 }, { 210, 90, 20 } };
        static const s16 sCold[3][3] = { { -30, -5, 40 }, { -40, -5, 50 }, { 150, 175, 215 } };
        static const s16 sPurple[3][3] = { { 30, -30, 50 }, { 50, -45, 70 }, { 120, 40, 170 } };
        static const s16 sGreen[3][3] = { { -15, 35, -35 }, { -25, 55, -50 }, { 95, 150, 55 } };
        static const u8 sFilter[7][3] = { { 0, 0, 0 },     { 0, 0, 0 },    { 160, 0, 0 }, { 230, 90, 0 },
                                          { 120, 170, 230 }, { 130, 40, 190 }, { 110, 170, 40 } };
        const s16(*tint)[3] = (style == ENV_RED)      ? sRed
                              : (style == ENV_COLD)   ? sCold
                              : (style == ENV_PURPLE) ? sPurple
                              : (style == ENV_GREEN)  ? sGreen
                                                      : sOrange;
        s32 k = CLAMP((s32)style, 0, 6);

        for (i = 0; i < 3; i++) {
            adj->ambientColor[i] = (s16)(tint[0][i] * f);
            adj->light1Color[i] = (s16)(tint[1][i] * f);
            adj->light2Color[i] = (s16)(tint[1][i] * f);
            adj->fogColor[i] = (s16)((tint[2][i] - base->fogColor[i]) * f);
        }
        adj->fogNear = 0;
        play->envCtx.customSkyboxFilter = true;
        play->envCtx.skyboxFilterColor[0] = sFilter[k][0];
        play->envCtx.skyboxFilterColor[1] = sFilter[k][1];
        play->envCtx.skyboxFilterColor[2] = sFilter[k][2];
        play->envCtx.skyboxFilterColor[3] = (u8)(150.0f * f);
    }
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------

static s32 sMusicPlaying = false;  // an event song was started
static s32 sMusicChanged = false;  // ...and it replaced a different song
static u16 sEventSeqId = NA_BGM_DISABLED;
static u16 sAreaSeqId = NA_BGM_DISABLED; // what was playing before the event
static s32 sMusicStopDelay = 0;    // frames until the area music is put back
static s32 sMusicCheckFrames = 0;  // keep an eye on the music for a bit after entering an area

// Fight songs that may take over during an event (Odolwa, Garo Master, ...).
static s32 Ev_IsFightSong(u16 seqId) {
    return (seqId == NA_BGM_BOSS) || (seqId == NA_BGM_CLEAR_BOSS) || (seqId == NA_BGM_MINI_BOSS);
}

static void Ev_StartMusic(u16 seqId) {
    u16 cur = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);

    sMusicStopDelay = 0; // a new event song takes over from any pending restore
    sEventSeqId = seqId;

    if (sMusicPlaying) {
        if (cur == seqId) {
            return; // still going
        }
        if (Ev_IsFightSong(cur) || (cur == NA_BGM_FINAL_HOURS)) {
            // Replace it, but keep returning to the area's own song at the end.
            SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, seqId);
            sMusicChanged = true;
            return;
        }
        // The area's music changed (new area, time of day): start over from it.
    }

    sAreaSeqId = (cur == NA_BGM_DISABLED) ? NA_BGM_GENERAL_SFX : cur;
    sMusicChanged = (cur != seqId);
    Audio_PlayBgm_StorePrevBgm(seqId);
    sMusicPlaying = true;
}

static void Ev_StopMusic(void) {
    u16 cur;
    u16 prev;

    sMusicStopDelay = 0;
    sMinibossMusicCheck = 0; // the event's own restore takes care of it
    if (!sMusicPlaying) {
        return;
    }
    sMusicPlaying = false;
    if (!sMusicChanged) {
        return; // that song was already playing before the event
    }

    cur = AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN);
    if ((cur != sEventSeqId) && !Ev_IsFightSong(cur)) {
        return; // the game has already moved on to other music
    }

    // Put back what was playing, the same way the game's own restore does.
    prev = sAreaSeqId;
    if ((prev == NA_BGM_DISABLED) || (prev == NA_BGM_GENERAL_SFX)) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0);
    } else {
        if (prev == NA_BGM_AMBIENCE) {
            prev = sPrevAmbienceSeqId;
        }
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, prev + SEQ_FLAG_ASYNC);
    }
    sPrevMainBgmSeqId = NA_BGM_DISABLED;
}

// Some enemies put the music back themselves when they're removed. That
// happens during the frame after our cleanup, so ours waits a moment.
static void Ev_StopMusicSoon(void) {
    if (sMusicPlaying) {
        sMusicStopDelay = 3;
    }
}

static s32 Ev_AnyLiveMiniboss(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        if ((actor->update != NULL) && ((actor->id == ACTOR_EN_IK) || (actor->id == ACTOR_EN_BIGPO) ||
                                        (actor->id == ACTOR_EN_DINOFOS) || (actor->id == ACTOR_EN_JSO2))) {
            return true;
        }
        actor = actor->next;
    }
    return false;
}

static void Ev_UpdateMusic(PlayState* play) {
    if (sMusicStopDelay > 0) {
        sMusicStopDelay--;
        if (sMusicStopDelay == 0) {
            Ev_StopMusic();
        }
    }

    // A mini-boss we removed left its music playing: put the area's back,
    // unless an event song of ours is in charge or another mini-boss is around.
    // This waits a few frames so any restore the game does itself goes first.
    if (sMinibossMusicCheck > 0) {
        sMinibossMusicCheck--;
        if ((sMinibossMusicCheck == 0) && !sMusicPlaying && (sMusicStopDelay == 0) &&
            (AudioSeq_GetActiveSeqId(SEQ_PLAYER_BGM_MAIN) == NA_BGM_MINI_BOSS) && !Ev_AnyLiveMiniboss(play)) {
            Audio_RestorePrevBgm();
        }
    }
}

// ---------------------------------------------------------------------------
// Event state
// ---------------------------------------------------------------------------

static s32 sActive = -1; // current event, or -1
static s32 sFramesLeft = 0;    // play frames left in the current event
static s32 sTotalFrames = 1;   // length of the current event
static s32 sEventFrames = 0;   // frames since the event started (while playing)
static s32 sUntilNext = 0;     // play frames until the next random event
static s32 sLastEvent = -1;
static s32 sBannerTimer = 0;
static s32 sSceneReady = false;
static s32 sPlayReady = false;
static s32 sSpawnsLeft = 0;
static s32 sSpawnFailFrames = 0;
static s32 sBloodMoonPick = -1;
static s32 sCritterCursor = 0;
static s32 sPendingCycleReset = false;
static s32 sStalkerRespawnTimer = 0;
static s32 sRoomHadEnemies = false;

// Runs `stmt` a second time with the combo's second event as the current one.
#define EV_COMBO_PASS(stmt)                    \
    do {                                       \
        if ((sCombo >= 0) && (sActive >= 0)) { \
            s32 prim_ = sActive;               \
            sComboPrimary = prim_;             \
            sInComboPass = true;               \
            sActive = sCombo;                  \
            stmt;                              \
            sInComboPass = false;              \
            if (sActive >= 0) {                \
                sActive = prim_;               \
            }                                  \
        }                                      \
    } while (0)

// Second-wave events (defined further down).
static s32 Wave2_IsTimed(void);
static void Wave2_OnStart(PlayState* play, s32 ev);
static void Wave2_SceneSetup(PlayState* play);
static void Wave2_Tick(PlayState* play);
static void Wave2_OnTimeout(PlayState* play);
static void Wave2_OnEnd(PlayState* play);
static void Wave2_AlwaysUpdate(PlayState* play);
static void Wave2_ResetForPlayInit(void);
static void Wave2_OnRoomChange(PlayState* play);
static void Wave2_ShouldActorUpdate(Actor* actor, bool* should);
static f32 RL_MoonGlow(void);
static void Glare_Strike(PlayState* play);
static void Wave4_OnStart(PlayState* play, s32 ev);
static Actor* SkyRock_Launch(PlayState* play, Vec3f* target, s32 frames, f32 scale);
static void SkyRock_DrawMask(Actor* thisx, PlayState* play);
static void Wave4_SceneSetup(PlayState* play);
static void Wave4_Tick(PlayState* play);
static void Wave4_OnEnd(PlayState* play);
static void Wave4_ResetForPlayInit(void);
static void Wave4_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should);
static void Wave4_AfterActorUpdate(PlayState* play, Actor* actor, TagData* t);
static void Wave4_DrawWorld(PlayState* play);
static s32 Wave4_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b);
static s32 sStillFreeze;
static f32 sStillTint;
static Actor* sNemActor; // defined with the Nemesis code
static s32 Ev_Append(char* out, s32 n, const char* text, s32 maxLen);

static s32 Ev_IsOutdoors(PlayState* play) {
    return play->skyboxId != SKYBOX_NONE;
}

static s32 sQuizHolding; // Pop Quiz is holding Link still (defined with the quiz)

static s32 Ev_GameplayActive(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if ((player == NULL) || gMenuOpen || IS_PAUSED(&play->pauseCtx)) {
        return false;
    }
    if ((play->transitionTrigger != TRANS_TRIGGER_OFF) || (play->transitionMode != TRANS_MODE_OFF)) {
        return false;
    }
    // Pop Quiz holds Link in a cutscene pose itself; that one doesn't count.
    if ((CutsceneManager_GetCurrentCsId() != CS_ID_NONE) || (play->csCtx.state != CS_STATE_IDLE) ||
        (!sQuizHolding && Play_InCsMode(play))) {
        return false;
    }
    if (play->msgCtx.msgMode != MSGMODE_NONE) {
        return false;
    }
    if ((player->stateFlags1 & PLAYER_STATE1_DEAD) || (gSaveContext.save.saveInfo.playerData.health <= 0)) {
        return false; // going down: nothing new starts until he's back up
    }
    return true;
}

static s32 Ev_AnyPoolEnabled(void) {
    s32 i;

    for (i = 0; i < POOL_COUNT; i++) {
        if (gOpt[ID_POOL_FIRST + i]) {
            return true;
        }
    }
    return false;
}

static s32 Ev_AnyCritterEnabled(void) {
    s32 i;

    for (i = 0; i < CRIT_COUNT; i++) {
        if (gOpt[ID_CRIT_FIRST + i]) {
            return true;
        }
    }
    return false;
}

static s32 Ev_CanRun(PlayState* play, s32 ev) {
    if (Events_IsHidden(ev) || (sBanMask & (1ull << ev))) {
        return false;
    }
    switch (ev) {
        case EV_MOONFALL:
            return Ev_IsOutdoors(play);
        case EV_LOWGRAV:
            return !Curse_Is(CURSE_FLOAT); // Weightless Moon already makes every event low gravity
        case EV_BLOOD_MOON:
            // Odolwa can only fight on low, flat ground; other enemies anywhere.
            return (Ev_PickPoolEnemy(false) >= 0) || (gOpt[ID_POOL_FIRST + POOL_ODOLWA] && Odolwa_AreaOk(play));
        case EV_JOKE:
            return Ev_AnyCritterEnabled();
        case EV_GLARE:
        case EV_MAJORA:
        case EV_LEVIATHAN:
        case EV_TINGLE:
            return Ev_IsOutdoors(play); // needs a sky
        case EV_SWARM:
            return Ev_IsOutdoors(play) &&
                   ((play->roomCtx.curRoom.num < 0) || !Flags_GetClear(play, play->roomCtx.curRoom.num));
        case EV_KAMIKAZE:
        case EV_INFIGHT:
        case EV_STILL:
        case EV_IMPOSTER:
        case EV_BULLETHELL:
        case EV_MIRAGE:
        case EV_MIRROR:
        case EV_CURSETAG:
            // The game won't place enemies in a room that's already been cleared.
            return (play->roomCtx.curRoom.num < 0) || !Flags_GetClear(play, play->roomCtx.curRoom.num);
        default:
            return V3_IsEvent(ev) ? V3_CanRun(play, ev) : true;
    }
}

static s32 Ev_PickRandom(PlayState* play) {
    s32 candidates[EV_COUNT * 3];
    s32 count = 0;
    s32 i;

    for (i = 0; i < EV_COUNT; i++) {
        if (gOpt[ID_EV_ON_FIRST + i] && Ev_CanRun(play, i) && V3_RandomOk(i) && (i != sLastEvent)) {
            // Moon's Favorite (curse): half of all picks.
            if (Curse_Is(CURSE_FAVORITE) && (i == sCurseFavorite) && (Rand_ZeroOne() < 0.5f)) {
                return i;
            }
            candidates[count++] = i;
        }
    }
    // If the only option is the one that just ran, allow it anyway.
    if ((count == 0) && (sLastEvent >= 0) && gOpt[ID_EV_ON_FIRST + sLastEvent] && Ev_CanRun(play, sLastEvent) &&
        V3_RandomOk(sLastEvent)) {
        candidates[count++] = sLastEvent;
    }
    if (count == 0) {
        return -1;
    }
    return candidates[(s32)(Rand_ZeroOne() * count) % count];
}

static s32 Ev_PickPoolEnemy(s32 allowBosses) {
    s32 candidates[POOL_COUNT];
    s32 count = 0;
    s32 i;

    for (i = 0; i < POOL_COUNT; i++) {
        // V3: Big Poes are no fun to fight: never picked.
        if (gOpt[ID_POOL_FIRST + i] && (allowBosses || !POOL_IS_BOSS(i)) && (i != POOL_BIGPOE)) {
            candidates[count++] = i;
        }
    }
    if (count == 0) {
        return -1;
    }
    return candidates[(s32)(Rand_ZeroOne() * count) % count];
}

// Fade in over the first 2 seconds, out over the last 2.
static f32 Ev_Fade(void) {
    f32 in = sEventFrames / (2.0f * FPS);
    f32 out = sFramesLeft / (2.0f * FPS);
    f32 f = MIN(in, out);

    return CLAMP(f, 0.0f, 1.0f);
}

static void Ev_RestoreMoons(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_FALL) {
            TagData* t = Tag_Get(actor);

            if ((t != NULL) && t->origSaved) {
                actor->world.pos = t->origPos;
                t->origSaved = false;
            }
        }
        actor = actor->next;
    }
}

static s32 Ev_CountRoomEnemies(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    s32 count = 0;

    while (actor != NULL) {
        if ((actor->update != NULL) && !Tag_IsOurs(Tag_Of(actor)) && (actor->room == play->roomCtx.curRoom.num)) {
            count++;
        }
        actor = actor->next;
    }
    return count;
}

// V3: Searchlights' backup stays a little after the lights go out.
#define LINGER_MAX 12
static Actor* sLinger[LINGER_MAX];
static s16 sLingerT[LINGER_MAX];
static s32 sLingerN = 0;

static void Linger_Keep(PlayState* play, s32 frames) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    for (; (actor != NULL) && (sLingerN < LINGER_MAX); actor = actor->next) {
        TagData* t = Tag_Get(actor);

        if ((t != NULL) && (t->tag == TAG_EVENT) && (actor->update != NULL) && (actor->colChkInfo.health > 0)) {
            t->tag = TAG_V2; // outlives the event like the 2.0 extras do
            sLinger[sLingerN] = actor;
            sLingerT[sLingerN] = (s16)frames;
            sLingerN++;
        }
    }
}

static void Ev_Poof(PlayState* play, Vec3f* pos, s32 colorIndex);

// Something a friend sent (cuccos) that goes away by itself later.
static void Linger_Add(Actor* actor, s32 frames) {
    if (sLingerN < LINGER_MAX) {
        sLinger[sLingerN] = actor;
        sLingerT[sLingerN] = (s16)frames;
        sLingerN++;
    }
}

static s32 Linger_Exists(PlayState* play, Actor* target) {
    s32 c;

    for (c = 0; c < ACTORCAT_MAX; c++) {
        Actor* actor;

        for (actor = play->actorCtx.actorLists[c].first; actor != NULL; actor = actor->next) {
            if (actor == target) {
                return (actor->update != NULL) && ((c != ACTORCAT_ENEMY) || (actor->colChkInfo.health > 0));
            }
        }
    }
    return false;
}

static void Linger_Update(PlayState* play, s32 playing) {
    s32 i;

    for (i = sLingerN - 1; i >= 0; i--) {
        if (!Linger_Exists(play, sLinger[i])) {
            sLinger[i] = sLinger[--sLingerN];
            sLingerT[i] = sLingerT[sLingerN];
        } else if (playing && (--sLingerT[i] <= 0)) {
            Ev_Poof(play, &sLinger[i]->world.pos, 1);
            Actor_Kill(sLinger[i]);
            sLinger[i] = sLinger[--sLingerN];
            sLingerT[i] = sLingerT[sLingerN];
        }
    }
}

// Must be cleared to end: untimed events and the ported ones that run until you finish.
static s32 Ev_HasGoal(s32 ev) {
    return (ev == EV_NEMESIS) || (ev == EV_CHAMPION) || (ev == EV_QUIZ) || V3_IsLong(ev);
}

static void Ev_End(PlayState* play) {
    if (sActive < 0) {
        return;
    }
    // A combo half finishing (won, found, timed out) while the other half still has a goal:
    // that half stops, and the combo keeps going until the other is cleared too.
    if ((sCombo >= 0) && !sEndQuiet && (sComboHalt != 3)) {
        s32 half = sInComboPass ? 2 : 1;

        sComboHalt |= half;
        if ((sComboHalt != 3) && ((half == 2) || Ev_HasGoal(sCombo))) {
            static char sHalfToast[48];
            s32 n = Ev_Append(sHalfToast, 0, "Now clear ", sizeof(sHalfToast));

            n = Ev_Append(sHalfToast, n, Events_ShortName((half == 2) ? sComboPrimary : sCombo), sizeof(sHalfToast));
            Ev_Append(sHalfToast, n, (half == 2) ? " to end it." : "!", sizeof(sHalfToast));
            if ((half == 2) && !Ev_HasGoal(sComboPrimary)) {
                n = Ev_Append(sHalfToast, 0, "Now survive ", sizeof(sHalfToast));
                n = Ev_Append(sHalfToast, n, Events_ShortName(sComboPrimary), sizeof(sHalfToast));
                Ev_Append(sHalfToast, n, ".", sizeof(sHalfToast));
            }
            Menu_ShowToast(sHalfToast);
            return;
        }
    }
    sComboHalt = 0;
    if (EV_RUNNING(EV_QUIZ)) {
        sFQuizCount = 0; // a friend's questions are for this quiz only
    }
    if ((sActive == EV_SEARCH) || (sCombo == EV_SEARCH)) {
        Linger_Keep(play, 10 * FPS);
    }
    Stats_OnEnd();
    V3_OnEnd(play, gSaveContext.save.saveInfo.playerData.health > 0);
    Wave2_OnEnd(play);
    Ev_KillOurs(play);
    Ev_RestoreMoons(play);
    Ev_ClearEnv(play);
    Ev_RestoreLighting(play);
    Ev_StopMusicSoon();
    Obj_RemoveAll(play);

    // While our enemies were around, the room couldn't register that its own
    // enemies were all beaten (doors/chests that open on "defeat all
    // enemies"). If they're all gone now, let it know.
    if (sRoomHadEnemies && (play->roomCtx.curRoom.num >= 0) && (Ev_CountRoomEnemies(play) == 0)) {
        Flags_SetClearTemp(play, play->roomCtx.curRoom.num);
    }
    sRoomHadEnemies = false;

    sActive = -1;
    sCombo = -1;
    sDoubled = false;
    sInComboPass = false;
    sBannerTimer = 0;
    sUntilNext = Ev_NextDelay();
}

// Starts an event, optionally with a second one running alongside (combo).
// combo == ev doubles it (friends only, see sDoubled); other combos of an event
// with itself are dropped.
static void V3_DrawWorld(PlayState* play);
static void V3_DrawFriendWorld(PlayState* play);

static void Ev_StartFull(PlayState* play, s32 ev, s32 combo, s32 fromRoulette) {
    if (sActive >= 0) {
        Ev_Abort(play);
    }

    sActive = ev;
    sCombo = -1;
    sDoubled = (combo == ev) && Ev_CanDouble(ev);
    sComboHalt = 0;
    sBombMarkCount = 0;
    sDirSpent = 0;
    sBloodSpawned = 0;
    sWildLevel = (s32)(Rand_ZeroOne() * INTENSITY_MAX) % INTENSITY_MAX;
    sWrathEvent = sWrathPending;
    sWrathPending = false;
    sTotalFrames = sFramesLeft = DurationFrames();
    sEventFrames = 0;
    sBannerTimer = 5 * FPS;
    sSceneReady = false;
    sLastEvent = ev;
    sBloodMoonPick = (ev == EV_BLOOD_MOON) ? Ev_PickPoolEnemy(true) : -1;
    if ((sBloodMoonPick == POOL_ODOLWA) && !Odolwa_AreaOk(play)) {
        sBloodMoonPick = Ev_PickPoolEnemy(false);
    }
    Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
    Wave2_OnStart(play, ev);
    V3_OnStart(play, ev);
    if ((combo >= 0) && (combo != ev)) {
        sCombo = combo;
        EV_COMBO_PASS(Wave2_OnStart(play, sActive));
        EV_COMBO_PASS(V3_OnStart(play, sActive));
    }
    Stats_OnStart(ev, sCombo);
    Card_ShowEvent(ev, sCombo, fromRoulette);
    V2_OnEventStartHook(play);
}

static void Ev_Start(PlayState* play, s32 ev) {
    Ev_StartFull(play, ev, -1, false);
}

// ---------------------------------------------------------------------------
// Odolwa outside his arena
// ---------------------------------------------------------------------------

// Odolwa's fight code has a few height limits built in (he only lands from
// jumps below height 40 and ignores Link above 200), so he only spawns where
// those hold.
static s32 Odolwa_AreaOk(PlayState* play) {
    Player* player = GET_PLAYER(play);

    // In a room that's already been cleared the game refuses to spawn his moth
    // swarm, and his fight code needs it.
    if (Flags_GetClear(play, play->roomCtx.curRoom.num)) {
        return false;
    }
    return (player != NULL) && (player->actor.floorHeight < 25.0f) && (player->actor.world.pos.y < 180.0f) &&
           (player->actor.floorHeight > -400.0f);
}

static Actor* Ev_SpawnOdolwa(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 woodfallCleared;
    s32 introWatched;
    Actor* boss = NULL;
    Vec3f spot;
    s32 tries;

    for (tries = 0; tries < 8; tries++) {
        if (Ev_FindSpot(play, 200.0f, 450.0f, player->actor.shape.rot.y, 0x7FFF, false, &spot) && (spot.y < 25.0f) &&
            (spot.y > -400.0f)) {
            break;
        }
    }
    if ((tries == 8) || (Obj_Ensure(play, OBJECT_BOSS01) < 0)) {
        return NULL;
    }

    Ev_SaveLighting(play);

    // Spawn him as a fresh fight: not "already beaten" (that would drop a
    // Heart Container and a warp) and without the intro cutscene.
    woodfallCleared = CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE);
    introWatched = CHECK_EVENTINF(EVENTINF_INTRO_CS_WATCHED_ODOLWA);
    CLEAR_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE);
    SET_EVENTINF(EVENTINF_INTRO_CS_WATCHED_ODOLWA);

    Ev_MarkRewards(play);
    boss = Ev_Spawn(play, ACTOR_BOSS_01, 0, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos), 0, 0,
                    TAG_EVENT);
    Ev_KillNewRewards(play); // never leave a Heart Container or warp behind

    if (woodfallCleared) {
        SET_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE);
    }
    if (!introWatched) {
        CLEAR_EVENTINF(EVENTINF_INTRO_CS_WATCHED_ODOLWA);
    }

    // He must have his moth swarm; without it his fight code would crash.
    if ((boss != NULL) && ((boss->child == NULL) || (boss->child->id != ACTOR_EN_TANRON1))) {
        Ev_KillOne(play, boss);
        Ev_RestoreLighting(play);
        sSpawnsLeft = 0; // don't keep retrying here
        boss = NULL;
    }
    return boss;
}

// Marks existing heart containers and blue warps, so we can tell which ones
// Odolwa spawns this frame.
static void Ev_MarkRewards(PlayState* play) {
    static const u8 sCats[] = { ACTORCAT_BOSS, ACTORCAT_ITEMACTION, ACTORCAT_MISC };
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            if ((actor->id == ACTOR_ITEM_B_HEART) || (actor->id == ACTOR_DOOR_WARP1)) {
                TagData* t = Tag_Get(actor);

                if ((t != NULL) && (t->tag == TAG_NONE)) {
                    t->tag = TAG_KEEP;
                }
            }
            actor = actor->next;
        }
    }
}

static void Ev_KillNewRewards(PlayState* play) {
    static const u8 sCats[] = { ACTORCAT_BOSS, ACTORCAT_ITEMACTION, ACTORCAT_MISC };
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            if (((actor->id == ACTOR_ITEM_B_HEART) || (actor->id == ACTOR_DOOR_WARP1)) &&
                (Tag_Of(actor) == TAG_NONE) && (actor->update != NULL)) {
                Actor_Kill(actor);
            }
            actor = actor->next;
        }
    }
}

static Actor* sOdolwaUpdating = NULL;
static PlayState* sOdolwaPlay = NULL;

RECOMP_HOOK("Boss01_Update") void Events_BeforeOdolwaUpdate(Actor* thisx, PlayState* play) {
    sOdolwaUpdating = NULL;
    if (Tag_Of(thisx) != TAG_EVENT) {
        return;
    }

    // Stay in the first phase: the second phase warps him and Link to the
    // middle of his arena.
    BOSS01_PHASE_FRAMES(thisx) = 0;

    // He sometimes runs toward the middle of the map; bring him back.
    if ((thisx->xzDistToPlayer > 1100.0f) && (BOSS01_SUBCAM(thisx) == SUB_CAM_ID_DONE) &&
        (thisx->colChkInfo.health > 0)) {
        Vec3f spot;

        if (Ev_FindSpot(play, 250.0f, 450.0f, 0, 0x7FFF, false, &spot) && (spot.y < 25.0f)) {
            thisx->world.pos = spot;
            thisx->prevPos = spot;
        }
    }

    Ev_MarkRewards(play);
    sOdolwaUpdating = thisx;
    sOdolwaPlay = play;
}

RECOMP_HOOK_RETURN("Boss01_Update") void Events_AfterOdolwaUpdate(void) {
    if (sOdolwaUpdating == NULL) {
        return;
    }
    // No Heart Container or blue warp from an event Odolwa.
    Ev_KillNewRewards(sOdolwaPlay);
    sOdolwaUpdating = NULL;
}

// ---------------------------------------------------------------------------
// A plain Garo (EnJso) away from Ikana. The game only spawns it from a ring of
// fire (EnEncount3), whose cutscene id it reads in its setup, and it plays an
// intro cutscene and a hint conversation when beaten. A Garo with no parent gets
// a stand-in parent for its setup, skips the intro and fights at once, and
// bursts into flames when beaten (no conversation).
// ---------------------------------------------------------------------------

#define ENJSO_ACTION(actor) ACTOR_FIELD(actor, s16, 0x27C)       // EnJso.action
#define ENJSO_LOCKED_ON(actor) ACTOR_FIELD(actor, s16, 0x28C)    // EnJso.isPlayerLockedOn
#define ENJSO_ATTACKING(actor) ACTOR_FIELD(actor, s16, 0x28E)    // EnJso.isAttacking
#define ENJSO_CSID(actor) ACTOR_FIELD(actor, s16, 0x4B8)         // EnJso.csId
#define ENJSO_CS_STATE(actor) ACTOR_FIELD(actor, s16, 0x4C0)     // EnJso.cutsceneState
#define ENJSO_SUBCAM(actor) ACTOR_FIELD(actor, s16, 0x4C2)       // EnJso.subCamId
#define ENJSO_SKEL(actor) ACTOR_FIELD(actor, SkelAnime, 0x144)   // EnJso.skelAnime
#define ENJSO_ANIM_END(actor) ACTOR_FIELD(actor, f32, 0x350)     // EnJso.animEndFrame
#define ENJSO_ACTION_FALL_DOWN_AND_TALK 14
#define ENENCOUNT3_CSID_OFFSET 0x15A                              // EnEncount3.csId
#define ENENCOUNT3_SIZE 0x1CC
// EnJso_Draw sits this far after EnJso_Update in the same overlay (0x809B0BB0 - 0x809B02CC).
#define ENJSO_DRAW_FROM_UPDATE 0x8E4

void EnJso_SetupJumpBack(Actor* thisx);
void EnJso_SetupReappear(Actor* thisx, PlayState* play);
void EnJso_Guard(Actor* thisx, PlayState* play);

static u64 sGaroFakeParent[(ENENCOUNT3_SIZE + 7) / 8];
static Actor* sGaroIniting = NULL;
static Actor* sGaroUpdating = NULL;
static PlayState* sGaroPlay = NULL;

RECOMP_HOOK("EnJso_Init") void Garo_BeforeInit(Actor* thisx, PlayState* play) {
    sGaroIniting = NULL;
    if (thisx->parent != NULL) {
        return; // a real ring-of-fire Garo
    }
    bzero(sGaroFakeParent, sizeof(sGaroFakeParent));
    *(s16*)((u8*)sGaroFakeParent + ENENCOUNT3_CSID_OFFSET) = CS_ID_NONE;
    thisx->parent = (Actor*)sGaroFakeParent;
    sGaroIniting = thisx;
}

RECOMP_HOOK_RETURN("EnJso_Init") void Garo_AfterInit(void) {
    Actor* thisx = sGaroIniting;

    if (thisx == NULL) {
        return;
    }
    sGaroIniting = NULL;
    thisx->parent = NULL;
    // What the end of its intro cutscene does.
    ENJSO_CSID(thisx) = CS_ID_NONE;
    ENJSO_CS_STATE(thisx) = 0;
    ENJSO_SUBCAM(thisx) = SUB_CAM_ID_DONE;
    thisx->draw = (ActorFunc)((uintptr_t)thisx->update + ENJSO_DRAW_FROM_UPDATE);
    thisx->shape.yOffset = 970.0f;
    thisx->shape.shadowScale = 16.0f;
    thisx->flags &= ~(ACTOR_FLAG_FREEZE_EXCEPTION | ACTOR_FLAG_LOCK_ON_DISABLED);
    thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    EnJso_SetupJumpBack(thisx); // draws both swords and hops back, then circles Link
}

// Removed mid-attack (an event ending, a Mirage fake fading): the overlay's
// shared "a Garo is attacking" flag would stay set and the other Garos would
// never attack again. Clear it first.
RECOMP_HOOK("EnJso_Destroy") void Garo_BeforeDestroy(Actor* thisx, PlayState* play) {
    if ((thisx->parent == NULL) && ENJSO_ATTACKING(thisx)) {
        // Its guard step, at the end of its animation, clears the flag (no sounds or effects).
        ENJSO_SKEL(thisx).curFrame = ENJSO_ANIM_END(thisx);
        EnJso_Guard(thisx, play);
    }
}

RECOMP_HOOK("EnJso_Update") void Garo_BeforeUpdate(Actor* thisx, PlayState* play) {
    sGaroUpdating = (thisx->parent == NULL) ? thisx : NULL;
    sGaroPlay = play;
}

RECOMP_HOOK_RETURN("EnJso_Update") void Garo_AfterUpdate(void) {
    static Vec3f sFlameVel[] = {
        { 1.0f, 0.0f, 0.5f },   { 1.0f, 0.0f, -0.5f },  { -1.0f, 0.0f, 0.5f },
        { -1.0f, 0.0f, -0.5f }, { 0.5f, 0.0f, 1.0f },   { -0.5f, 0.0f, 1.0f },
        { 0.5f, 0.0f, -1.0f },  { -0.5f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f },
    };
    Actor* thisx = sGaroUpdating;
    PlayState* play = sGaroPlay;
    Vec3f pos;
    s32 i;

    sGaroUpdating = NULL;
    if ((thisx == NULL) || (play == NULL) || (thisx->update == NULL) ||
        (ENJSO_ACTION(thisx) != ENJSO_ACTION_FALL_DOWN_AND_TALK)) {
        return;
    }
    // Beaten: it would sit down and talk. Burn it away instead.
    pos = thisx->world.pos;
    pos.y = thisx->floorHeight;
    if (ENJSO_ATTACKING(thisx) || ENJSO_LOCKED_ON(thisx)) {
        EnJso_SetupReappear(thisx, play); // (only to clear the overlay's "a Garo is attacking" flags)
    }
    for (i = 0; i < ARRAY_COUNT(sFlameVel); i++) {
        Vec3f firePos = pos;

        firePos.x += Rand_CenteredFloat(30.0f);
        firePos.z += Rand_CenteredFloat(30.0f);
        func_800B3030(play, &firePos, &sFlameVel[i], &sFlameVel[i], (s16)(Rand_ZeroFloat(100.0f) + 100.0f), 20, 1);
    }
    SoundSource_PlaySfxEachFrameAtFixedWorldPos(play, &pos, 10, NA_SE_EN_COMMON_EXTINCT_LEV - SFX_FLAG);
    Actor_Kill(thisx);
}

// ---------------------------------------------------------------------------
// Dogs need a path to walk along, so they get little loops near Link.
// ---------------------------------------------------------------------------

#define DOG_PATHS 6
#define DOG_PATH_POINTS 4

static Path sDogPaths[DOG_PATHS];
static Vec3s* sDogPoints = NULL; // in the game's own heap (dogs read it like scene data)
static s32 sDogPathsReady = false;

static void Dog_BuildPaths(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 k;
    s32 p;

    if (sDogPoints == NULL) {
        sDogPoints = ZeldaArena_Malloc(sizeof(Vec3s) * DOG_PATHS * DOG_PATH_POINTS);
        if (sDogPoints == NULL) {
            return;
        }
    }
    for (k = 0; k < DOG_PATHS; k++) {
        Vec3f center;
        f32 radius = 90.0f + Rand_ZeroOne() * 90.0f;
        s16 start = (s16)(Rand_ZeroOne() * 0xFFFF);

        if (!Ev_FindSpot(play, 80.0f, 500.0f, 0, 0x7FFF, false, &center)) {
            center = player->actor.world.pos;
        }
        for (p = 0; p < DOG_PATH_POINTS; p++) {
            s16 yaw = start + p * (0x10000 / DOG_PATH_POINTS);
            Vec3s* point = &sDogPoints[k * DOG_PATH_POINTS + p];

            point->x = (s16)(center.x + Math_SinS(yaw) * radius);
            point->y = (s16)center.y;
            point->z = (s16)(center.z + Math_CosS(yaw) * radius);
        }
        sDogPaths[k].count = DOG_PATH_POINTS;
        sDogPaths[k].additionalPathIndex = 0xFF;
        sDogPaths[k].customValue = 0;
        sDogPaths[k].points = &sDogPoints[k * DOG_PATH_POINTS];
    }
    sDogPathsReady = true;
}

// ---------------------------------------------------------------------------
// Scene setup (runs once per area/room while an event is running)
// ---------------------------------------------------------------------------

// The song an event plays, or NA_BGM_DISABLED for none.
static u16 Ev_SongOf(s32 ev) {
    if (ev == EV_BLOOD_MOON) {
        return NA_BGM_MINI_BOSS;
    }
    if ((ev == EV_MOONFALL) && gOpt[ID_MF_MUSIC]) {
        return NA_BGM_FINAL_HOURS;
    }
    if (ev == EV_CHAMPION) {
        return NA_BGM_MINI_BOSS;
    }
    return NA_BGM_DISABLED;
}

// The first event's song, or the combo's if the first has none.
static u16 Ev_EventSong(void) {
    s32 prim = sInComboPass ? sComboPrimary : sActive;
    s32 combo = sInComboPass ? sActive : sCombo;
    u16 song = Ev_SongOf(prim);

    if ((song == NA_BGM_DISABLED) && (combo >= 0)) {
        song = Ev_SongOf(combo);
    }
    return song;
}

static void Ev_SceneSetupEvent(PlayState* play) {
    Player* player = GET_PLAYER(play);

    switch (sActive) {
        case EV_BLOOD_MOON:
            // Odolwa can only fight on fairly low, flat ground.
            if ((sBloodMoonPick == POOL_ODOLWA) && !Odolwa_AreaOk(play)) {
                sBloodMoonPick = Ev_PickPoolEnemy(false);
            }
            if (sBloodMoonPick >= 0) {
                sSpawnsLeft = POOL_IS_BOSS(sBloodMoonPick) ? 1 : HordeSize() * (Ev_IsDoubled() ? 2 : 1);
            }
            Ev_StartMusic(NA_BGM_MINI_BOSS);
            break;

        case EV_JOKE:
            sSpawnsLeft = CritterAmount();
            sDogPathsReady = false;
            break;

        case EV_POE_HUNT: {
            static const u8 sPoeCounts[INTENSITY_MAX] = { 4, 7, 12 };

            sSpawnsLeft = sPoeCounts[Intensity()];
            break;
        }

        case EV_STALKER:
            sStalkerRespawnTimer = 1;
            break;

        case EV_MOONFALL:
            if (Ev_EventSong() == NA_BGM_FINAL_HOURS) {
                Ev_StartMusic(NA_BGM_FINAL_HOURS);
            }
            // Areas without a moon get one of their own (outdoors only).
            if (gOpt[ID_MF_SWOOP] && Ev_IsOutdoors(play) && (player != NULL)) {
                Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;
                s32 hasMoon = false;

                while (actor != NULL) {
                    if ((actor->id == ACTOR_EN_FALL) && (actor->update != NULL)) {
                        s32 type = EN_FALL_TYPE(actor);

                        if ((type == EN_FALL_TYPE_TERMINA_FIELD_MOON) || (type == EN_FALL_TYPE_CLOCK_TOWER_MOON) ||
                            (type == EN_FALL_TYPE_LODMOON) || (type == EN_FALL_TYPE_LODMOON_NO_LERP) ||
                            (type == EN_FALL_TYPE_LODMOON_INVERTED_STONE_TOWER)) {
                            hasMoon = true;
                        }
                    }
                    actor = actor->next;
                }
                if (!hasMoon && (Obj_Ensure(play, OBJECT_FALL) >= 0)) {
                    Vec3f pos = player->actor.world.pos;

                    pos.x += Math_SinS(player->actor.shape.rot.y) * 8000.0f;
                    pos.y += 4000.0f;
                    pos.z += Math_CosS(player->actor.shape.rot.y) * 8000.0f;
                    Ev_Spawn(play, ACTOR_EN_FALL, 0, &pos, 0, 0, 0, 0, TAG_OURMOON);
                }
            }
            break;

        default:
            break;
    }

    Wave2_SceneSetup(play);
}

static void Ev_SceneSetup(PlayState* play) {
    sSpawnsLeft = 0;
    sSpawnFailFrames = 0;
    sStalkerRespawnTimer = 0;
    if (Ev_CountRoomEnemies(play) > 0) {
        sRoomHadEnemies = true;
    }
    Ev_SceneSetupEvent(play);
    EV_COMBO_PASS(Ev_SceneSetupEvent(play));
    V3_SceneSetup(play);
    EV_COMBO_PASS(V3_SceneSetup(play));

    // An area's own music can start a little after we arrive; check back.
    sMusicCheckFrames = 5 * FPS;
}

// ---------------------------------------------------------------------------
// Individual events (per-frame behaviour while playing)
// ---------------------------------------------------------------------------

static void Ev_SpawnLinkBomb(PlayState* play, Vec3f* pos, s16 timer) {
    Actor* actor =
        Ev_Spawn(play, ACTOR_EN_BOM, 0, pos, BOMB_EXPLOSIVE_TYPE_BOMB, 0, 0, BOMB_TYPE_BODY, TAG_LINKBOMB);

    if (actor != NULL) {
        ((EnBom*)actor)->timer = timer;
    }
}

// V3: bombs drop from high up (about 2 seconds of fall), a red mark shows where
// each lands, and nothing lands under a roof or overhang, so cover works.
static void Tick_BombRain(PlayState* play) {
    static const u8 sInterval[INTENSITY_MAX] = { 14, 10, 6 };
    Player* player = GET_PLAYER(play);
    Vec3f spot;
    Vec3f probe;
    f32 ceilingY;
    CollisionPoly* poly;
    s32 bgId;
    f32 reach = Ev_IsOutdoors(play) ? 700.0f : 380.0f;
    s32 k;
    s32 m = 0;

    for (k = 0; k < sBombMarkCount; k++) {
        if (--sBombMarks[k].timer > 0) {
            sBombMarks[m++] = sBombMarks[k];
        }
    }
    sBombMarkCount = m;
    if ((sEventFrames % (Ev_IsDoubled() ? MAX(sInterval[Intensity()] / 2, 2) : sInterval[Intensity()])) != 0) {
        return;
    }
    if (play->actorCtx.actorLists[ACTORCAT_EXPLOSIVES].length >= (Ev_IsDoubled() ? 24 : 16)) {
        return;
    }
    // About one in six aims where Link is heading.
    if (Rand_ZeroOne() < 0.18f) {
        Vec3f ahead = player->actor.world.pos;
        f32 floorY;

        ahead.x += player->actor.velocity.x * 15.0f;
        ahead.z += player->actor.velocity.z * 15.0f;
        ahead.y += 60.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &ahead);
        if (floorY > BGCHECK_Y_MIN + 10.0f) {
            spot = ahead;
            spot.y = floorY;
        } else if (!Ev_FindSpot(play, 0.0f, 50.0f, 0, 0x7FFF, false, &spot)) {
            return;
        }
    } else if (!Ev_FindSpot(play, 40.0f, reach, player->actor.shape.rot.y, 0x7FFF, false, &spot)) {
        return;
    }
    probe = spot;
    probe.y += 20.0f;
    if (BgCheck_EntityCheckCeiling(&play->colCtx, &ceilingY, &probe, 900.0f, &poly, &bgId, &player->actor)) {
        return; // under cover: it hits the roof instead
    }
    if (sBombMarkCount < ARRAY_COUNT(sBombMarks)) {
        sBombMarks[sBombMarkCount].pos = spot;
        sBombMarks[sBombMarkCount].timer = 50;
        sBombMarkCount++;
    }
    spot.y += 850.0f;
    Ev_SpawnLinkBomb(play, &spot, 75);
}

// --- Rupoors -----------------------------------------------------------------

#define RUPOOR_SCALE 0.2f

static char sRupoorToast[24];

static void Rupoor_Hit(PlayState* play) {
    static const u8 sPenalty[INTENSITY_MAX] = { 5, 10, 20 };
    s32 penalty = sPenalty[Intensity()];
    s32 wallet = gSaveContext.save.saveInfo.playerData.rupees + gSaveContext.rupeeAccumulator;
    Player* player = GET_PLAYER(play);
    const char* rest = " Rupees!";
    s32 n = 0;
    s32 i;

    // Wallet first, then the bank.
    if (wallet >= penalty) {
        Rupees_ChangeBy(-penalty);
    } else {
        s32 fromBank = penalty - MAX(wallet, 0);
        s32 bank = HS_GET_BANK_RUPEES();

        if (wallet > 0) {
            Rupees_ChangeBy(-wallet);
        }
        bank -= fromBank;
        HS_SET_BANK_RUPEES((bank < 0) ? 0 : bank);
    }
    Audio_PlaySfx(NA_SE_SY_ERROR);
    if (player != NULL) {
        Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 180, COLORFILTER_BUFFLAG_OPA, 8);
    }

    // "-10 Rupees!"
    sRupoorToast[n++] = '-';
    if (penalty >= 10) {
        sRupoorToast[n++] = '0' + (penalty / 10);
    }
    sRupoorToast[n++] = '0' + (penalty % 10);
    for (i = 0; rest[i] != '\0'; i++) {
        sRupoorToast[n++] = rest[i];
    }
    sRupoorToast[n] = '\0';
    Menu_ShowToast(sRupoorToast);
}

static void Rupoor_Update(Actor* thisx, PlayState* play) {
    TagData* t = Tag_Get(thisx);

    if (t == NULL) {
        Actor_Kill(thisx);
        return;
    }

    thisx->shape.rot.y += 0x700;
    if (!(thisx->bgCheckFlags & BGCHECKFLAG_GROUND)) {
        Actor_MoveWithGravity(thisx);
    } else {
        thisx->speed = 0.0f;
        thisx->velocity.y = 0.0f;
    }
    Actor_UpdateBgCheckInfo(play, thisx, 10.0f, 15.0f, 15.0f, UPDBGCHECKINFO_FLAG_1 | UPDBGCHECKINFO_FLAG_4);

    t->timer++;
    if (t->timer > 10 * FPS) {
        Actor_Kill(thisx);
        return;
    }
    if ((thisx->xzDistToPlayer < 28.0f) && (fabsf(thisx->playerHeightRel) < 45.0f)) {
        Rupoor_Hit(play);
        Actor_Kill(thisx);
    }
}

static void Rupoor_Draw(Actor* thisx, PlayState* play) {
    TagData* t = Tag_Get(thisx);
    void* segment = Obj_GetBuffer(OBJECT_GI_RUPY);
    uintptr_t savedSegment6;
    s32 pulse = (s32)(Math_SinS((s16)(play->gameplayFrames * 0x1200)) * 60.0f) + 60;

    if (segment == NULL) {
        return;
    }
    // Blink out during the last 2 seconds.
    if ((t != NULL) && (t->timer > 8 * FPS) && (play->gameplayFrames & 2)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, segment);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, 50 + pulse, 0, 25, 255);
    gSPFogPosition(POLY_OPA_DISP++, 0, 1500);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetFogColor(POLY_XLU_DISP++, 50 + pulse, 0, 25, 255);
    gSPFogPosition(POLY_XLU_DISP++, 0, 1500);
    CLOSE_DISPS(play->state.gfxCtx);

    savedSegment6 = gSegments[6];
    gSegments[6] = OS_K0_TO_PHYSICAL(segment);
    GetItem_Draw(play, GID_RUPEE_RED);
    gSegments[6] = savedSegment6;

    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    gDPPipeSync(POLY_XLU_DISP++);
    POLY_XLU_DISP = Play_SetFog(play, POLY_XLU_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}

static void Rupoor_SpawnAt(PlayState* play, Vec3f* spot);

static void Tick_RupoorRain(PlayState* play) {
    static const u8 sInterval[INTENSITY_MAX] = { 10, 6, 3 };
    Player* player = GET_PLAYER(play);
    Vec3f spot;

    if ((sEventFrames % (Ev_IsDoubled() ? MAX(sInterval[Intensity()] / 2, 2) : sInterval[Intensity()])) != 0) {
        return;
    }
    if (Ev_CountTagged(play, ACTORCAT_MISC, TAG_RUPOOR) >= (Ev_IsDoubled() ? 60 : 40)) {
        return;
    }
    if (!Ev_FindSpot(play, 0.0f, 350.0f, player->actor.shape.rot.y, 0x7FFF, false, &spot)) {
        return;
    }
    spot.y = Ev_SkyHeight(play, &spot, 250.0f);
    Rupoor_SpawnAt(play, &spot);
}

// One rupoor falling from `spot` (already up in the sky).
static void Rupoor_SpawnAt(PlayState* play, Vec3f* spot) {
    Actor* actor = Ev_Spawn(play, ACTOR_EN_ITEM00, 0, spot, 0, (s16)(Rand_ZeroOne() * 0xFFFF), 0, ITEM00_RUPEE_GREEN,
                            TAG_RUPOOR);

    if (actor != NULL) {
        actor->update = Rupoor_Update;
        actor->draw = Rupoor_Draw;
        Actor_SetScale(actor, RUPOOR_SCALE);
        actor->shape.yOffset = 12.0f / RUPOOR_SCALE;
        actor->shape.shadowDraw = NULL;
        actor->gravity = -1.2f;
        actor->terminalVelocity = -12.0f;
        actor->speed = Rand_ZeroOne() * 1.5f;
        actor->velocity.y = 0.0f;
        actor->world.rot.y = (s16)(Rand_ZeroOne() * 0xFFFF);
        actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
    }
}

// --- Blood Moon / Joke / Poe Hunt (staggered spawns) ---------------------------

// Extra setup some actors need right after spawning.
static void Ev_AfterSpawn(PlayState* play, Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_FLOORMAS:
            // Floormasters come as a set of three; keep the set together.
            if (actor->parent != NULL) {
                Ev_TagActor(actor->parent, TAG_EVENT);
            }
            if (actor->child != NULL) {
                Ev_TagActor(actor->child, TAG_EVENT);
            }
            break;

        case ACTOR_EN_IK:
            // Iron Knuckles sleep until hit; wake it up.
            ENIK_CYL_ACFLAGS(actor) |= AC_HIT;
            break;

        case ACTOR_EN_RR:
            // A beaten Like Like drops the shield it stole; that model needs
            // to be available here.
            Obj_Ensure(play, OBJECT_GI_SHIELD_2);
            break;

        case ACTOR_EN_COW:
            // The randomizer turns cows into item checks when you play Epona's
            // Song, and ignores cows with this value (normally a cow's tail).
            // It's only read again when the cow is removed, where it just skips
            // an empty cleanup step.
            actor->params = 1;
            break;

        case ACTOR_EN_DG:
            if (!sDogPathsReady) {
                Dog_BuildPaths(play);
            }
            if (sDogPathsReady) {
                ENDG_PATH(actor) = &sDogPaths[(s32)(Rand_ZeroOne() * DOG_PATHS) % DOG_PATHS];
            } else {
                Actor_Kill(actor);
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// V3 Director: each event has a pool of threat points by its tier and buys
// enemies with them (easy 1, medium 2, hard 4, Iron Knuckle 8). No more than
// half the pool is alive at once. Hard enemies only in Hard/Brutal events.
// Scales with your hearts: 3 hearts 70%, 10 hearts 100%, 20 hearts 140%.
// ---------------------------------------------------------------------------

static s32 Pool_Tier(s32 pool);
static s32 Ev_Tier(s32 ev);
static s32 Ev_CountOurEnemies(PlayState* play);
static s32 Ev_StepToward(PlayState* play, Actor* actor, Vec3f* target, f32 speed);

static s32 Dir_PoolOfActorId(s16 actorId) {
    s32 i;

    for (i = 0; i < POOL_COUNT; i++) {
        if ((sPool[i].actorId == actorId) && !POOL_IS_BOSS(i)) {
            return i;
        }
    }
    return -1;
}

static s32 Dir_Cost(s16 actorId) {
    static const u8 sCost[4] = { 1, 2, 4, 8 };
    s32 pool = Dir_PoolOfActorId(actorId);

    return (pool >= 0) ? sCost[Pool_Tier(pool)] : 0;
}

static s32 Dir_EventTier(void) {
    s32 tier = (sActive >= 0) ? Ev_Tier(sActive) : 1;

    {
        s32 other = sInComboPass ? sComboPrimary : sCombo;

        if ((other >= 0) && (other != sActive)) {
            tier = MAX(tier, Ev_Tier(other)); // a combo spends like its harder half
        }
    }
    if (Intensity() >= INTENSITY_BRUTAL) {
        tier++; // Fierce Moon, Empower
    }
    return MIN(tier, 3);
}

static s32 Dir_Budget(void) {
    static const u8 sBudget[4] = { 4, 8, 14, 24 };
    s32 hearts = gSaveContext.save.saveInfo.playerData.healthCapacity / 0x10;
    f32 scale = (hearts <= 10) ? (0.7f + 0.3f * (hearts - 3) / 7.0f) : (1.0f + 0.04f * (hearts - 10));

    scale = CLAMP(scale, 0.7f, 1.4f);
    if (gOpt[ID_V2_MUT]) {
        scale *= 1.0f + 0.05f * sDirPicks; // each Moon Draft pick: 5% more enemies (they pay 10% more)
    }
    if (Ev_IsDoubled()) {
        scale *= 2.0f; // doubled: twice the enemies
    }
    return MAX((s32)(sBudget[Dir_EventTier()] * scale * sDirMult + 0.5f), (sDirMult <= 0.0f) ? 0 : 2);
}

// Threat of our event enemies alive right now.
static s32 Dir_AliveCost(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    s32 cost = 0;

    while (actor != NULL) {
        u8 tag = Tag_Of(actor);

        if ((actor->update != NULL) && (actor->colChkInfo.health > 0) &&
            ((tag == TAG_EVENT) || (tag == TAG_MIRAGE_REAL) || (tag == TAG_TEAM_A) || (tag == TAG_TEAM_B))) {
            cost += Dir_Cost(actor->id);
        }
        actor = actor->next;
    }
    return cost;
}

// Can the event afford this enemy now?
static s32 Dir_Allow(PlayState* play, s16 actorId) {
    s32 cost = Dir_Cost(actorId);
    s32 budget = Dir_Budget();
    s32 pool = Dir_PoolOfActorId(actorId);

    if (cost == 0) {
        return true;
    }
    if ((pool >= 0) && (Pool_Tier(pool) >= 2) && (Dir_EventTier() < 2) && !EV_RUNNING(EV_BLOOD_MOON)) {
        return false; // tough enemies stay out of the easier events
    }
    if (sDirSpent + cost > budget * 2 + (EV_RUNNING(EV_SEARCH) ? 6 : 0)) {
        return false; // the whole pool (counted twice over a long event) is spent
    }
    // Searchlights' backup gets a little more room (one more enemy at a time).
    return Dir_AliveCost(play) + cost <= MAX(budget / 2 + (EV_RUNNING(EV_SEARCH) ? 2 : 0), cost);
}

// Out of budget for another enemy of this cost.
static s32 Dir_Exhausted(s32 cost) {
    return sDirSpent + MAX(cost, 1) > Dir_Budget() * 2;
}

static s32 sSpawnNear = false;

static void Ev_SpawnDef(PlayState* play, const SpawnDef* def, s32 params, f32 minDist, f32 maxDist) {
    Player* player = GET_PLAYER(play);
    Actor* actor;
    Vec3f spot;
    s32 found;

    if (!Dir_Allow(play, def->actorId)) {
        return;
    }
    // V3: outdoors, enemies come from much farther away and close in
    // (not Searchlights' backup: that one has to show up right away).
    found = false;
    if (!sSpawnNear && Ev_IsOutdoors(play) && (Dir_Cost(def->actorId) > 0)) {
        found = Ev_FindSpot(play, MAX(minDist, 800.0f), MIN(maxDist * 3.5f, 2500.0f), player->actor.shape.rot.y,
                            0x7FFF, true, &spot);
    }
    if (!found && !Ev_FindSpot(play, minDist, maxDist, player->actor.shape.rot.y, 0x7FFF, true, &spot)) {
        sSpawnFailFrames++;
        return;
    }
    spot.y += def->yOffset;
    actor = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                     def->rotZ, params, TAG_EVENT);
    if (actor != NULL) {
        Ev_AfterSpawn(play, actor);
        sDirSpent += Dir_Cost(def->actorId);
        actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        sSpawnsLeft--;
    } else {
        sSpawnFailFrames++;
    }
}

static void Tick_BloodMoon(PlayState* play) {
    s32 i;

    if (sBloodMoonPick < 0) {
        return;
    }
    if (sBloodMoonPick == POOL_ODOLWA) {
        if ((sSpawnsLeft > 0) && (sSpawnFailFrames < 200) && ((sEventFrames % 10) == 0)) {
            if (Ev_SpawnOdolwa(play) != NULL) {
                sSpawnsLeft--;
            } else {
                sSpawnFailFrames += 10;
            }
        }
        return;
    }
    for (i = 0; (i < 2) && (sSpawnsLeft > 0) && (sSpawnFailFrames < 90); i++) {
        const SpawnDef* def = &sPool[sBloodMoonPick];
        s32 before = sSpawnsLeft;

        Ev_SpawnDef(play, def, def->params, 220.0f, 650.0f);
        if (sSpawnsLeft < before) {
            sBloodSpawned++;
        }
    }
    // V3: wipe out the whole horde and the Blood Moon sets early.
    if ((sBloodSpawned > 0) && ((sSpawnsLeft <= 0) || Dir_Exhausted(Dir_Cost(sPool[sBloodMoonPick].actorId))) && (Ev_CountOurEnemies(play) == 0) &&
        (sFramesLeft > 1) && !sInComboPass) {
        Menu_ShowToast("The horde is broken!");
        sFramesLeft = 1;
    }
}

static void Tick_Joke(PlayState* play) {
    s32 i;

    for (i = 0; (i < 3) && (sSpawnsLeft > 0) && (sSpawnFailFrames < 120); i++) {
        const SpawnDef* def;
        s32 params;
        s32 tries;

        // Round-robin over the enabled critter types.
        for (tries = 0; tries < CRIT_COUNT; tries++) {
            sCritterCursor = (sCritterCursor + 1) % CRIT_COUNT;
            if (gOpt[ID_CRIT_FIRST + sCritterCursor]) {
                break;
            }
        }
        if (!gOpt[ID_CRIT_FIRST + sCritterCursor]) {
            return;
        }
        def = &sCritters[sCritterCursor];
        params = def->params;
        if (sCritterCursor == CRIT_FROG) {
            params = 1 + (sSpawnsLeft % 4); // the four frog colors
        } else if (sCritterCursor == CRIT_DOG) {
            params = ENDG_PARAMS(0x3F, sSpawnsLeft % 14); // different dogs
        }
        Ev_SpawnDef(play, def, params, 80.0f, 750.0f);
    }
}

static void Tick_PoeHunt(PlayState* play) {
    static const SpawnDef sPoe = { ACTOR_EN_POH, OBJECT_PO, 0, 50, 0 };
    static const u8 sPoeCounts[INTENSITY_MAX] = { 4, 7, 12 };

    if ((sSpawnsLeft > 0) && ((sEventFrames % 2) == 0)) {
        Ev_SpawnDef(play, &sPoe, sPoe.params, 350.0f, 750.0f);
    }

    // Keep the pack topped up.
    if ((sEventFrames % (5 * FPS)) == 0) {
        s32 alive = Ev_CountTagged(play, ACTORCAT_ENEMY, TAG_EVENT);

        if (alive < sPoeCounts[Intensity()]) {
            sSpawnsLeft = sPoeCounts[Intensity()] - alive;
            sSpawnFailFrames = 0;
        }
    }
}

// Enemies that come back to life forever, and ours that died, get cleaned up.
static void Ev_Upkeep(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        Actor* next = actor->next;
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_EVENT)) {
            if (t->timer < 0x7FFF) {
                t->timer++;
            }
            if ((actor->draw == NULL) && (t->timer > 20) &&
                ((actor->id == ACTOR_EN_SLIME) || (actor->id == ACTOR_EN_BB) || (actor->id == ACTOR_EN_CROW) ||
                 (actor->id == ACTOR_EN_RAT))) {
                Ev_KillOne(play, actor);
            }
        }
        actor = next;
    }
}

// Joke frogs hold still while Don Gero's Mask is on, so talking to them can't
// count toward the frog choir.
void Events_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should) {
    Player* player;

    if (V2_ShouldFreeze(actor) || Exp_Frozen(actor)) {
        *should = false; // Moon Hourglass, or an Experimental friend froze it
    } else {
        V2_Perception(play, actor); // Smoke Veil and Decoy
    }
    Wave2_ShouldActorUpdate(actor, should);
    Wave4_ShouldActorUpdate(play, actor, should);

    // Champion Duel: enemies that were already around wait out the duel.
    if (EV_RUNNING(EV_CHAMPION) && gOpt[ID_CD_FREEZE] && (actor->category == ACTORCAT_ENEMY)) {
        TagData* t = Tag_Get(actor);

        if ((t != NULL) && (t->tag == TAG_NONE) && (t->flags & TAGF_DUEL_HOLD) && (sNemActor != NULL)) {
            *should = false;
        }
    }

    if ((actor->id != ACTOR_EN_MINIFROG) || (Tag_Of(actor) != TAG_EVENT)) {
        return;
    }
    player = GET_PLAYER(play);
    if ((player != NULL) && (player->currentMask == PLAYER_MASK_DON_GERO)) {
        *should = false;
    }
}

// --- The Stalker ---------------------------------------------------------------

static void Stalker_Place(PlayState* play, Actor* stalker) {
    Player* player = GET_PLAYER(play);
    Vec3f spot;

    // Somewhere behind Link, out of sight.
    if (Ev_FindSpot(play, 450.0f, 750.0f, player->actor.shape.rot.y + 0x8000, 0x5000, false, &spot) ||
        Ev_FindSpot(play, 300.0f, 800.0f, 0, 0x7FFF, false, &spot)) {
        if (stalker == NULL) {
            Ev_Spawn(play, ACTOR_EN_RD, OBJECT_RD, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos), 0,
                     0x7FFF, TAG_STALKER);
        } else {
            stalker->world.pos = spot;
            stalker->prevPos = spot;
        }
    }
}

static void Tick_Stalker(PlayState* play) {
    if (Ev_CountTagged(play, ACTORCAT_ENEMY, TAG_STALKER) == 0) {
        if (sStalkerRespawnTimer > 0) {
            sStalkerRespawnTimer--;
            if (sStalkerRespawnTimer == 0) {
                Stalker_Place(play, NULL);
                sStalkerRespawnTimer = 3 * FPS; // retry later if placing failed
            }
        } else {
            sStalkerRespawnTimer = 3 * FPS;
        }
    }
}

RECOMP_HOOK("EnRd_Update") void Events_BeforeReDeadUpdate(Actor* thisx, PlayState* play) {
    static const f32 sSpeed[INTENSITY_MAX] = { 1.8f, 2.8f, 4.0f };
    static const u8 sDamage[INTENSITY_MAX] = { 0x30, 0x50, 0x80 }; // 3, 5, 8 hearts
    TagData* t = Tag_Get(thisx);
    Player* player = GET_PLAYER(play);
    u8 action;
    f32 dist;
    s32 busy;

    if ((t == NULL) || (t->tag != TAG_STALKER) || (player == NULL)) {
        return;
    }

    // Can't be killed, and never wanders back to where it started.
    thisx->colChkInfo.health = 200;
    thisx->home.pos = player->actor.world.pos;

    dist = thisx->xzDistToPlayer;
    if (dist > 1400.0f) {
        Stalker_Place(play, thisx);
        return;
    }

    action = ENRD_ACTION(thisx);
    busy = (action == RD_ACTION_STUNNED) || (action == RD_ACTION_GRABBING) || (action == RD_ACTION_DAMAGE) ||
           (action == RD_ACTION_DEAD);

    // Always closing in (except right after it lands a hit, so Link can get up).
    if (!busy && (dist > 40.0f) && (t->timer < 2 * FPS)) {
        f32 speed = sSpeed[Intensity()];
        Vec3f next = thisx->world.pos;
        Vec3f from;
        Vec3f to;
        Vec3f hit;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;

        next.x += Math_SinS(thisx->yawTowardsPlayer) * speed;
        next.z += Math_CosS(thisx->yawTowardsPlayer) * speed;
        from = thisx->world.pos;
        from.y += 25.0f;
        to = next;
        to.y += 25.0f;
        if (!BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
            to.y = next.y + 50.0f;
            floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &to);
            if ((floorY > BGCHECK_Y_MIN) && (fabsf(floorY - thisx->world.pos.y) < 60.0f)) {
                next.y = floorY;
                thisx->world.pos = next;
            }
        }
        thisx->world.rot.y = thisx->shape.rot.y = thisx->yawTowardsPlayer;
    }

    // Stuck behind something for 3 seconds: pop up somewhere else.
    t->timer2++;
    if (t->timer2 >= 3 * FPS) {
        if ((Math_Vec3f_DistXZ(&thisx->world.pos, &t->origPos) < 20.0f) && (dist > 150.0f)) {
            Stalker_Place(play, thisx);
        }
        t->origPos = thisx->world.pos;
        t->timer2 = 0;
    }

    // Heavy contact damage on top of its own attacks.
    if (t->timer > 0) {
        t->timer--;
    } else if (!busy && (dist < 45.0f) && (fabsf(thisx->playerHeightRel) < 60.0f)) {
        func_800B8D50(play, thisx, 8.0f, thisx->yawTowardsPlayer, 6.0f, sDamage[Intensity()]);
        t->timer = 5 * FPS; // stands still for 3 seconds, then 2 more before it can hit again
    }
}

// --- Moonfall ------------------------------------------------------------------

static void Tick_Moonfall(PlayState* play) {
    static const u8 sInterval[INTENSITY_MAX] = { 40, 26, 16 };
    static const u8 sFallTime[INTENSITY_MAX] = { 36, 30, 26 };
    static const f32 sSize[INTENSITY_MAX] = { 0.25f, 0.35f, 0.5f };
    Player* player = GET_PLAYER(play);
    CollisionPoly* poly;
    s32 bgId;
    Vec3f target;
    f32 floorY;

    if (player == NULL) {
        return;
    }
    if ((sEventFrames % 40) == 0) {
        Actor_RequestQuake(play, 4, 45);
        Rumble_Request(0.0f, 150, 10, 60);
    }
    if (!Ev_IsOutdoors(play) ||
        ((sEventFrames % (Ev_IsDoubled() ? sInterval[Intensity()] / 2 : sInterval[Intensity()])) != 0)) {
        return;
    }
    if (Ev_CountTagged(play, ACTORCAT_MISC, TAG_SKYROCK) >= (Ev_IsDoubled() ? 12 : 8)) {
        return;
    }

    // V3: aimed where Link is heading. Change direction and it misses.
    target = player->actor.world.pos;
    target.x += player->actor.velocity.x * 15.0f + Rand_CenteredFloat(120.0f);
    target.z += player->actor.velocity.z * 15.0f + Rand_CenteredFloat(120.0f);
    {
        f32 sx;
        f32 sz;

        // A friend is aiming: rocks land around their mark (never too far off).
        if (Steer_Get(1, &sx, &sz)) {
            f32 dx = sx - player->actor.world.pos.x;
            f32 dz = sz - player->actor.world.pos.z;
            f32 d = sqrtf(dx * dx + dz * dz);

            if (d > 900.0f) {
                dx *= 900.0f / d;
                dz *= 900.0f / d;
            }
            target.x = player->actor.world.pos.x + dx + Rand_CenteredFloat(80.0f);
            target.z = player->actor.world.pos.z + dz + Rand_CenteredFloat(80.0f);
        }
    }
    target.y += 50.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &target);
    target.y = (floorY > BGCHECK_Y_MIN) ? floorY : player->actor.world.pos.y;
    SkyRock_Launch(play, &target, sFallTime[Intensity()], sSize[Intensity()]);
}

static Actor* sMeteorUpdating = NULL;

RECOMP_HOOK("EnFall_Fireball_Update") void Events_BeforeMeteorUpdate(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    TagData* t = Tag_Get(thisx);
    Vec3f probe;
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY;
    s32 hitGround;
    s32 hitLink;

    sMeteorUpdating = NULL;
    if ((t == NULL) || (t->tag != TAG_METEOR) || t->dead) {
        return;
    }
    sMeteorUpdating = thisx;

    // The game's own draw function for the fireball; we keep putting it back
    // because the vanilla update hides it outside the moon crash cutscene.
    if (thisx->draw != NULL) {
        t->drawFunc = (u32)thisx->draw;
    }

    // Moon's Glare strikes home in on Link.
    if ((t->timer2 == 1) && (player != NULL)) {
        thisx->velocity.x = (player->actor.world.pos.x - thisx->world.pos.x) * 0.35f;
        thisx->velocity.z = (player->actor.world.pos.z - thisx->world.pos.z) * 0.35f;
    }

    thisx->world.pos.x += thisx->velocity.x;
    thisx->world.pos.y += thisx->velocity.y;
    thisx->world.pos.z += thisx->velocity.z;
    thisx->velocity.y = MAX(thisx->velocity.y - 0.6f, -40.0f);

    probe = thisx->world.pos;
    probe.y += 40.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    hitGround = (floorY > BGCHECK_Y_MIN) && (thisx->world.pos.y <= floorY + 15.0f);
    hitLink = (player != NULL) && (Math_Vec3f_DistXYZ(&thisx->world.pos, &player->actor.world.pos) < 50.0f);

    t->timer++;
    if (hitGround || hitLink || (t->timer > 8 * FPS)) {
        if (hitGround || hitLink) {
            Vec3f blast = thisx->world.pos;

            if (hitGround) {
                blast.y = floorY;
            }
            Ev_SpawnLinkBomb(play, &blast, 1); // explodes immediately, only hurts Link
        }
        t->dead = true;
        Actor_Kill(thisx);
    }
}

RECOMP_HOOK_RETURN("EnFall_Fireball_Update") void Events_AfterMeteorUpdate(void) {
    Actor* thisx = sMeteorUpdating;
    TagData* t;

    sMeteorUpdating = NULL;
    if (thisx == NULL) {
        return;
    }
    t = Tag_Get(thisx);
    if ((t == NULL) || t->dead || (t->drawFunc == 0)) {
        return;
    }
    thisx->draw = (ActorFunc)t->drawFunc;
    Actor_SetScale(thisx, MeteorScale());
    thisx->shape.rot.x += 0x300;
    EnFall_Fireball_SetPerVertexAlpha(1.0f);
}

static s32 Ev_IsMoonType(Actor* actor) {
    s32 type = EN_FALL_TYPE(actor);

    return (type == EN_FALL_TYPE_TERMINA_FIELD_MOON) || (type == EN_FALL_TYPE_CLOCK_TOWER_MOON) ||
           (type == EN_FALL_TYPE_LODMOON) || (type == EN_FALL_TYPE_LODMOON_NO_LERP) ||
           (type == EN_FALL_TYPE_LODMOON_INVERTED_STONE_TOWER);
}

// Red Light, Green Light: the moon's eyes glow while it's watching.
static void Ev_UpdateMoonGlow(Actor* thisx) {
    EnFall* moon = (EnFall*)thisx;
    TagData* t = Tag_Get(thisx);

    if ((t == NULL) || !Ev_IsMoonType(thisx)) {
        return;
    }
    if (EV_RUNNING(EV_REDLIGHT) || EV_RUNNING(EV_GLARE)) {
        if (!t->glowSaved) {
            t->savedGlow = moon->eyeGlowIntensity;
            t->glowSaved = true;
        }
        moon->eyeGlowIntensity = MAX(t->savedGlow, RL_MoonGlow());
    } else if (t->glowSaved) {
        moon->eyeGlowIntensity = t->savedGlow;
        t->glowSaved = false;
    }
}

// The moon swoops in during Moonfall.
static Actor* sMoonUpdating = NULL;
static PlayState* sMoonPlay = NULL;

RECOMP_HOOK("EnFall_Update") void Events_BeforeMoonUpdate(Actor* thisx, PlayState* play) {
    sMoonUpdating = thisx;
    sMoonPlay = play;
}

RECOMP_HOOK_RETURN("EnFall_Update") void Events_AfterMoonUpdate(void) {
    static const f32 sTargetDist[INTENSITY_MAX] = { 3200.0f, 2400.0f, 1700.0f };
    Actor* thisx = sMoonUpdating;
    PlayState* play = sMoonPlay;
    Player* player;
    TagData* t;
    s32 type;
    f32 f;
    f32 dx;
    f32 dz;
    f32 len;
    Vec3f target;

    sMoonUpdating = NULL;
    if ((thisx != NULL) && (thisx->update != NULL)) {
        Ev_UpdateMoonGlow(thisx);
    }
    if ((thisx == NULL) || (play == NULL) || !Ev_IsOn(EV_MOONFALL) || !gOpt[ID_MF_SWOOP] ||
        (thisx->draw == NULL) || (thisx->update == NULL)) {
        return;
    }
    type = EN_FALL_TYPE(thisx);
    if (!((type == EN_FALL_TYPE_TERMINA_FIELD_MOON) || (type == EN_FALL_TYPE_CLOCK_TOWER_MOON) ||
          (type == EN_FALL_TYPE_LODMOON) || (type == EN_FALL_TYPE_LODMOON_NO_LERP) ||
          (type == EN_FALL_TYPE_LODMOON_INVERTED_STONE_TOWER))) {
        return;
    }
    player = GET_PLAYER(play);
    t = Tag_Get(thisx);
    if ((player == NULL) || (t == NULL)) {
        return;
    }
    if (!t->origSaved) {
        t->origPos = thisx->world.pos;
        t->origSaved = true;
    }

    // Swoop in over 8 seconds, back out over the last 5.
    f = MIN(sEventFrames / (8.0f * FPS), sFramesLeft / (5.0f * FPS));
    f = CLAMP(f, 0.0f, 1.0f);
    f = f * f * (3.0f - 2.0f * f);

    dx = t->origPos.x - player->actor.world.pos.x;
    dz = t->origPos.z - player->actor.world.pos.z;
    len = sqrtf(SQ(dx) + SQ(dz));
    if (len < 1.0f) {
        dx = 0.0f;
        dz = 1.0f;
        len = 1.0f;
    }
    target.x = player->actor.world.pos.x + (dx / len) * sTargetDist[Intensity()];
    target.y = player->actor.world.pos.y + 1500.0f;
    target.z = player->actor.world.pos.z + (dz / len) * sTargetDist[Intensity()];

    thisx->world.pos.x = t->origPos.x + (target.x - t->origPos.x) * f;
    thisx->world.pos.y = thisx->world.pos.y + (target.y - thisx->world.pos.y) * f;
    thisx->world.pos.z = t->origPos.z + (target.z - t->origPos.z) * f;
}

// Blood Moon: tint the moon red. The moon is lit by the lights set up right
// before its draw function runs, so swapping in red lights recolors it.
static void Ev_SetRedMoonLights(PlayState* play) {
    static const Lights1 sRedMoonLights = gdSPDefLights1(190, 25, 15, 70, 10, 5, 0x49, 0x49, 0x49);
    Lights1* lights = GRAPH_ALLOC(play->state.gfxCtx, sizeof(Lights1));

    *lights = sRedMoonLights;
    OPEN_DISPS(play->state.gfxCtx);
    gSPSetLights1(POLY_OPA_DISP++, (*lights));
    CLOSE_DISPS(play->state.gfxCtx);
}

// The distant moon used by most areas.
RECOMP_HOOK("EnFall_LodMoon_Draw") void Events_BeforeLodMoonDraw(Actor* thisx, PlayState* play) {
    if (EV_RUNNING(EV_BLOOD_MOON)) {
        Ev_SetRedMoonLights(play);
    }
}

static PlayState* sMoonTintPlay = NULL;

// The full-detail moon in Termina Field.
RECOMP_HOOK("EnFall_Moon_Draw") void Events_BeforeMoonDraw(Actor* thisx, PlayState* play) {
    sMoonTintPlay = NULL;
    if (!EV_RUNNING(EV_BLOOD_MOON)) {
        return;
    }
    Ev_SetRedMoonLights(play);
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, 200, 0, 0, 255);
    gSPFogPosition(POLY_OPA_DISP++, 0, 1250);
    CLOSE_DISPS(play->state.gfxCtx);
    sMoonTintPlay = play;
}

RECOMP_HOOK_RETURN("EnFall_Moon_Draw") void Events_AfterMoonDraw(void) {
    PlayState* play = sMoonTintPlay;

    if (play == NULL) {
        return;
    }
    sMoonTintPlay = NULL;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}

// --- Moon Pouch explosions: enemies only -----------------------------------------

// They use the player's hit type so they can hurt enemies, which would also let
// them break pots, grass and rocks (real item drops), hit switches and open
// bombable walls (flags). Right before hits are worked out, anything that isn't
// an enemy, a boss or Link sits this frame out if one of them is going off nearby.
RECOMP_HOOK("CollisionCheck_AT") void Events_BeforeCollisionAT(PlayState* play, CollisionCheckContext* colChkCtx) {
    Vec3f spots[8];
    s32 n = 0;
    s32 i;
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_EXPLOSIVES].first;

    while ((actor != NULL) && (n < 8)) {
        if ((actor->update != NULL) && (actor->id == ACTOR_EN_BOM) && (Tag_Of(actor) == TAG_ENEMYBOMB)) {
            spots[n++] = actor->world.pos;
        }
        actor = actor->next;
    }
    if (n == 0) {
        return;
    }
    for (i = 0; i < colChkCtx->colACCount; i++) {
        Collider* col = colChkCtx->colAC[i];
        Actor* owner;
        s32 k;

        if (col == NULL) {
            continue;
        }
        owner = col->actor;
        if ((owner != NULL) && ((owner->category == ACTORCAT_ENEMY) || (owner->category == ACTORCAT_BOSS) ||
                                (owner->category == ACTORCAT_PLAYER))) {
            continue;
        }
        if (owner == NULL) {
            colChkCtx->colAC[i] = NULL;
            continue;
        }
        for (k = 0; k < n; k++) {
            if (Math_Vec3f_DistXYZ(&owner->world.pos, &spots[k]) < 400.0f) {
                colChkCtx->colAC[i] = NULL;
                break;
            }
        }
    }
}

// --- Link-only bombs -------------------------------------------------------------

RECOMP_HOOK("EnBom_Update") void Events_BeforeBombUpdate(Actor* thisx, PlayState* play) {
    static const u8 sDamage[INTENSITY_MAX] = { 0x08, 0x10, 0x20 };
    EnBom* bomb = (EnBom*)thisx;

    if (Tag_Of(thisx) == TAG_ENEMYBOMB) {
        // Moon Pouch explosions: enemies only.
        bomb->collider2.base.atFlags &= ~(AT_TYPE_ENEMY | AT_TYPE_OTHER);
        bomb->collider2.base.atFlags |= AT_TYPE_PLAYER;
        return;
    }
    if (Tag_Of(thisx) != TAG_LINKBOMB) {
        return;
    }
    // "Enemy attack" hitbox type: only Link can be hit by it. No enemies,
    // pots, grass, rocks or chain reactions.
    bomb->collider2.base.atFlags &= ~(AT_TYPE_PLAYER | AT_TYPE_OTHER);
    bomb->collider2.base.atFlags |= AT_TYPE_ENEMY;
    bomb->collider2Elements[0].base.atDmgInfo.damage = sDamage[Intensity()];
}

// ===========================================================================
// Second wave: Nemesis, Phantom Army, Treasure Hunt, Blood Hunger,
// Searchlights, Terrible Fate, Red Light Green Light and Echo.
// ===========================================================================

static s32 OptLvl(s32 id) {
    if (id == ID_EV_INTENSITY) {
        return Intensity();
    }
    return gOpt[id] % LVL_MAX;
}

static s32 OptStep(s32 id) {
    return gOpt[id] % STEP_MAX;
}

static s32 Ev_IsTaggedLive(PlayState* play, Actor* actor, u8 tag) {
    return (actor != NULL) && Ev_IsLiveActor(play, actor) && (Tag_Of(actor) == tag);
}

// Our living spawns that fight (Wolfos count as props in this game).
static s32 Ev_CountOurEnemies(PlayState* play) {
    return Ev_CountTagged(play, ACTORCAT_ENEMY, TAG_EVENT) + Ev_CountTagged(play, ACTORCAT_PROP, TAG_EVENT);
}

// Spawns an actor that we move and animate ourselves; its own logic never runs.
static Actor* Ev_SpawnPuppet(PlayState* play, s16 actorId, s16 objectId, Vec3f* pos, s16 yaw, s32 params, u8 tag) {
    u32 savedClear = play->actorCtx.sceneFlags.clearedRoom;
    Actor* actor;

    // A cleared room refuses new enemies. Puppets never fight, so let them in.
    play->actorCtx.sceneFlags.clearedRoom = 0;
    actor = Ev_Spawn(play, actorId, objectId, pos, 0, yaw, 0, params, tag);
    play->actorCtx.sceneFlags.clearedRoom = savedClear;

    if (actor != NULL) {
        actor->flags |= ACTOR_FLAG_DRAW_CULLING_DISABLED;
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }
    return actor;
}

// Advances a puppet's animation (the model data has to be selected first).
static void Puppet_Animate(PlayState* play, Actor* actor, SkelAnime* skelAnime) {
    uintptr_t saved = gSegments[6];

    Actor_SetObjectDependency(play, actor);
    SkelAnime_Update(skelAnime);
    gSegments[6] = saved;
}

static f32 Ev_DistXZ(Vec3f* a, Vec3f* b) {
    return sqrtf(SQ(a->x - b->x) + SQ(a->z - b->z));
}

// ---------------------------------------------------------------------------
// Freezing Link in place for a moment (Treasure Hunt penalty, Terrible Fate grab)
// ---------------------------------------------------------------------------

void DD_BeforeHealthChange(void);
void DD_AfterHealthChange(void);
static s32 sFateGrab; // (set up with the Terrible Fate code below)
static s32 sFreezeFrames = 0;
static s32 sFreezeRelease = 0; // frames after a freeze where we make sure Link really let go

static void Freeze_Stop(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (sFreezeFrames <= 0) {
        return;
    }
    sFreezeFrames = 0;
    sFreezeRelease = 10;
    if (player != NULL) {
        Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
        EffectSsIcePiece_SpawnBurst(play, &player->actor.world.pos, player->actor.scale.x);
        Audio_PlaySfx(NA_SE_PL_ICE_BROKEN);
    }
}

static void Freeze_Start(PlayState* play, s32 frames) {
    Player* player = GET_PLAYER(play);

    if (player == NULL) {
        return;
    }
    // A zero-length hold would never be let go: treat it as "unfreeze".
    frames = MIN(frames, 30 * FPS);
    if (frames <= 0) {
        Freeze_Stop(play);
        return;
    }
    sFreezeFrames = frames;
    Player_SetCsAction(play, NULL, PLAYER_CSACTION_WAIT);
    Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_BLUE, 255, COLORFILTER_BUFFLAG_OPA, frames);
    Audio_PlaySfx(NA_SE_PL_FREEZE_S);
}

static void Freeze_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (sFreezeFrames <= 0) {
        // Just let go: if Link is still held (the release didn't take, say mid-air),
        // keep asking for a few frames. Only when nothing else is holding him.
        if (sFreezeRelease > 0) {
            sFreezeRelease--;
            if ((player != NULL) && (player->csAction == PLAYER_CSACTION_WAIT) &&
                (play->csCtx.state == CS_STATE_IDLE) && !gMenuOpen && (sFateGrab < 0)) {
                Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
            }
        }
        return;
    }
    if (--sFreezeFrames == 0) {
        sFreezeFrames = 1; // (Freeze_Stop does the letting go)
        Freeze_Stop(play);
    }
}

// ---------------------------------------------------------------------------
// Nemesis: one powered-up enemy that hunts you. No time limit; the event ends
// when it dies. It follows you to new areas with the health it has left.
// ---------------------------------------------------------------------------

// Indexes into sPool.
static const u8 sNemRegular[] = { 3, 4, 5, 6, 9, 13, 19, 22, 24, 28 };
static const u8 sNemMiniboss[] = { 30, 31, 32 };

static s32 sNemPick = -1;
static Actor* sNemActor = NULL;
static s32 sNemHealth = 0;
static s32 sNemMaxHealth = 1;
static s32 sNemStarted = false;
static s32 sNemZeroFrames = 0;
static s32 sNemRespawnTimer = 0;
static s32 sNemFarFrames = 0;
static s32 sNemWaitFrames = 0; // Champion Duel: how long we've failed to bring the champion in
static void Duel_MarkHolds(PlayState* play);

static f32 Nemesis_Size(void) {
    static const f32 sSizes[LVL_MAX] = { 1.0f, 1.4f, 1.8f };

    return sSizes[OptLvl(ID_NEM_SIZE)];
}

static s32 Nemesis_Pick(void) {
    s32 type = gOpt[ID_NEM_TYPE] % NEMTYPE_MAX;
    s32 total;
    s32 r;

    if (type == NEMTYPE_REGULAR) {
        return sNemRegular[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sNemRegular)) % ARRAY_COUNT(sNemRegular)];
    }
    if (type == NEMTYPE_MINIBOSS) {
        return sNemMiniboss[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sNemMiniboss)) % ARRAY_COUNT(sNemMiniboss)];
    }
    total = ARRAY_COUNT(sNemRegular) + ARRAY_COUNT(sNemMiniboss);
    r = (s32)(Rand_ZeroOne() * total) % total;
    return (r < (s32)ARRAY_COUNT(sNemRegular)) ? sNemRegular[r] : sNemMiniboss[r - ARRAY_COUNT(sNemRegular)];
}

// The Nemesis is drawn bigger and glowing red. This is done around the game's
// generic actor draw so the enemy's own code is never touched (some enemies,
// like Snappers, check which draw function they have).
static Actor* sNemDrawing = NULL;
static PlayState* sNemDrawPlay = NULL;
static Vec3f sNemSavedScale;
static s32 sNemGlowing = false;

RECOMP_HOOK("Actor_Draw") void Events_BeforeActorDraw(PlayState* play, Actor* actor) {
    f32 size = 1.0f;
    f32 expMult;
    u8 r;
    u8 g;
    u8 b;
    s32 fogFar;
    u8 tag;

    sNemDrawing = NULL;
    if (actor == NULL) {
        return;
    }
    tag = Tag_Of(actor);
    expMult = Exp_DrawScale(actor);
    if ((tag == TAG_NEMV2) || (tag == TAG_BOUNTY)) {
        // 2.0: your Nemesis pulses red, bounty targets glow gold.
        f32 w = (Math_SinS((s16)(play->gameplayFrames * 0x0C00)) + 1.0f) * 0.5f;

        r = (tag == TAG_NEMV2) ? 150 + (s32)(w * 100.0f) : 200 + (s32)(w * 55.0f);
        g = (tag == TAG_NEMV2) ? 0 : 150 + (s32)(w * 60.0f);
        b = 0;
        fogFar = 1700;
        sNemGlowing = true;
    } else if ((sActive < 0) && (expMult != 1.0f)) {
        r = g = b = 0; // (an Experimental friend resized it: no glow)
        fogFar = 1000;
        sNemGlowing = false;
    } else if (sActive < 0) {
        return;
    } else if (EV_RUNNING(EV_NEMESIS) && (actor == sNemActor) && (tag == TAG_NEMESIS)) {
        // Bigger, pulsing red.
        size = Nemesis_Size();
        r = 170 + (s32)(Math_SinS((s16)(play->gameplayFrames * 0x0C00)) * 60.0f);
        g = 0;
        b = 0;
        fogFar = 1600;
        sNemGlowing = gOpt[ID_NEM_GLOW];
    } else if (EV_RUNNING(EV_MIRAGE) && ((tag == TAG_MIRAGE_REAL) || (tag == TAG_MIRAGE_FAKE))) {
        // Real and fake shimmer alike, each at its own pace.
        f32 w = (Math_SinS((s16)(play->gameplayFrames * 0x0900 + (s32)((uintptr_t)actor >> 3) * 0x3D1)) + 1.0f) * 0.5f;

        r = 40 + (s32)(w * 90.0f);
        g = 150 + (s32)(w * 80.0f);
        b = 255;
        fogFar = 1900;
        sNemGlowing = true;
    } else if (CurseTag_Marked(play, actor)) {
        // Cursed Tag: whoever has the curse pulses purple.
        r = 140 + (s32)(Math_SinS((s16)(play->gameplayFrames * 0x1000)) * 70.0f);
        g = 0;
        b = 210;
        fogFar = 1700;
        sNemGlowing = true;
    } else if (expMult != 1.0f) {
        r = g = b = 0;
        fogFar = 1000;
        sNemGlowing = false;
    } else {
        return;
    }
    size *= expMult;
    sNemDrawing = actor;
    sNemDrawPlay = play;
    sNemSavedScale = actor->scale;
    actor->scale.x *= size;
    actor->scale.y *= size;
    actor->scale.z *= size;

    // Leave the game's own hit flash alone.
    sNemGlowing = sNemGlowing && (actor->colorFilterTimer == 0);
    if (sNemGlowing) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetFogColor(POLY_OPA_DISP++, r, g, b, 255);
        gSPFogPosition(POLY_OPA_DISP++, 0, fogFar);
        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetFogColor(POLY_XLU_DISP++, r, g, b, 255);
        gSPFogPosition(POLY_XLU_DISP++, 0, fogFar);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

RECOMP_HOOK_RETURN("Actor_Draw") void Events_AfterActorDraw(void) {
    Actor* actor = sNemDrawing;
    PlayState* play = sNemDrawPlay;

    if (actor == NULL) {
        return;
    }
    sNemDrawing = NULL;
    actor->scale = sNemSavedScale;
    if (sNemGlowing && (play != NULL)) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
        gDPPipeSync(POLY_XLU_DISP++);
        POLY_XLU_DISP = Play_SetFog(play, POLY_XLU_DISP);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

static void Nemesis_Spawn(PlayState* play) {
    static const u8 sHealthMult[INTENSITY_MAX] = { 2, 3, 5 };
    Player* player = GET_PLAYER(play);
    const SpawnDef* def;
    Actor* actor;
    Vec3f spot;

    if ((sNemPick < 0) || (player == NULL)) {
        return;
    }
    // The game won't place enemies in a room that's already been cleared.
    if (Flags_GetClear(play, play->roomCtx.curRoom.num)) {
        return;
    }
    if (EV_RUNNING(EV_CHAMPION)) {
        // The champion steps up in front of you.
        if (!Ev_FindSpot(play, 250.0f, 400.0f, player->actor.shape.rot.y, 0x3000, false, &spot) &&
            !Ev_FindSpot(play, 250.0f, 500.0f, 0, 0x7FFF, false, &spot)) {
            return;
        }
    } else if (!Ev_FindSpot(play, 350.0f, 650.0f, player->actor.shape.rot.y, 0x7FFF, false, &spot)) {
        return;
    }
    def = &sPool[sNemPick];
    spot.y += def->yOffset;
    actor = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                     def->rotZ, def->params, TAG_NEMESIS);
    if (actor == NULL) {
        return;
    }
    Ev_AfterSpawn(play, actor);

    if (!sNemStarted) {
        s32 base = (actor->colChkInfo.health > 0) ? actor->colChkInfo.health : 1;

        if (EV_RUNNING(EV_CHAMPION)) {
            static const f32 sChampMult[LVL_MAX] = { 1.0f, 1.5f, 2.0f };

            sNemMaxHealth = CLAMP((s32)(base * sChampMult[OptLvl(ID_CD_HEALTH)] + 0.5f), 1, 255);
        } else {
            sNemMaxHealth = CLAMP(base * sHealthMult[Intensity()], 1, 255);
        }
        sNemHealth = sNemMaxHealth;
        sNemStarted = true;
    }
    actor->colChkInfo.health = sNemHealth;

    sNemActor = actor;
    sNemZeroFrames = 0;
    sNemFarFrames = 0;
    sNemWaitFrames = 0;
    if (EV_RUNNING(EV_CHAMPION)) {
        Duel_MarkHolds(play);
    }
    Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
}

static void Nemesis_Defeated(PlayState* play) {
    Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    Menu_ShowToast(EV_RUNNING(EV_CHAMPION) ? "You beat the champion!" : "The Nemesis has fallen!");
    sNemActor = NULL;
    Ev_End(play);
}

static void Tick_Nemesis(PlayState* play) {
    Actor* actor = sNemActor;
    Player* player = GET_PLAYER(play);

    if ((actor != NULL) && !Ev_IsTaggedLive(play, actor, TAG_NEMESIS)) {
        // Gone: beaten, or removed because the area changed.
        sNemActor = NULL;
        actor = NULL;
        if (sNemStarted && (sNemHealth <= 0)) {
            Nemesis_Defeated(play);
            return;
        }
        sNemRespawnTimer = 3 * FPS;
    }

    if (actor == NULL) {
        // A champion that can't show up here calls off the duel.
        if (EV_RUNNING(EV_CHAMPION) && (++sNemWaitFrames > 15 * FPS)) {
            Menu_ShowToast("The champion never showed.");
            Ev_Abort(play); // nothing to fight: no reward either
            return;
        }
        if (sNemRespawnTimer > 0) {
            sNemRespawnTimer--;
            return;
        }
        Nemesis_Spawn(play);
        if (sNemActor == NULL) {
            sNemRespawnTimer = FPS; // try again shortly
        }
        return;
    }

    sNemHealth = actor->colChkInfo.health;
    if (sNemHealth == 0) {
        // Give its death animation a moment.
        if (++sNemZeroFrames >= 2 * FPS) {
            Nemesis_Defeated(play);
        }
        return;
    }
    sNemZeroFrames = 0;

    // It hunts you: if it loses track of you, it catches up.
    if ((player != NULL) && (actor->xzDistToPlayer > 1500.0f)) {
        if (++sNemFarFrames > 4 * FPS) {
            Vec3f spot;

            if (Ev_FindSpot(play, 400.0f, 700.0f, 0, 0x7FFF, false, &spot)) {
                spot.y += sPool[sNemPick].yOffset;
                actor->world.pos = spot;
                actor->prevPos = spot;
            }
            sNemFarFrames = 0;
        }
    } else {
        sNemFarFrames = 0;
    }
}

// ---------------------------------------------------------------------------
// Phantom Army: enemies only the Lens of Truth can see. Without the Lens they
// shimmer into view every few seconds.
// ---------------------------------------------------------------------------

static const u8 sPhantomStalchild[] = { 5 };
static const u8 sPhantomMixed[] = { 5, 5, 3, 4, 32 }; // Stalchildren, Wolfos, White Wolfos, Dinolfos

static s32 Phantom_Size(void) {
    static const u8 sSizes[LVL_MAX] = { 6, 10, 16 };

    return sSizes[OptLvl(ID_PH_SIZE)];
}

static void Phantom_SetVisibility(PlayState* play, s32 visible) {
    static const u8 sCats[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            if ((actor->update != NULL) && (Tag_Of(actor) == TAG_EVENT)) {
                if (visible) {
                    actor->flags &= ~ACTOR_FLAG_REACT_TO_LENS;
                } else {
                    // The game then only draws it inside the Lens of Truth's view.
                    actor->flags |= ACTOR_FLAG_REACT_TO_LENS;
                }
            }
            actor = actor->next;
        }
    }
}

static void Tick_Phantom(PlayState* play) {
    static const u8 sShimmerPeriod[LVL_MAX] = { 40, 80, 120 };
    s32 hasLens = (INV_CONTENT(ITEM_LENS_OF_TRUTH) == ITEM_LENS_OF_TRUTH);
    s32 phase = sEventFrames % sShimmerPeriod[OptLvl(ID_PH_SHIMMER)];
    s32 shimmer = !hasLens && ((phase < 3) || ((phase >= 6) && (phase < 8)));

    if ((sSpawnsLeft > 0) && (sSpawnFailFrames < 90) && ((sEventFrames % 2) == 0)) {
        const u8* list = (gOpt[ID_PH_SOLDIERS] % SOLDIERS_MAX == SOLDIERS_MIXED) ? sPhantomMixed : sPhantomStalchild;
        s32 count = (list == sPhantomMixed) ? ARRAY_COUNT(sPhantomMixed) : ARRAY_COUNT(sPhantomStalchild);
        const SpawnDef* def = &sPool[list[(s32)(Rand_ZeroOne() * count) % count]];

        Ev_SpawnDef(play, def, def->params, 300.0f, 700.0f);
    }

    // The army keeps marching in.
    if ((sEventFrames % (8 * FPS)) == 0) {
        s32 alive = Ev_CountOurEnemies(play);

        if (alive < Phantom_Size()) {
            sSpawnsLeft = Phantom_Size() - alive;
            sSpawnFailFrames = 0;
        }
    }

    Phantom_SetVisibility(play, shimmer);
}

// ---------------------------------------------------------------------------
// Treasure Hunt: find the treasure before time runs out. Miss it and Link is
// frozen for a moment.
// ---------------------------------------------------------------------------

static Vec3f sTreasurePos;
static s32 sTreasureValid = false;
static s32 sTreasureBeep = 0;
static f32 sTreasureStartDist = 1000.0f;
static LightNode* sTreasureLight = NULL;
static LightInfo sTreasureLightInfo;

static s32 Treasure_TimeFrames(void) {
    static const u8 sSeconds[STEP_MAX] = { 30, 45, 60, 90 };

    return sSeconds[OptStep(ID_TH_TIME)] * FPS;
}

// A floor point near (x, z) at roughly Link's level, not under water.
static s32 Treasure_Ground(PlayState* play, f32 x, f32 startY, f32 z, f32 linkY, Vec3f* out) {
    Vec3f probe;
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY;
    f32 waterY;
    WaterBox* waterBox;

    probe.x = x;
    probe.y = startY;
    probe.z = z;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if ((floorY <= BGCHECK_Y_MIN) || (floorY < linkY - 350.0f) || (floorY > linkY + 250.0f)) {
        return false;
    }
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) && (waterY > floorY + 20.0f)) {
        return false;
    }
    out->x = x;
    out->y = floorY;
    out->z = z;
    return true;
}

static s32 Treasure_Place(PlayState* play) {
    static const f32 sMin[LVL_MAX] = { 350.0f, 700.0f, 1200.0f };
    static const f32 sMax[LVL_MAX] = { 800.0f, 1400.0f, 2200.0f };
    static const u8 sCats[] = { ACTORCAT_NPC, ACTORCAT_PROP, ACTORCAT_MISC };
    Player* player = GET_PLAYER(play);
    Vec3f link;
    Vec3f candidates[24];
    s32 count = 0;
    f32 minDist = sMin[Intensity()];
    f32 maxDist = sMax[Intensity()];
    s32 c;
    s32 pass;

    if (player == NULL) {
        return false;
    }
    link = player->actor.world.pos;

    // Things the level designers placed (signs, trees, pots, people) are
    // usually somewhere you can walk to, so hide it next to one of them.
    for (c = 0; (c < ARRAY_COUNT(sCats)) && (count < ARRAY_COUNT(candidates)); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while ((actor != NULL) && (count < ARRAY_COUNT(candidates))) {
            f32 d = Ev_DistXZ(&actor->world.pos, &link);

            if ((actor->update != NULL) && !Tag_IsOurs(Tag_Of(actor)) && (d >= minDist) && (d <= maxDist)) {
                s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &link);
                f32 x = actor->world.pos.x + Math_SinS(yaw) * 40.0f;
                f32 z = actor->world.pos.z + Math_CosS(yaw) * 40.0f;

                if (Treasure_Ground(play, x, actor->world.pos.y + 60.0f, z, link.y, &candidates[count])) {
                    count++;
                }
            }
            actor = actor->next;
        }
    }

    if (count > 0) {
        sTreasurePos = candidates[(s32)(Rand_ZeroOne() * count) % count];
    } else {
        // Otherwise any open ground at the right distance, getting closer if needed.
        s32 found = false;

        for (pass = 0; (pass < 3) && !found; pass++) {
            f32 scale = (pass == 0) ? 1.0f : ((pass == 1) ? 0.6f : 0.35f);
            s32 tries;

            for (tries = 0; (tries < 16) && !found; tries++) {
                s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
                f32 d = (minDist + Rand_ZeroOne() * (maxDist - minDist)) * scale;

                found = Treasure_Ground(play, link.x + Math_SinS(yaw) * d, link.y + 250.0f,
                                        link.z + Math_CosS(yaw) * d, link.y, &sTreasurePos);
            }
        }
        if (!found) {
            return false;
        }
    }

    sTreasureStartDist = MAX(Math_Vec3f_DistXYZ(&sTreasurePos, &link), 300.0f);
    sTreasureValid = true;
    sTreasureBeep = 0;

    // A golden glow that can be seen from a distance.
    if (sTreasureLight == NULL) {
        sTreasureLight = LightContext_InsertLight(play, &play->lightCtx, &sTreasureLightInfo);
    }
    Lights_PointGlowSetInfo(&sTreasureLightInfo, sTreasurePos.x, sTreasurePos.y + 45.0f, sTreasurePos.z, 255, 210, 60,
                            320);
    return true;
}

// V3 Friend Link: a friend hides it somewhere else (not right under Link's feet).
static s32 Treasure_PlaceAt(PlayState* play, f32 x, f32 z) {
    Player* player = GET_PLAYER(play);
    Vec3f p;

    if (!sTreasureValid || (player == NULL) ||
        !Treasure_Ground(play, x, player->actor.world.pos.y + 400.0f, z, player->actor.world.pos.y, &p) ||
        (Ev_DistXZ(&p, &player->actor.world.pos) < 250.0f)) {
        return false;
    }
    sTreasurePos = p;
    sTreasureStartDist = MAX(Math_Vec3f_DistXYZ(&sTreasurePos, &player->actor.world.pos), 300.0f);
    Lights_PointGlowSetInfo(&sTreasureLightInfo, sTreasurePos.x, sTreasurePos.y + 45.0f, sTreasurePos.z, 255, 210, 60,
                            320);
    return true;
}

static void Treasure_RemoveLight(PlayState* play) {
    if (sTreasureLight != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sTreasureLight);
        sTreasureLight = NULL;
    }
}

static void Tick_Treasure(PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 dist;

    if (!sTreasureValid || (player == NULL)) {
        return;
    }

    if ((sEventFrames % 4) == 0) {
        static Color_RGBA8 sPrim = { 255, 255, 180, 255 };
        static Color_RGBA8 sEnv = { 255, 190, 0, 0 };
        Vec3f pos;
        Vec3f vel = { 0.0f, 1.5f, 0.0f };
        Vec3f accel = { 0.0f, 0.05f, 0.0f };

        pos.x = sTreasurePos.x + Rand_CenteredFloat(40.0f);
        pos.y = sTreasurePos.y + 20.0f + Rand_ZeroOne() * 40.0f;
        pos.z = sTreasurePos.z + Rand_CenteredFloat(40.0f);
        EffectSsKirakira_SpawnSmall(play, &pos, &vel, &accel, &sPrim, &sEnv);
    }

    dist = Math_Vec3f_DistXYZ(&player->actor.world.pos, &sTreasurePos);

    // Radar beeps that speed up as you get close.
    if (dist < 700.0f) {
        if (sTreasureBeep > 0) {
            sTreasureBeep--;
        } else {
            Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
            sTreasureBeep = 4 + (s32)(dist / 700.0f * 26.0f);
        }
    }

    if ((Ev_DistXZ(&player->actor.world.pos, &sTreasurePos) < 45.0f) &&
        (fabsf(player->actor.world.pos.y - sTreasurePos.y) < 90.0f)) {
        sTreasureValid = false;
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
        Menu_ShowToast("Treasure found!");
        Ev_End(play);
    }
}

// ---------------------------------------------------------------------------
// Blood Hunger: health drains (never below 1 heart), enemies keep coming,
// every kill heals.
// ---------------------------------------------------------------------------

static const u8 sHungerFodder[] = { 0, 5, 8, 3, 14, 9, 29 }; // Keese, Stalchild, Chuchu, Wolfos, Guay, Tektite, Leever
static s32 sHungerKills = 0;
static s32 sHungerDrain = 0;

static s32 Hunger_MaxEnemies(void) {
    static const u8 sCounts[LVL_MAX] = { 4, 6, 8 };

    return sCounts[OptLvl(ID_BH_ENEMIES)];
}

static void Tick_Hunger(PlayState* play) {
    static const u8 sDrainEvery[INTENSITY_MAX] = { 60, 40, 20 };
    static const u8 sCats[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    s32 health = gSaveContext.save.saveInfo.playerData.health;
    s32 c;

    // Drain a quarter heart at a time, stopping at one heart.
    if (++sHungerDrain >= sDrainEvery[Intensity()]) {
        s32 loss = MIN(4, health - 0x10);

        sHungerDrain = 0;
        if (loss > 0) {
            // Set directly: the randomizer multiplies (or one-hit-KOs) anything
            // that goes through the game's damage function, and the drain must
            // never be what kills you.
            gSaveContext.save.saveInfo.playerData.health = health - loss;
        }
    }

    // Kills heal you.
    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            TagData* t = Tag_Get(actor);

            if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_EVENT)) {
                if (actor->colChkInfo.health > 0) {
                    t->flags |= TAGF_WAS_ALIVE;
                } else if ((t->flags & TAGF_WAS_ALIVE) && !(t->flags & TAGF_REWARDED)) {
                    t->flags |= TAGF_REWARDED;
                    sHungerKills++;
                    Health_ChangeBy(play, 0x10 * (OptLvl(ID_BH_HEAL) + 1));
                    Audio_PlaySfx(NA_SE_SY_HP_RECOVER);
                }
            }
            actor = actor->next;
        }
    }

    // Keep the pack topped up.
    if ((sEventFrames % (3 * FPS)) == 0) {
        s32 alive = Ev_CountOurEnemies(play);

        if (alive < Hunger_MaxEnemies()) {
            sSpawnsLeft = Hunger_MaxEnemies() - alive;
            sSpawnFailFrames = 0;
        }
    }
    if ((sSpawnsLeft > 0) && (sSpawnFailFrames < 60) && ((sEventFrames % 4) == 0)) {
        const SpawnDef* def =
            &sPool[sHungerFodder[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sHungerFodder)) % ARRAY_COUNT(sHungerFodder)]];

        Ev_SpawnDef(play, def, def->params, 250.0f, 600.0f);
    }
}

// ---------------------------------------------------------------------------
// Searchlights: sweeping spotlights in the dark. Get caught and an alarm
// calls in enemies.
// ---------------------------------------------------------------------------

#define MAX_SPOTS 6
#define CIRCLE_SHADOW_DL ((Gfx*)0x04076BC0) // gCircleShadowDL in gameplay_keep

typedef struct {
    Vec3f pos;
    CollisionPoly* floorPoly;
    f32 ampX;
    f32 ampZ;
    s16 phaseX;
    s16 phaseZ;
    s16 freqX;
    s16 freqZ;
    s32 valid;
    LightNode* light;
    LightInfo lightInfo;
} Spotlight;

static Spotlight sSpots[MAX_SPOTS];
static s32 sSpotCount = 0;
static Vec3f sSpotCenter;
static f32 sSpotTime = 0.0f;
static s32 sSpotAlert = 0;
static s32 sSpotExposure = 0; // frames Link has been standing in a beam
static s32 sSpotCooldown = 0;

static s32 Search_Count(void) {
    static const u8 sCounts[STEP_MAX] = { 2, 3, 4, 6 };

    return sCounts[OptStep(ID_SL_COUNT)];
}

static f32 Search_Radius(void) {
    static const f32 sRadius[LVL_MAX] = { 90.0f, 130.0f, 180.0f };

    return sRadius[OptLvl(ID_SL_SIZE)];
}

static s32 Search_Backup(void) {
    static const u8 sCounts[STEP_MAX] = { 2, 3, 4, 6 }; // 3.1.8: one more each time

    return sCounts[OptStep(ID_SL_BACKUP)] * (Ev_IsDoubled() ? 2 : 1);
}

static void Search_RemoveLights(PlayState* play) {
    s32 i;

    for (i = 0; i < MAX_SPOTS; i++) {
        if (sSpots[i].light != NULL) {
            LightContext_RemoveLight(play, &play->lightCtx, sSpots[i].light);
            sSpots[i].light = NULL;
        }
        sSpots[i].valid = false;
    }
    sSpotCount = 0;
}

static void Search_Setup(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 i;

    Search_RemoveLights(play);
    if (player == NULL) {
        return;
    }
    sSpotCenter = player->actor.world.pos;
    sSpotCount = Search_Count();
    for (i = 0; i < sSpotCount; i++) {
        Spotlight* spot = &sSpots[i];

        spot->ampX = 220.0f + Rand_ZeroOne() * 260.0f;
        spot->ampZ = 220.0f + Rand_ZeroOne() * 260.0f;
        spot->phaseX = (s16)(Rand_ZeroOne() * 0xFFFF);
        spot->phaseZ = (s16)(Rand_ZeroOne() * 0xFFFF);
        spot->freqX = 0x60 + (s16)(Rand_ZeroOne() * 0x50);
        spot->freqZ = 0x60 + (s16)(Rand_ZeroOne() * 0x50);
        spot->valid = false;
        spot->light = LightContext_InsertLight(play, &play->lightCtx, &spot->lightInfo);
    }
}

static void Tick_Search(PlayState* play) {
    static const f32 sSpeed[INTENSITY_MAX] = { 0.7f, 1.0f, 1.5f };
    static const u8 sCooldown[INTENSITY_MAX] = { 160, 120, 80 };
    Player* player = GET_PLAYER(play);
    f32 radius = Search_Radius();
    s32 caught = false;
    s32 i;

    if (player == NULL) {
        return;
    }
    if (sSpotAlert > 0) {
        sSpotAlert--;
    }
    if (sSpotCooldown > 0) {
        sSpotCooldown--;
    }

    // The sweep area drifts along with Link.
    sSpotCenter.x += (player->actor.world.pos.x - sSpotCenter.x) * 0.03f;
    sSpotCenter.y = player->actor.world.pos.y;
    sSpotCenter.z += (player->actor.world.pos.z - sSpotCenter.z) * 0.03f;
    sSpotTime += sSpeed[Intensity()];

    for (i = 0; i < sSpotCount; i++) {
        Spotlight* spot = &sSpots[i];
        Vec3f probe;
        s32 bgId;
        f32 floorY;

        probe.x = sSpotCenter.x + Math_SinS((s16)(spot->phaseX + (s32)(spot->freqX * sSpotTime))) * spot->ampX;
        probe.z = sSpotCenter.z + Math_SinS((s16)(spot->phaseZ + (s32)(spot->freqZ * sSpotTime))) * spot->ampZ;
        if (i == 0) {
            static f32 sBeamX;
            static f32 sBeamZ;
            static s32 sBeamHeld = false;
            f32 sx;
            f32 sz;

            // A friend drives the first beam: it glides to their mark.
            if (Steer_Get(2, &sx, &sz)) {
                f32 dx;
                f32 dz;
                f32 d;

                if (!sBeamHeld) {
                    sBeamX = probe.x;
                    sBeamZ = probe.z;
                    sBeamHeld = true;
                }
                dx = sx - sBeamX;
                dz = sz - sBeamZ;
                d = sqrtf(dx * dx + dz * dz);
                if (d > 9.0f) {
                    sBeamX += dx * 9.0f / d;
                    sBeamZ += dz * 9.0f / d;
                }
                probe.x = sBeamX;
                probe.z = sBeamZ;
            } else {
                sBeamHeld = false;
            }
        }
        probe.y = player->actor.world.pos.y + 250.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &spot->floorPoly, &bgId, &probe);

        spot->valid = (floorY > BGCHECK_Y_MIN) && (floorY > player->actor.world.pos.y - 400.0f) &&
                      (spot->floorPoly != NULL);
        spot->pos.x = probe.x;
        spot->pos.y = floorY;
        spot->pos.z = probe.z;

        if (spot->light != NULL) {
            u8 g = (sSpotAlert > 0) ? 40 : 240;
            u8 b = (sSpotAlert > 0) ? 40 : 190;

            Lights_PointNoGlowSetInfo(&spot->lightInfo, probe.x, spot->valid ? (floorY + 80.0f) : probe.y, probe.z,
                                      255, g, b, spot->valid ? (s16)(radius * 2.5f) : 0);
        }

        if (spot->valid && (Ev_DistXZ(&spot->pos, &player->actor.world.pos) < radius) &&
            (fabsf(player->actor.world.pos.y - floorY) < 120.0f)) {
            caught = true;
        }
    }

    // A short warning (with a ticking sound) before the alarm goes off.
    if (caught && (sSpotCooldown == 0)) {
        static const u8 sGrace[INTENSITY_MAX] = { 16, 12, 8 };

        if ((sSpotExposure % 6) == 0) {
            Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
        }
        if (++sSpotExposure < sGrace[Intensity()]) {
            caught = false;
        }
    } else {
        sSpotExposure = 0;
    }

    if (caught && (sSpotCooldown == 0)) {
        s32 room = 12 - Ev_CountOurEnemies(play);

        sSpotExposure = 0;
        sSpotAlert = 40;
        sSpotCooldown = sCooldown[Intensity()];
        Audio_PlaySfx(NA_SE_SY_FOUND);
        Menu_ShowToast("SPOTTED!");
        sSpawnsLeft = CLAMP(Search_Backup(), 0, MAX(room, 0));
        sSpawnFailFrames = 0;
    }

    // Reinforcements drop in around Link.
    if ((sSpawnsLeft > 0) && (sSpawnFailFrames < 40) && ((sEventFrames % 3) == 0)) {
        s32 pick = Ev_PickPoolEnemy(false);
        const SpawnDef* def = &sPool[(pick >= 0) ? pick : 5];

        sSpawnNear = true;
        Ev_SpawnDef(play, def, def->params, 150.0f, 350.0f);
        sSpawnNear = false;
    }
}

static void Search_Draw(PlayState* play) {
    f32 scale = Search_Radius() / 100.0f;
    s32 i;

    OPEN_DISPS(play->state.gfxCtx);
    for (i = 0; i < sSpotCount; i++) {
        Spotlight* spot = &sSpots[i];
        MtxF mtx;

        if (!spot->valid || (spot->floorPoly == NULL)) {
            continue;
        }
        POLY_OPA_DISP = Gfx_SetupDL(POLY_OPA_DISP, SETUPDL_44);
        // Normal depth test instead of the shadow's "decal" mode, which only shows
        // where the circle sits exactly on the ground and so vanishes up close.
        gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
        gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0,
                          COMBINED);
        if (sSpotAlert > 0) {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 40, 30, 170);
        } else {
            gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 245, 200, 150);
        }
        {
            // One big flat circle sinks into bumpy ground, so the beam is drawn
            // as seven smaller circles, each laid on the ground under it.
            f32 radius = Search_Radius();
            s32 k;

            for (k = 0; k < 7; k++) {
                CollisionPoly* poly;
                s32 bgId;
                Vec3f probe;
                f32 floorY;
                f32 sub = (k == 0) ? 0.6f : 0.55f;

                probe.x = spot->pos.x + ((k == 0) ? 0.0f : Math_SinS((s16)(k * 0x2AAA)) * radius * 0.5f);
                probe.y = spot->pos.y + 60.0f;
                probe.z = spot->pos.z + ((k == 0) ? 0.0f : Math_CosS((s16)(k * 0x2AAA)) * radius * 0.5f);
                floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                if ((floorY <= BGCHECK_Y_MIN) || (poly == NULL) || (fabsf(floorY - spot->pos.y) > 80.0f)) {
                    continue;
                }
                func_800C0094(poly, probe.x, floorY + 2.0f, probe.z, &mtx);
                Matrix_Put(&mtx);
                Matrix_Scale(scale * sub, 1.0f, scale * sub, MTXMODE_APPLY);
                MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
                gSPDisplayList(POLY_OPA_DISP++, CIRCLE_SHADOW_DL);
            }
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// Terrible Fate: the Happy Mask Salesman only moves while you aren't looking.
// If he reaches you, he grabs and shakes you.
// ---------------------------------------------------------------------------

#define MAX_FATE 3

static Actor* sFate[MAX_FATE];
static s32 sFateBlocked[MAX_FATE];
static s32 sFateGrab = -1;       // which one is holding Link
static s32 sFateGrabTimer = 0;
static s32 sFateHitDelay = 0;    // frames until the grab's damage lands
static s32 sFateCooldown = 0;
static s32 sFateBanner = 0;
static s16 sFateHitYaw = 0;

static s32 Fate_Count(void) {
    return OptLvl(ID_TF_COUNT) + 1;
}

// His own animations (in object_osn) and the matching values the draw code
// reads to pick facial expressions.
#define OSN_ARMS_OUT_ANIM ((AnimationHeader*)0x06002F74) // gHappyMaskSalesmanArmsOutAnim
#define OSN_SHAKE_ANIM ((AnimationHeader*)0x0600AE9C)    // gHappyMaskSalesmanShakeAnim
#define OSN_ANIMINDEX_ARMS_OUT 1
#define OSN_ANIMINDEX_CHOKE 11 // wide open eyes and a grin

static void Fate_SetAnim(PlayState* play, Actor* actor, AnimationHeader* anim, u8 animIndex) {
    EnOsn* osn = (EnOsn*)actor;
    uintptr_t saved = gSegments[6];

    Actor_SetObjectDependency(play, actor);
    Animation_PlayLoop(&osn->skelAnime, anim);
    gSegments[6] = saved;
    osn->animIndex = animIndex;
}

static s32 Fate_PointOnScreen(PlayState* play, Vec3f* point) {
    Vec3f proj;
    f32 w;

    SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF, point, &proj, &w);
    if (w < 1.0f) {
        return false; // behind the camera
    }
    // Generous sideways so widescreen and ultrawide edges count as "seen".
    return (fabsf(proj.x / w) < 1.8f) && (fabsf(proj.y / w) < 1.1f);
}

static s32 Fate_IsSeen(PlayState* play, Actor* actor) {
    Player* player = GET_PLAYER(play);
    Vec3f points[3];
    s32 onScreen = false;
    s32 i;

    // With the camera behind Link he's almost always on screen, so Link has to
    // be facing him too. Turn your back and he comes.
    if (player != NULL) {
        s16 diff = Math_Vec3f_Yaw(&player->actor.world.pos, &actor->world.pos) - player->actor.shape.rot.y;

        if (ABS_ALT(diff) > 0x3000) {
            return false;
        }
    }

    for (i = 0; i < 3; i++) {
        points[i] = actor->world.pos;
        points[i].y += 10.0f + i * 35.0f;
        if (Fate_PointOnScreen(play, &points[i])) {
            onScreen = true;
        }
    }
    if (!onScreen) {
        return false;
    }
    // Hidden behind a wall counts as not being looked at.
    if (gOpt[ID_TF_WALLS]) {
        Vec3f eye = play->view.eye;
        Vec3f hit;
        CollisionPoly* poly;
        s32 bgId;

        if (BgCheck_EntityLineTest1(&play->colCtx, &eye, &points[1], &hit, &poly, true, true, true, true, &bgId) &&
            BgCheck_EntityLineTest1(&play->colCtx, &eye, &points[2], &hit, &poly, true, true, true, true, &bgId)) {
            return false;
        }
    }
    return true;
}

// Puts a salesman somewhere behind the camera, out of sight.
static void Fate_Place(PlayState* play, s32 index) {
    Player* player = GET_PLAYER(play);
    Camera* cam = GET_ACTIVE_CAM(play);
    s16 behind;
    Vec3f spot;
    s32 found;

    if ((player == NULL) || (cam == NULL)) {
        return;
    }
    behind = Camera_GetCamDirYaw(cam) + 0x8000;
    found = Ev_FindSpot(play, 700.0f, 1000.0f, behind, 0x3000, false, &spot) ||
            Ev_FindSpot(play, 450.0f, 900.0f, behind, 0x6000, false, &spot) ||
            Ev_FindSpot(play, 350.0f, 900.0f, 0, 0x7FFF, false, &spot);
    if (!found) {
        return;
    }

    if (sFate[index] == NULL) {
        Actor* actor = Ev_SpawnPuppet(play, ACTOR_EN_OSN, OBJECT_OSN, &spot,
                                      Math_Vec3f_Yaw(&spot, &player->actor.world.pos), 0, TAG_FATE);

        if (actor == NULL) {
            return;
        }
        Fate_SetAnim(play, actor, OSN_ARMS_OUT_ANIM, OSN_ANIMINDEX_ARMS_OUT);
        sFate[index] = actor;
    } else {
        sFate[index]->world.pos = spot;
        sFate[index]->prevPos = spot;
    }
    sFate[index]->shape.rot.y = sFate[index]->world.rot.y = Math_Vec3f_Yaw(&spot, &player->actor.world.pos);
    sFateBlocked[index] = 0;
}

// Runs every frame, even while Link is held (the grab puts him in a cutscene state).
static void Fate_UpdateGrab(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (sFateBanner > 0) {
        sFateBanner--;
    }
    if (player == NULL) {
        return;
    }

    if (sFateHitDelay > 0) {
        static const u8 sDamage[INTENSITY_MAX] = { 0x10, 0x20, 0x40 };

        if (--sFateHitDelay == 0) {
            func_800B8D50(play, &player->actor, 10.0f, sFateHitYaw, 8.0f, sDamage[Intensity()]);
        }
    }

    if (sFateGrab < 0) {
        return;
    }

    if (Ev_IsTaggedLive(play, sFate[sFateGrab], TAG_FATE)) {
        Actor* actor = sFate[sFateGrab];
        s16 facing = player->actor.shape.rot.y;

        actor->world.pos.x = player->actor.world.pos.x + Math_SinS(facing) * 40.0f;
        actor->world.pos.y = player->actor.world.pos.y;
        actor->world.pos.z = player->actor.world.pos.z + Math_CosS(facing) * 40.0f;
        actor->prevPos = actor->world.pos;
        actor->shape.rot.y = actor->world.rot.y = facing + 0x8000;
        Puppet_Animate(play, actor, &((EnOsn*)actor)->skelAnime);
        if ((sFateGrabTimer % 10) == 0) {
            Actor_RequestQuake(play, 6, 12);
        }
    }

    if (--sFateGrabTimer <= 0) {
        s32 index = sFateGrab;

        sFateGrab = -1;
        Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
        sFateHitYaw = player->actor.shape.rot.y + 0x8000;
        sFateHitDelay = 3;
        sFateCooldown = 3 * FPS;
        if (Ev_IsTaggedLive(play, sFate[index], TAG_FATE)) {
            Fate_SetAnim(play, sFate[index], OSN_ARMS_OUT_ANIM, OSN_ANIMINDEX_ARMS_OUT);
            Fate_Place(play, index);
        }
    }
}

static void Fate_Release(PlayState* play) {
    if (sFateGrab >= 0) {
        sFateGrab = -1;
        Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
    }
}

static void Tick_Fate(PlayState* play) {
    static const f32 sSpeed[LVL_MAX] = { 6.0f, 10.0f, 15.0f };
    Player* player = GET_PLAYER(play);
    f32 speed = sSpeed[OptLvl(ID_TF_SPEED)];
    s32 i;

    if ((player == NULL) || (sFateGrab >= 0)) {
        return;
    }
    if (sFateCooldown > 0) {
        sFateCooldown--;
    }

    for (i = 0; i < MAX_FATE; i++) {
        Actor* actor;
        EnOsn* osn;
        f32 dist;

        if ((sFate[i] != NULL) && !Ev_IsTaggedLive(play, sFate[i], TAG_FATE)) {
            sFate[i] = NULL;
        }
        if (i >= Fate_Count()) {
            continue;
        }
        if (sFate[i] == NULL) {
            if ((sEventFrames % FPS) == (i * 5)) {
                Fate_Place(play, i);
            }
            continue;
        }

        actor = sFate[i];
        osn = (EnOsn*)actor;
        dist = Ev_DistXZ(&actor->world.pos, &player->actor.world.pos);

        if (dist > 1600.0f) {
            Fate_Place(play, i);
            continue;
        }

        if (!Fate_IsSeen(play, actor)) {
            // Creep closer while nobody is looking.
            if (dist > 30.0f) {
                s16 yaw = Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos);
                f32 step = MIN(speed, dist - 25.0f);
                Vec3f next = actor->world.pos;
                Vec3f probe;
                CollisionPoly* poly;
                s32 bgId;
                f32 floorY;

                next.x += Math_SinS(yaw) * step;
                next.z += Math_CosS(yaw) * step;
                probe = next;
                probe.y += 80.0f;
                floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                if ((floorY > BGCHECK_Y_MIN) && (fabsf(floorY - actor->world.pos.y) < 70.0f)) {
                    next.y = floorY;
                    actor->world.pos = next;
                    actor->prevPos = next;
                    sFateBlocked[i] = 0;
                } else if (++sFateBlocked[i] > 2 * FPS) {
                    Fate_Place(play, i);
                    continue;
                }
                actor->shape.rot.y = actor->world.rot.y = yaw;
            }
            Puppet_Animate(play, actor, &osn->skelAnime);
        }

        // Caught.
        if ((sFateCooldown == 0) && (Ev_DistXZ(&actor->world.pos, &player->actor.world.pos) < 55.0f) &&
            (fabsf(actor->world.pos.y - player->actor.world.pos.y) < 100.0f)) {
            sFateGrab = i;
            sFateGrabTimer = 50;
            Fate_SetAnim(play, actor, OSN_SHAKE_ANIM, OSN_ANIMINDEX_CHOKE);
            sFateBanner = 60;
            Player_SetCsAction(play, NULL, PLAYER_CSACTION_WAIT);
            Audio_PlaySfx(NA_SE_SY_STALKIDS_PSYCHO);
            Rumble_Request(0.0f, 255, 50, 150);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Red Light, Green Light: while the moon's eyes glow, stand still.
// ---------------------------------------------------------------------------

typedef enum { RL_GREEN, RL_WARN, RL_RED } RedLightPhase;

static s32 sRLPhase = RL_GREEN;
static s32 sRLTimer = 0;
static s32 sRLRedFrames = 0;
static s32 sRLImmune = 0;
static s32 sRLZapFlash = 0;

static s32 RL_RandomRange(s32 lo, s32 hi) {
    return lo + (s32)(Rand_ZeroOne() * (hi - lo));
}

static void RL_StartGreen(void) {
    static const u8 sLo[LVL_MAX] = { 40, 60, 100 };
    static const u8 sHi[LVL_MAX] = { 80, 120, 180 };

    sRLPhase = RL_GREEN;
    if (EV_RUNNING(EV_GLARE)) {
        // The moon looks away for a while; less on higher intensity.
        static const u8 sGlareLo[INTENSITY_MAX] = { 100, 70, 45 };
        static const u8 sGlareHi[INTENSITY_MAX] = { 160, 120, 80 };

        sRLTimer = RL_RandomRange(sGlareLo[Intensity()], sGlareHi[Intensity()]);
        return;
    }
    sRLTimer = RL_RandomRange(sLo[OptLvl(ID_RL_GREEN)], sHi[OptLvl(ID_RL_GREEN)]);
}

// How strongly the moon's eyes glow right now (0 to 1).
static f32 RL_MoonGlow(void) {
    if (!EV_RUNNING(EV_REDLIGHT) && !EV_RUNNING(EV_GLARE)) {
        return 0.0f;
    }
    if (sRLPhase == RL_RED) {
        return 1.0f;
    }
    if (sRLPhase == RL_WARN) {
        return 1.0f - (sRLTimer / 16.0f);
    }
    return 0.0f;
}

static void Tick_RedLight(PlayState* play) {
    static const u8 sGrace[LVL_MAX] = { 12, 8, 4 };
    static const u8 sRedLo[LVL_MAX] = { 30, 40, 60 };
    static const u8 sRedHi[LVL_MAX] = { 50, 80, 100 };
    static const u8 sDamage[INTENSITY_MAX] = { 0x08, 0x10, 0x20 };
    Player* player = GET_PLAYER(play);

    if (player == NULL) {
        return;
    }
    if (sRLImmune > 0) {
        sRLImmune--;
    }
    if (sRLZapFlash > 0) {
        sRLZapFlash--;
    }

    switch (sRLPhase) {
        case RL_GREEN:
            if (--sRLTimer <= 0) {
                sRLPhase = RL_WARN;
                sRLTimer = 16;
                Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
            }
            break;

        case RL_WARN:
            if (--sRLTimer <= 0) {
                sRLPhase = RL_RED;
                s32 lenId = EV_RUNNING(EV_GLARE) ? ID_MG_LENGTH : ID_RL_RED;

                sRLTimer = RL_RandomRange(sRedLo[OptLvl(lenId)], sRedHi[OptLvl(lenId)]);
                sRLRedFrames = 0;
                Audio_PlaySfx(NA_SE_EN_EYEGOLE_EYE);
            }
            break;

        case RL_RED: {
            // Moving on your own counts; being carried by a platform doesn't.
            s32 moving = (fabsf(player->speedXZ) > 1.0f) ||
                         ((player->rideActor != NULL) && (fabsf(player->rideActor->speed) > 1.0f));

            s32 reactId = EV_RUNNING(EV_GLARE) ? ID_MG_REACT : ID_RL_REACT;

            sRLRedFrames++;
            if (moving && (sRLRedFrames > sGrace[OptLvl(reactId)]) && (sRLImmune == 0)) {
                if (EV_RUNNING(EV_GLARE)) {
                    // The moon strikes you with a fireball.
                    Glare_Strike(play);
                    sRLImmune = 25;
                } else {
                    // Electric shock with knockback.
                    func_800B8D10(play, &player->actor, 6.0f, player->actor.shape.rot.y + 0x8000, 5.0f, 4,
                                  sDamage[Intensity()]);
                    sRLImmune = 30;
                }
                sRLZapFlash = 20;
            }
            if (--sRLTimer <= 0) {
                RL_StartGreen();
                Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Echo: a ghost retraces your exact path a few seconds behind you.
// ---------------------------------------------------------------------------

#define ECHO_BUF 200
#define ENPOH_SKELANIME(actor) ((SkelAnime*)((u8*)(actor) + 0x144))   // EnPoh.skelAnime
#define ENPOH_BYTE(actor, offset) ACTOR_FIELD(actor, u8, offset)     // EnPoh.unk_194..unk_19B
#define POE_FLOAT_ANIM ((AnimationHeader*)0x06000A60)                // gPoeFloatAnim in object_po

static Vec3f sEchoBuf[ECHO_BUF];
static s32 sEchoHead = 0;
static s32 sEchoCount = 0;
static Actor* sEchoGhost = NULL;
static s32 sEchoCooldown = 0;

// V3: always 2 seconds behind you.
static s32 Echo_Delay(void) {
    return 2 * FPS;
}

static Vec3f* Echo_History(s32 framesAgo) {
    return &sEchoBuf[(sEchoHead - 1 - framesAgo + 2 * ECHO_BUF) % ECHO_BUF];
}

static void Echo_Reset(void) {
    sEchoHead = 0;
    sEchoCount = 0;
}

static void Echo_SetupGhost(PlayState* play, Actor* ghost) {
    uintptr_t saved = gSegments[6];

    ghost->shape.shadowAlpha = 60;
    // Float gently instead of playing the "rising from the ground" animation.
    Actor_SetObjectDependency(play, ghost);
    Animation_PlayLoop(ENPOH_SKELANIME(ghost), POE_FLOAT_ANIM);
    gSegments[6] = saved;
}

static s32 gEchoOneShot = false; // Moon Shield doesn't block this hit
static void Echo_OneShot(PlayState* play, Player* player, s16 yaw) {
    func_800B8D50(play, &player->actor, 8.0f, yaw, 6.0f, 0);
    gEchoOneShot = true;
    Health_ChangeBy(play, -0x4000); // more than 20 hearts, even with double defense
    gEchoOneShot = false;
}

static void Tick_Echo(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 delay = Echo_Delay();
    Vec3f* target;
    Vec3f* before;
    Actor* ghost;

    if (player == NULL) {
        return;
    }
    sEchoBuf[sEchoHead] = player->actor.world.pos;
    sEchoHead = (sEchoHead + 1) % ECHO_BUF;
    if (sEchoCount < ECHO_BUF) {
        sEchoCount++;
    }
    if (sEchoCooldown > 0) {
        sEchoCooldown--;
    }

    if ((sEchoGhost != NULL) && !Ev_IsTaggedLive(play, sEchoGhost, TAG_ECHO)) {
        sEchoGhost = NULL;
    }
    if ((sEchoCount <= delay + 1) || (sEventFrames < 3 * FPS)) {
        return; // a head start: it rises 3 seconds in, 2 seconds behind you
    }

    target = Echo_History(delay);
    before = Echo_History(delay + 1);

    if (sEchoGhost == NULL) {
        Vec3f pos = *target;

        pos.y += 25.0f;
        sEchoGhost = Ev_SpawnPuppet(play, ACTOR_EN_POH, OBJECT_PO, &pos, player->actor.shape.rot.y, 0, TAG_ECHO);
        if (sEchoGhost == NULL) {
            return;
        }
        Echo_SetupGhost(play, sEchoGhost);
        Actor_PlaySfx(sEchoGhost, NA_SE_EN_PO_LAUGH);
    }

    ghost = sEchoGhost;
    ghost->world.pos = *target;
    ghost->world.pos.y += 25.0f + Math_SinS((s16)(sEventFrames * 0x800)) * 6.0f;
    ghost->prevPos = ghost->world.pos;
    if (Ev_DistXZ(target, before) > 0.5f) {
        ghost->shape.rot.y = ghost->world.rot.y = Math_Vec3f_Yaw(before, target);
    }
    // Pale and see-through, with a blue lantern.
    ENPOH_BYTE(ghost, 0x194) = 120;
    ENPOH_BYTE(ghost, 0x195) = 130;
    ENPOH_BYTE(ghost, 0x196) = 255;
    ENPOH_BYTE(ghost, 0x197) = 140;
    ENPOH_BYTE(ghost, 0x198) = 90;
    ENPOH_BYTE(ghost, 0x199) = 110;
    ENPOH_BYTE(ghost, 0x19A) = 255;
    ENPOH_BYTE(ghost, 0x19B) = 200;
    Puppet_Animate(play, ghost, ENPOH_SKELANIME(ghost));

    if ((sEventFrames % 40) == 0) {
        Actor_PlaySfx(ghost, NA_SE_EN_PO_FLY);
    }

    // Caught up with you.
    if ((sEchoCooldown == 0) && (Ev_DistXZ(&ghost->world.pos, &player->actor.world.pos) < 40.0f) &&
        (fabsf(ghost->world.pos.y - 25.0f - player->actor.world.pos.y) < 70.0f)) {
        // V3: one touch takes every heart (a fairy or Second Wind can still save you).
        Echo_OneShot(play, player, ghost->shape.rot.y);
        Audio_PlaySfx(NA_SE_EN_PO_LAUGH);
        sEchoCooldown = 2 * FPS;
        // It fades and starts following your trail again from here.
        Ev_KillOne(play, ghost);
        sEchoGhost = NULL;
        Echo_Reset();
    }
}

// ===========================================================================
// Third wave: Moon's Glare, Kamikaze Keese, Wolf Pack, Champion Duel and
// Infighting.
// ===========================================================================

// Makes sure there's a moon in the sky (outdoors only).
static void Ev_EnsureMoon(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;
    Vec3f pos;

    if ((player == NULL) || !Ev_IsOutdoors(play)) {
        return;
    }
    while (actor != NULL) {
        if ((actor->id == ACTOR_EN_FALL) && (actor->update != NULL) && Ev_IsMoonType(actor)) {
            return;
        }
        actor = actor->next;
    }
    if (Obj_Ensure(play, OBJECT_FALL) < 0) {
        return;
    }
    pos = player->actor.world.pos;
    pos.x += Math_SinS(player->actor.shape.rot.y) * 8000.0f;
    pos.y += 4000.0f;
    pos.z += Math_CosS(player->actor.shape.rot.y) * 8000.0f;
    Ev_Spawn(play, ACTOR_EN_FALL, 0, &pos, 0, 0, 0, 0, TAG_OURMOON);
}

// ---------------------------------------------------------------------------
// Moon's Glare: like Red Light Green Light, but moving while the moon glares
// brings a fireball down on you.
// ---------------------------------------------------------------------------

static void Glare_Strike(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* meteor;
    TagData* t;
    Vec3f start;

    if ((player == NULL) || (Obj_Ensure(play, OBJECT_FALL) < 0)) {
        return;
    }
    start = player->actor.world.pos;
    start.x += Rand_CenteredFloat(120.0f);
    start.y += 450.0f;
    start.z += Rand_CenteredFloat(120.0f);

    meteor = Ev_Spawn(play, ACTOR_EN_FALL, 0, &start, 0, 0, 0, EN_FALL_TYPE_CRASH_FIRE_BALL << 7, TAG_METEOR);
    if (meteor == NULL) {
        return;
    }
    meteor->velocity.x = 0.0f;
    meteor->velocity.y = -30.0f;
    meteor->velocity.z = 0.0f;
    meteor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    Actor_SetScale(meteor, MeteorScale());
    t = Tag_Get(meteor);
    if (t != NULL) {
        t->timer2 = 1; // homes in on Link (see the meteor update hook)
    }
    Audio_PlaySfx(NA_SE_EV_LIGHTNING);
}

// ---------------------------------------------------------------------------
// Kamikaze Keese: Fire Keese dive at you and blow up on contact.
// ---------------------------------------------------------------------------

static void Tick_Kamikaze(PlayState* play) {
    static const u8 sSwarm[LVL_MAX] = { 4, 7, 10 };
    static const f32 sSpeed[LVL_MAX] = { 3.0f, 4.5f, 6.5f };
    Player* player = GET_PLAYER(play);
    Actor* actor;
    s32 alive = 0;

    if (player == NULL) {
        return;
    }

    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        Actor* next = actor->next;
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_KAMIKAZE) && (actor->colChkInfo.health > 0)) {
            Vec3f target = player->actor.world.pos;
            f32 dx;
            f32 dy;
            f32 dz;
            f32 dist;

            target.y += 30.0f;
            dx = target.x - actor->world.pos.x;
            dy = target.y - actor->world.pos.y;
            dz = target.z - actor->world.pos.z;
            dist = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));
            alive++;
            if (t->timer < 0x7FFF) {
                t->timer++;
            }

            if (dist < 40.0f) {
                // Boom. Only Link gets hurt.
                Vec3f blast = actor->world.pos;

                Ev_SpawnLinkBomb(play, &blast, 1);
                Ev_KillOne(play, actor);
            } else if (t->timer > 20) {
                // A short hover after appearing, then the dive.
                f32 speed = sSpeed[OptLvl(ID_KK_SPEED)];

                actor->world.pos.x += (dx / dist) * speed;
                actor->world.pos.y += (dy / dist) * speed;
                actor->world.pos.z += (dz / dist) * speed;
                actor->world.rot.y = actor->shape.rot.y = Math_Vec3f_Yaw(&actor->world.pos, &target);
                if ((t->timer % 30) == 21) {
                    Actor_PlaySfx(actor, NA_SE_EN_FFLY_ATTACK);
                }
            }
        }
        actor = next;
    }

    // Keep them coming.
    if ((alive < sSwarm[OptLvl(ID_KK_SWARM)]) && ((sEventFrames % 12) == 0) && (sSpawnFailFrames < 200)) {
        const SpawnDef* def = &sPool[1]; // Fire Keese
        Vec3f spot;

        if (Ev_FindSpot(play, 400.0f, 700.0f, 0, 0x7FFF, false, &spot)) {
            spot.y = MIN(spot.y + 150.0f, Ev_SkyHeight(play, &spot, 150.0f));
            if (Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                         def->rotZ, def->params, TAG_KAMIKAZE) == NULL) {
                sSpawnFailFrames += 12;
            }
        } else {
            sSpawnFailFrames += 12;
        }
    }
}

// ---------------------------------------------------------------------------
// Wolf Pack: in the dark, Wolfos circle you and howl, then attack together.
// When the pack is beaten, another one comes.
// ---------------------------------------------------------------------------

typedef enum { WOLF_SPAWN, WOLF_CIRCLE, WOLF_ATTACK, WOLF_REST } WolfPhase;

#define WOLF_RADIUS 330.0f

static s32 sWolfPhase = WOLF_SPAWN;
static s32 sWolfTimer = 0;
static s16 sWolfAngle = 0;
static s32 sWolfBanner = 0;

static s32 Wolf_PackSize(void) {
    static const u8 sSizes[LVL_MAX] = { 3, 5, 8 };

    return sSizes[OptLvl(ID_WP_SIZE)];
}

static s32 Wolf_CountAlive(PlayState* play) {
    return Ev_CountTagged(play, ACTORCAT_PROP, TAG_WOLF) + Ev_CountTagged(play, ACTORCAT_ENEMY, TAG_WOLF);
}

static void Wolf_SpawnPack(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 n = Wolf_PackSize();
    s32 i;

    sWolfAngle = (s16)(Rand_ZeroOne() * 0xFFFF);
    for (i = 0; i < n; i++) {
        s16 yaw = sWolfAngle + (s16)(i * (0x10000 / n));
        s32 white;
        const SpawnDef* def;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;
        Vec3f spot;
        Actor* wolf;

        spot.x = player->actor.world.pos.x + Math_SinS(yaw) * WOLF_RADIUS;
        spot.y = player->actor.world.pos.y + 120.0f;
        spot.z = player->actor.world.pos.z + Math_CosS(yaw) * WOLF_RADIUS;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &spot);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > 150.0f)) {
            // No ground at its place in the circle: anywhere nearby will do.
            if (!Ev_FindSpot(play, 250.0f, 450.0f, 0, 0x7FFF, false, &spot)) {
                continue;
            }
        } else {
            spot.y = floorY;
        }

        switch (OptLvl(ID_WP_WHITE)) {
            case LVL_LOW:
                white = false;
                break;
            case LVL_MID:
                white = ((i % 3) == 2);
                break;
            default:
                white = true;
                break;
        }
        def = &sPool[white ? 4 : 3];
        wolf = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                        def->rotZ, def->params, TAG_WOLF);
        if (wolf != NULL) {
            TagData* t = Tag_Get(wolf);

            if (t != NULL) {
                t->timer2 = i;                              // its place in the circle
                t->timer = (s16)(10 + Rand_ZeroOne() * 50); // when it howls
            }
        }
    }
}

// Runs right after a wolf's own update while the pack is circling: keeps it
// running around Link at a distance.
static void Wolf_HoldCircle(PlayState* play, Actor* wolf, TagData* t) {
    Player* player = GET_PLAYER(play);
    s32 n = Wolf_PackSize();
    CollisionPoly* poly;
    s32 bgId;
    s16 yaw;
    Vec3f target;
    Vec3f probe;
    f32 dx;
    f32 dz;
    f32 dist;
    f32 floorY;

    if ((player == NULL) || (wolf->colChkInfo.health == 0)) {
        return;
    }
    yaw = sWolfAngle + (s16)((t->timer2 % n) * (0x10000 / n));
    target.x = player->actor.world.pos.x + Math_SinS(yaw) * WOLF_RADIUS;
    target.z = player->actor.world.pos.z + Math_CosS(yaw) * WOLF_RADIUS;

    // Run toward its spot rather than teleporting.
    dx = target.x - wolf->world.pos.x;
    dz = target.z - wolf->world.pos.z;
    dist = sqrtf(SQ(dx) + SQ(dz));
    probe = wolf->world.pos;
    if (dist > 1.0f) {
        f32 step = MIN(dist, 14.0f);

        probe.x += (dx / dist) * step;
        probe.z += (dz / dist) * step;
    }
    probe.y = wolf->world.pos.y + 60.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - wolf->world.pos.y) > 60.0f)) {
        return; // a drop or a wall that way; wait
    }
    wolf->world.pos.x = probe.x;
    wolf->world.pos.z = probe.z;
    wolf->world.pos.y = floorY;
    wolf->world.rot.y = wolf->shape.rot.y = yaw + 0x4000; // facing along the circle
}

static void Tick_WolfPack(PlayState* play) {
    static const u8 sCircleTime[INTENSITY_MAX] = { 120, 90, 60 };
    Player* player = GET_PLAYER(play);

    if (player == NULL) {
        return;
    }
    if (sWolfBanner > 0) {
        sWolfBanner--;
    }

    switch (sWolfPhase) {
        case WOLF_SPAWN:
            if (--sWolfTimer <= 0) {
                Wolf_SpawnPack(play);
                if (Wolf_CountAlive(play) > 0) {
                    sWolfPhase = WOLF_CIRCLE;
                    sWolfTimer = sCircleTime[Intensity()];
                } else {
                    sWolfTimer = 3 * FPS; // nowhere to put them; try again
                }
            }
            break;

        case WOLF_CIRCLE: {
            Actor* actor = play->actorCtx.actorLists[ACTORCAT_PROP].first;

            sWolfAngle += 0x140;
            // Howls around the circle.
            while (actor != NULL) {
                TagData* t = Tag_Get(actor);

                if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_WOLF) && (--t->timer <= 0)) {
                    Actor_PlaySfx(actor, NA_SE_EN_WOLFOS_CRY);
                    t->timer = (s16)(40 + Rand_ZeroOne() * 60);
                }
                actor = actor->next;
            }
            if (--sWolfTimer <= 0) {
                sWolfPhase = WOLF_ATTACK;
                sWolfBanner = 2 * FPS;
                Audio_PlaySfx(NA_SE_EN_WOLFOS_APPEAR);
            }
            break;
        }

        case WOLF_ATTACK:
            if (Wolf_CountAlive(play) == 0) {
                sWolfPhase = WOLF_REST;
                sWolfTimer = 5 * FPS;
            }
            break;

        case WOLF_REST:
            if (--sWolfTimer <= 0) {
                sWolfPhase = WOLF_SPAWN;
                sWolfTimer = 1;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Champion Duel: a mini-boss challenges you. No time limit; the event ends
// when it falls. Uses the Nemesis code for following you and its health bar.
// ---------------------------------------------------------------------------

static s32 Champion_Pick(void) {
    switch (gOpt[ID_CD_WHO] % CHAMP_MAX) {
        case CHAMP_IRON_KNUCKLE:
            return 31;
        case CHAMP_DINOLFOS:
            return 32;
        case CHAMP_GARO:
            return 30;
        default:
            // Any: half the time a mini-boss, otherwise one of the old Nemesis event's regulars.
            if (Rand_ZeroOne() < 0.5f) {
                return sNemMiniboss[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sNemMiniboss)) % ARRAY_COUNT(sNemMiniboss)];
            }
            return sNemRegular[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sNemRegular)) % ARRAY_COUNT(sNemRegular)];
    }
}

// Enemies already in the area sit out the duel. Ones close to Link (or holding
// him) are left alone so he can't end up stuck in a frozen grab.
static void Duel_MarkHolds(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if (t != NULL) {
            t->flags &= ~TAGF_DUEL_HOLD;
            if ((actor->update != NULL) && (t->tag == TAG_NONE) && (player != NULL) &&
                (player->actor.parent != actor) &&
                (Math_Vec3f_DistXZ(&actor->world.pos, &player->actor.world.pos) > 250.0f)) {
                t->flags |= TAGF_DUEL_HOLD;
            }
        }
        actor = actor->next;
    }
}

// ---------------------------------------------------------------------------
// Infighting: two rival groups brawl with each other. Get too close and they
// turn on you. When one side wins, the winners come for you.
// ---------------------------------------------------------------------------

// Ground fighters from sPool: Wolfos, White Wolfos, Stalchild, ReDead, Dodongo,
// Iron Knuckle, Dinolfos.
static const u8 sFighters[] = { 3, 4, 5, 6 }; // V3: Easy event, no heavies

static s32 sTeamPick[2] = { -1, -1 };
static s32 sTeamToSpawn[2] = { 0, 0 };
static s32 sTeamHad[2] = { false, false };
static s16 sTeamYaw = 0;
static s32 sInfightRest = 0;
static s32 sInfightWinnerShown = false;
static char sInfightToast[48];

static s32 Infight_TeamSize(void) {
    return 3;
}

static void Infight_NewBrawl(void) {
    s32 count = ARRAY_COUNT(sFighters);
    s32 a = (s32)(Rand_ZeroOne() * count) % count;
    s32 b;
    s32 tries = 0;

    // Two different kinds (both Wolfos colors count as one kind).
    do {
        b = (s32)(Rand_ZeroOne() * count) % count;
    } while (((b == a) || ((a <= 1) && (b <= 1))) && (++tries < 20));
    if ((b == a) || ((a <= 1) && (b <= 1))) {
        b = (a + 2) % count;
    }
    sTeamPick[0] = sFighters[a];
    sTeamPick[1] = sFighters[b];
    sTeamToSpawn[0] = sTeamToSpawn[1] = Infight_TeamSize();
    sTeamHad[0] = sTeamHad[1] = false;
    sTeamYaw = (s16)(Rand_ZeroOne() * 0xFFFF);
    sInfightRest = 0;
    sInfightWinnerShown = false;
    sSpawnFailFrames = 0;
}

static s32 Infight_CountTeam(PlayState* play, s32 team) {
    u8 tag = (team == 0) ? TAG_TEAM_A : TAG_TEAM_B;

    return Ev_CountTagged(play, ACTORCAT_ENEMY, tag) + Ev_CountTagged(play, ACTORCAT_PROP, tag);
}

static void Infight_SpawnOne(PlayState* play, s32 team) {
    Player* player = GET_PLAYER(play);
    const SpawnDef* def = &sPool[sTeamPick[team]];
    s16 side = sTeamYaw + ((team == 0) ? 0 : 0x8000);
    Actor* actor;
    Vec3f spot;

    if ((player == NULL) || !Ev_FindSpot(play, 300.0f, 500.0f, side, 0x2800, false, &spot)) {
        sSpawnFailFrames++;
        return;
    }
    spot.y += def->yOffset;
    actor = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, side + 0x8000, def->rotZ, def->params,
                     (team == 0) ? TAG_TEAM_A : TAG_TEAM_B);
    if (actor == NULL) {
        sSpawnFailFrames++;
        return;
    }
    Ev_AfterSpawn(play, actor);
    {
        TagData* t = Tag_Get(actor);

        if (t != NULL) {
            t->timer2 = MAX(actor->colChkInfo.health, 1); // starting health, for damage per hit
            t->timer = (s16)(Rand_ZeroOne() * 20);
        }
    }
    sTeamToSpawn[team]--;
    sTeamHad[team] = true;
}

static Actor* Infight_NearestRival(PlayState* play, Actor* self, u8 rivalTag, f32* outDist) {
    static const u8 sCats[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    Actor* best = NULL;
    f32 bestDist = 0.0f;
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            if ((actor != self) && (actor->update != NULL) && (actor->colChkInfo.health > 0) &&
                (Tag_Of(actor) == rivalTag)) {
                f32 d = Math_Vec3f_DistXZ(&self->world.pos, &actor->world.pos);

                if ((best == NULL) || (d < bestDist)) {
                    best = actor;
                    bestDist = d;
                }
            }
            actor = actor->next;
        }
    }
    *outDist = bestDist;
    return best;
}

static void Infight_Hit(PlayState* play, Actor* victim) {
    TagData* vt = Tag_Get(victim);
    s32 damage = MAX(1, ((vt != NULL) ? vt->timer2 : 5) / 5); // about five hits to knock one out
    Vec3f pos;

    Actor_SetColorFilter(victim, COLORFILTER_COLORFLAG_RED, 255, COLORFILTER_BUFFLAG_OPA, 8);
    Actor_PlaySfx(victim, NA_SE_IT_HAMMER_HIT);
    if (victim->colChkInfo.health > damage) {
        victim->colChkInfo.health -= damage;
        return;
    }

    // Knocked out.
    victim->colChkInfo.health = 0;
    pos = victim->world.pos;
    pos.y += 25.0f;
    func_800B3030(play, &pos, &gZeroVec3f, &gZeroVec3f, 150, 0, 0);
    Actor_PlaySfx(victim, NA_SE_EN_EXTINCT);
    Ev_KillOne(play, victim);
}

// Runs right after a brawler's own update: sends it after the nearest rival
// unless Link is the closer target.
static void Infight_Brawl(PlayState* play, Actor* self, TagData* t) {
    static const u8 sHitEvery[INTENSITY_MAX] = { 26, 20, 14 };
    Player* player = GET_PLAYER(play);
    u8 rivalTag = (t->tag == TAG_TEAM_A) ? TAG_TEAM_B : TAG_TEAM_A;
    Actor* rival;
    f32 dist;
    s16 yaw;

    if ((player == NULL) || (self->colChkInfo.health == 0)) {
        return;
    }
    rival = Infight_NearestRival(play, self, rivalTag, &dist);
    if (rival == NULL) {
        return; // their side won: Link's next
    }
    if ((self->xzDistToPlayer < 120.0f) && (self->xzDistToPlayer < dist)) {
        return; // Link is in its face
    }

    yaw = Math_Vec3f_Yaw(&self->world.pos, &rival->world.pos);
    self->world.rot.y = self->shape.rot.y = yaw;

    if (dist > 60.0f) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f next = self->world.pos;
        Vec3f from;
        Vec3f to;
        Vec3f hit;
        f32 floorY;
        f32 step = MIN(3.0f, dist - 60.0f);

        next.x += Math_SinS(yaw) * step;
        next.z += Math_CosS(yaw) * step;
        from = self->world.pos;
        from.y += 25.0f;
        to = next;
        to.y += 25.0f;
        if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
            return;
        }
        to.y = next.y + 50.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &to);
        if ((floorY > BGCHECK_Y_MIN) && (fabsf(floorY - self->world.pos.y) < 60.0f)) {
            next.y = floorY;
            self->world.pos = next;
        }
        return;
    }

    // Close enough to trade blows.
    if (t->timer > 0) {
        t->timer--;
        return;
    }
    t->timer = sHitEvery[Intensity()];
    Infight_Hit(play, rival);
}

static void Tick_Infight(PlayState* play) {
    s32 alive[2];
    s32 team;

    if ((sTeamPick[0] < 0) || (sTeamPick[1] < 0)) {
        Infight_NewBrawl();
    }

    // Both groups arrive over a few frames.
    if (((sTeamToSpawn[0] > 0) || (sTeamToSpawn[1] > 0)) && (sSpawnFailFrames < 90) && ((sEventFrames % 2) == 0)) {
        for (team = 0; team < 2; team++) {
            if (sTeamToSpawn[team] > 0) {
                Infight_SpawnOne(play, team);
            }
        }
        return;
    }

    alive[0] = Infight_CountTeam(play, 0);
    alive[1] = Infight_CountTeam(play, 1);

    // One side wiped out the other.
    if (!sInfightWinnerShown && sTeamHad[0] && sTeamHad[1] && ((alive[0] == 0) != (alive[1] == 0))) {
        s32 winner = (alive[0] > 0) ? 0 : 1;
        s32 n = Ev_Append(sInfightToast, 0, Pool_Name(sTeamPick[winner]), sizeof(sInfightToast));

        Ev_Append(sInfightToast, n, " won. You're next!", sizeof(sInfightToast));
        Menu_ShowToast(sInfightToast);
        sInfightWinnerShown = true;
    }

    // Everyone's gone: a new brawl starts nearby.
    if ((alive[0] == 0) && (alive[1] == 0)) {
        if (++sInfightRest > 4 * FPS) {
            Infight_NewBrawl();
        }
    }
}

// After each actor's own update (the game calls this for every actor).
RECOMP_CALLBACK("*", recomp_after_actor_update) void Events_AfterActorUpdate(PlayState* play, Actor* actor) {
    TagData* t;

    if ((actor != NULL) && (actor->update != NULL)) {
        Ev_NoPoeSouls(play, actor);
        if (actor->update == NULL) {
            return;
        }
        V2_AfterActorUpdate(play, actor);
        if (EV_RUNNING(EV_PREY)) {
            Prey_AfterActorUpdate(play, actor);
        }
    }
    if ((sActive < 0) || (actor == NULL) || (actor->update == NULL)) {
        return;
    }
    t = Tag_Get(actor);
    if (t == NULL) {
        return;
    }
    // V3: event enemies that start far off close in until they're close enough to fight.
    if ((t->tag == TAG_EVENT) && (actor->category == ACTORCAT_ENEMY) && (actor->xzDistToPlayer > 600.0f) &&
        (actor->colChkInfo.health > 0) && (GET_PLAYER(play) != NULL)) {
        if (Ev_StepToward(play, actor, &GET_PLAYER(play)->actor.world.pos, 6.0f)) {
            actor->shape.rot.y = actor->world.rot.y = actor->yawTowardsPlayer;
        }
    }
    if (EV_RUNNING(EV_WOLFPACK) && (t->tag == TAG_WOLF) && (sWolfPhase == WOLF_CIRCLE)) {
        Wolf_HoldCircle(play, actor, t);
    } else if (EV_RUNNING(EV_INFIGHT) && ((t->tag == TAG_TEAM_A) || (t->tag == TAG_TEAM_B))) {
        Infight_Brawl(play, actor, t);
    } else {
        Wave4_AfterActorUpdate(play, actor, t);
    }
}

// ===========================================================================
// Fourth wave: Time Moves When You Move, Imposters, Carpenter Uprising,
// Procession of the Dead, Poltergeist, Bullet Hell, Mirage, Mirror Dance,
// Swarm Night, Majora Looms and Sky Leviathan.
// ===========================================================================

// Model data inside objects (offsets from the decomp's asset lists).
#define DAIKU_SKELANIME(actor) ((SkelAnime*)((u8*)(actor) + 0x144))        // EnDaiku.skelAnime
#define DAIKU_COLLIDER(actor) ((ColliderCylinder*)((u8*)(actor) + 0x29C))  // EnDaiku.collider
#define ENRD_SKELANIME(actor) ((SkelAnime*)((u8*)(actor) + 0x144))         // EnRd.skelAnime
#define ENRD_COLLIDER(actor) ((ColliderCylinder*)((u8*)(actor) + 0x190))   // EnRd.collider
#define RD_WALK_ANIM ((AnimationHeader*)0x060113EC)                        // gGibdoRedeadWalkAnim
#define ISHI_SMALL_ROCK_DL ((Gfx*)0x060009B0)                              // gSmallRockDL
#define MAJORA_MASK_SKEL ((SkeletonHeader*)0x06019C58)                     // gMajorasMaskSkel
#define MAJORA_MASK_ANIM ((AnimationHeader*)0x0600AEDC)                    // gMajorasMaskStationaryAnim
#define MAJORA_EYES_NORMAL 0x42330                                         // gMajorasMaskWithNormalEyesTex
#define MAJORA_EYES_DULL 0x45B30                                           // gMajorasMaskWithDullEyesTex
#define GYORG_SKEL ((FlexSkeletonHeader*)0x060093A8)                       // gGyorgSkel
#define GYORG_SWIM_ANIM ((AnimationHeader*)0x0600A020)                     // gGyorgGentleSwimmingAnim
#define DAIKU_PARAMS_PLANK 0x3F03 // plank-carrying carpenter, no path
#define EV_HAHEN_DEFAULT_DEBRIS -1  // HAHEN_OBJECT_DEFAULT: plain rock debris

// Scale a Level3 option to one of three values.
#define LVL3(id, a, b, c) ((OptLvl(id) == LVL_LOW) ? (a) : (OptLvl(id) == LVL_MID) ? (b) : (c))

// Yaw of a direction, the same way the game measures it (0 = +Z).
static s16 Ev_YawOf(f32 dx, f32 dz) {
    Vec3f a = { 0.0f, 0.0f, 0.0f };
    Vec3f b;

    b.x = dx;
    b.y = 0.0f;
    b.z = dz;
    return Math_Vec3f_Yaw(&a, &b);
}

static void Ev_Poof(PlayState* play, Vec3f* pos, s32 colorIndex) {
    Vec3f p = *pos;

    p.y += 25.0f;
    func_800B3030(play, &p, &gZeroVec3f, &gZeroVec3f, 150, 0, colorIndex);
}

// Counts our living actors with this tag in the categories fighters and NPCs use.
static s32 Ev_CountTagAll(PlayState* play, u8 tag) {
    return Ev_CountTagged(play, ACTORCAT_ENEMY, tag) + Ev_CountTagged(play, ACTORCAT_PROP, tag) +
           Ev_CountTagged(play, ACTORCAT_NPC, tag);
}

// Moves an actor toward a point on the ground, stopping at walls and drops.
static s32 Ev_StepToward(PlayState* play, Actor* actor, Vec3f* target, f32 speed) {
    CollisionPoly* poly;
    s32 bgId;
    f32 dx = target->x - actor->world.pos.x;
    f32 dz = target->z - actor->world.pos.z;
    f32 dist = sqrtf(SQ(dx) + SQ(dz));
    Vec3f next;
    Vec3f from;
    Vec3f to;
    Vec3f hit;
    f32 floorY;

    if (dist < 1.0f) {
        return false;
    }
    // Never drag something that's in the air: a jumping enemy (Dinolfos, Tektite)
    // pulled back to the floor can get stuck mid-jump.
    if ((actor->category == ACTORCAT_ENEMY) && !(actor->bgCheckFlags & BGCHECKFLAG_GROUND) &&
        (actor->gravity < 0.0f)) {
        return false;
    }
    speed = MIN(speed, dist);
    next = actor->world.pos;
    next.x += (dx / dist) * speed;
    next.z += (dz / dist) * speed;
    from = actor->world.pos;
    from.y += 25.0f;
    to = next;
    to.y += 25.0f;
    if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
        return false;
    }
    to.y = next.y + 50.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &to);
    if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - actor->world.pos.y) > 50.0f)) {
        return false;
    }
    next.y = floorY;
    actor->world.pos = next;
    return true;
}

// Changes a puppet's animation (the model data has to be selected first).
static void Puppet_ChangeAnim(PlayState* play, Actor* actor, SkelAnime* skelAnime, AnimationHeader* anim) {
    uintptr_t saved = gSegments[6];

    Actor_SetObjectDependency(play, actor);
    Animation_Change(skelAnime, anim, 1.0f, 0.0f, Animation_GetLastFrame(anim), ANIMMODE_LOOP, -4.0f);
    gSegments[6] = saved;
}

// Lets Link hit (and bump into) a puppet whose own update never runs.
static void Puppet_RegisterCollider(PlayState* play, Actor* actor, ColliderCylinder* col) {
    Collider_UpdateCylinder(actor, col);
    CollisionCheck_SetAC(play, &play->colChkCtx, &col->base);
    CollisionCheck_SetOC(play, &play->colChkCtx, &col->base);
}

// Carpenters go home on the final night; these ones don't.
static Actor* Ev_SpawnCarpenter(PlayState* play, Vec3f* pos, s16 yaw, u8 tag) {
    s32 savedNight = gSaveContext.save.isNight;
    Actor* actor;

    gSaveContext.save.isNight = false;
    actor = Ev_SpawnPuppet(play, ACTOR_EN_DAIKU, OBJECT_DAIKU, pos, yaw, DAIKU_PARAMS_PLANK, tag);
    gSaveContext.save.isNight = savedNight;
    return actor;
}

// ---------------------------------------------------------------------------
// Time Moves When You Move: enemies close in, but the world only moves while
// you do (or swing, jump, or get hit).
// ---------------------------------------------------------------------------

static s32 sStillFreeze = false; // freeze the world this frame
static f32 sStillFlow = 0.0f;
static f32 sStillTint = 0.0f;

static void Tick_Still(PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 activity;

    if (player == NULL) {
        return;
    }

    // Enemies keep arriving from the Blood Moon pool.
    if (((sEventFrames % 20) == 0) && (Ev_CountOurEnemies(play) < LVL3(ID_ST_COUNT, 6, 10, 14)) &&
        (sSpawnFailFrames < 200)) {
        s32 pick = Ev_PickPoolEnemy(false);

        if (pick >= 0) {
            Ev_SpawnDef(play, &sPool[pick], sPool[pick].params, 300.0f, 650.0f);
        }
    }

    // How much Link is doing this frame decides how much time passes.
    activity = CLAMP(fabsf(player->speedXZ) / 5.0f, 0.0f, 1.0f);
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || (player->meleeWeaponState != 0) ||
        (player->invincibilityTimer != 0) || (player->actor.parent != NULL) ||
        (player->stateFlags2 & PLAYER_STATE2_80) || (player->csAction != PLAYER_CSACTION_NONE)) {
        activity = 1.0f; // jumping, swinging, hurt or grabbed: never freeze
    }
    activity = MAX(activity, LVL3(ID_ST_STILL, 0.0f, 0.05f, 0.2f));

    sStillFlow += activity;
    if (sStillFlow >= 1.0f) {
        sStillFlow -= 1.0f;
        sStillFreeze = false;
    } else {
        sStillFreeze = true;
    }
    sStillFlow = MIN(sStillFlow, 2.0f);
    Math_StepToF(&sStillTint, sStillFreeze ? 1.0f : 0.0f, 0.15f);
}

static s32 Still_ShouldFreeze(PlayState* play, Actor* actor) {
    Player* player = GET_PLAYER(play);

    if ((player == NULL) || (actor == player->actor.parent) || (actor == player->heldActor)) {
        return false;
    }
    switch (actor->category) {
        case ACTORCAT_ENEMY:
        case ACTORCAT_EXPLOSIVES:
            return true;
        case ACTORCAT_PROP:
            return (Tag_Of(actor) == TAG_EVENT) || (actor->id == ACTOR_EN_WF) || (actor->id == ACTOR_EN_NUTSBALL);
        case ACTORCAT_ITEMACTION:
            return actor->id == ACTOR_EN_ARROW; // once it leaves the bow, it waits for time like everything else
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Imposters: carpenters wander around. Some are ReDeads in disguise.
// ---------------------------------------------------------------------------

#define TAGS_IMPOSTER 1 // TagData.state: this one is a ReDead

static void Imposter_NewTarget(PlayState* play, Actor* actor, TagData* t) {
    if (!Ev_FindSpot(play, 100.0f, 600.0f, 0, 0x7FFF, false, &t->origPos)) {
        t->origPos = actor->world.pos;
    }
}

static void Tick_Imposters(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor;
    s32 crowd = LVL3(ID_IM_CROWD, 6, 10, 14);
    s32 share = LVL3(ID_IM_SHARE, 4, 3, 2);

    if (player == NULL) {
        return;
    }

    if (((sEventFrames % 10) == 0) && (Ev_CountTagAll(play, TAG_IMPOSTER) < crowd) && (sSpawnFailFrames < 200)) {
        Vec3f spot;

        if (Ev_FindSpot(play, 250.0f, 700.0f, 0, 0x7FFF, true, &spot)) {
            Actor* c = Ev_SpawnCarpenter(play, &spot, (s16)(Rand_ZeroOne() * 0xFFFF), TAG_IMPOSTER);
            TagData* t = (c != NULL) ? Tag_Get(c) : NULL;

            if (t != NULL) {
                t->state = ((s32)(Rand_ZeroOne() * share) == 0) ? TAGS_IMPOSTER : 0;
                Imposter_NewTarget(play, c, t);
            } else {
                sSpawnFailFrames += 10;
            }
        } else {
            sSpawnFailFrames += 10;
        }
    }

    actor = play->actorCtx.actorLists[ACTORCAT_NPC].first;
    while (actor != NULL) {
        Actor* next = actor->next;
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_IMPOSTER)) {
            SkelAnime* skel = DAIKU_SKELANIME(actor);
            f32 dist = Math_Vec3f_DistXZ(&actor->world.pos, &player->actor.world.pos);
            s32 moving = false;

            if ((t->state == TAGS_IMPOSTER) && (dist < 110.0f)) {
                // It was never a carpenter.
                const SpawnDef* def = &sPool[6]; // ReDead
                Vec3f pos = actor->world.pos;
                Actor* rd;

                Ev_Poof(play, &pos, 3);
                Ev_KillOne(play, actor);
                rd = Ev_Spawn(play, def->actorId, def->objectId, &pos, 0,
                              Math_Vec3f_Yaw(&pos, &player->actor.world.pos), def->rotZ, def->params, TAG_EVENT);
                if (rd != NULL) {
                    Actor_PlaySfx(rd, NA_SE_EN_REDEAD_AIM);
                }
                actor = next;
                continue;
            }

            if (dist < 150.0f) {
                // Stops and stares at you.
                Math_SmoothStepToS(&actor->shape.rot.y, Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos),
                                   4, 0x800, 0x100);
            } else {
                if (!Ev_StepToward(play, actor, &t->origPos, 1.4f) ||
                    (Math_Vec3f_DistXZ(&actor->world.pos, &t->origPos) < 20.0f)) {
                    Imposter_NewTarget(play, actor, t);
                } else {
                    moving = true;
                    Math_SmoothStepToS(&actor->shape.rot.y, Math_Vec3f_Yaw(&actor->world.pos, &t->origPos), 4,
                                       0x800, 0x100);
                }
            }
            actor->world.rot.y = actor->shape.rot.y;
            skel->playSpeed = moving ? 1.0f : 0.0f;
            Puppet_Animate(play, actor, skel);
        }
        actor = next;
    }
}

// ---------------------------------------------------------------------------
// Carpenter Uprising: rainbow carpenters come at you swinging their planks.
// ---------------------------------------------------------------------------

s32 Events_IsRainbowCarpenter(Actor* actor) {
    return (EV_RUNNING(EV_UPRISING) && (Tag_Of(actor) == TAG_UPRISING)) || (Tag_Of(actor) == TAG_BOARD);
}

s32 Events_IsEventActor(Actor* actor) {
    return Tag_IsOurs(Tag_Of(actor));
}

static void Tick_Uprising(PlayState* play) {
    static const f32 sSpeed[INTENSITY_MAX] = { 2.0f, 2.8f, 3.8f };
    static const u8 sDamage[INTENSITY_MAX] = { 0x08, 0x10, 0x18 };
    Player* player = GET_PLAYER(play);
    Actor* actor;

    if (player == NULL) {
        return;
    }

    if (((sEventFrames % 15) == 0) && (Ev_CountTagAll(play, TAG_UPRISING) < LVL3(ID_CU_COUNT, 4, 6, 9)) &&
        (sSpawnFailFrames < 200)) {
        Vec3f spot;
        Actor* c = NULL;

        if (Ev_FindSpot(play, 300.0f, 600.0f, 0, 0x7FFF, true, &spot)) {
            c = Ev_SpawnCarpenter(play, &spot, Math_Vec3f_Yaw(&spot, &player->actor.world.pos), TAG_UPRISING);
        }
        if (c != NULL) {
            ColliderCylinder* col = DAIKU_COLLIDER(c);

            // Let Link's attacks connect.
            col->base.acFlags = AC_ON | AC_TYPE_PLAYER;
            col->elem.acElemFlags |= ACELEM_ON;
            col->elem.acDmgInfo.dmgFlags = 0xF7CFFFFF;
            DAIKU_SKELANIME(c)->playSpeed = 1.5f;
            Actor_PlaySfx(c, NA_SE_EV_WOOD_BOUND);
        } else {
            sSpawnFailFrames += 15;
        }
    }

    actor = play->actorCtx.actorLists[ACTORCAT_NPC].first;
    while (actor != NULL) {
        Actor* next = actor->next;
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_UPRISING)) {
            ColliderCylinder* col = DAIKU_COLLIDER(actor);
            f32 dist = Math_Vec3f_DistXZ(&actor->world.pos, &player->actor.world.pos);
            s16 toLink = Math_Vec3f_Yaw(&actor->world.pos, &player->actor.world.pos);

            // Link hit back.
            if (col->base.acFlags & AC_HIT) {
                col->base.acFlags &= ~AC_HIT;
                if (t->timer3 == 0) {
                    Vec3f away = actor->world.pos;

                    t->timer2++;
                    t->timer3 = 12;
                    Actor_SetColorFilter(actor, COLORFILTER_COLORFLAG_RED, 255, COLORFILTER_BUFFLAG_OPA, 10);
                    Actor_PlaySfx(actor, NA_SE_EV_WOOD_HIT);
                    away.x -= Math_SinS(toLink) * 40.0f;
                    away.z -= Math_CosS(toLink) * 40.0f;
                    Ev_StepToward(play, actor, &away, 30.0f);
                    if (t->timer2 >= LVL3(ID_CU_HITS, 2, 3, 5)) {
                        Vec3f pos = actor->world.pos;

                        Ev_Poof(play, &pos, 3);
                        Actor_PlaySfx(actor, NA_SE_EN_EXTINCT);
                        Ev_KillOne(play, actor);
                        actor = next;
                        continue;
                    }
                }
            }
            if (t->timer3 > 0) {
                t->timer3--;
            }

            if (t->state > 0) {
                // Mid-swing: the plank sweeps around with him.
                actor->shape.rot.y += 0x1C00;
                t->state--;
                if ((t->state == 5) && (dist < 90.0f) && (fabsf(actor->world.pos.y - player->actor.world.pos.y) < 60.0f)) {
                    func_800B8D50(play, actor, 8.0f, toLink, 6.0f, sDamage[Intensity()]);
                    Audio_PlaySfx(NA_SE_EV_WOOD_BOUND);
                }
            } else {
                if (t->timer > 0) {
                    t->timer--;
                }
                if ((dist < 75.0f) && (t->timer == 0)) {
                    t->state = 9; // spin
                    t->timer = 45;
                    Actor_PlaySfx(actor, NA_SE_IT_SWORD_SWING);
                } else if (dist > 55.0f) {
                    Ev_StepToward(play, actor, &player->actor.world.pos, sSpeed[Intensity()]);
                    actor->shape.rot.y = toLink;
                }
            }
            actor->world.rot.y = actor->shape.rot.y;
            Puppet_Animate(play, actor, DAIKU_SKELANIME(actor));
        }
        actor = next;
    }
}

// ---------------------------------------------------------------------------
// Procession of the Dead: a column of ReDeads marches through the area.
// Touching it hurts. Attack it and the whole column turns on you.
// ---------------------------------------------------------------------------

#define PROC_SPACING 70.0f

static Vec3f sProcStart;
static f32 sProcDirX = 0.0f;
static f32 sProcDirZ = 1.0f;
static f32 sProcLen = 0.0f;
static f32 sProcHead = 0.0f;
static s32 sProcTurned = false;
static s32 sProcCount = 0;
static s32 sProcRest = 0;
static s32 sProcHurtCooldown = 0;
static s32 sProcBanner = 0;

// Checks that a straight march from `start` along the direction is walkable.
static s32 Proc_PathOk(PlayState* play, Vec3f* start, f32 dx, f32 dz, f32 len) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f prev = *start;
    f32 s;

    for (s = 40.0f; s <= len; s += 40.0f) {
        Vec3f p;
        Vec3f from;
        Vec3f to;
        Vec3f hit;
        f32 floorY;

        p.x = start->x + dx * s;
        p.y = prev.y + 60.0f;
        p.z = start->z + dz * s;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &p);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - prev.y) > 40.0f)) {
            return false;
        }
        p.y = floorY;
        from = prev;
        from.y += 30.0f;
        to = p;
        to.y += 30.0f;
        if (BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
            return false;
        }
        prev = p;
    }
    return true;
}

static s32 Proc_FindPath(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 tries;

    for (tries = 0; tries < 30; tries++) {
        s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
        f32 dx = Math_SinS(yaw);
        f32 dz = Math_CosS(yaw);
        f32 side = ((tries & 1) ? 1.0f : -1.0f) * (140.0f + Rand_ZeroOne() * 140.0f);
        f32 len = (tries < 15) ? 1500.0f : 900.0f;
        Vec3f start;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;

        // A line passing beside Link, perpendicular offset `side`.
        start.x = player->actor.world.pos.x + dz * side - dx * len * 0.5f;
        start.y = player->actor.world.pos.y + 100.0f;
        start.z = player->actor.world.pos.z - dx * side - dz * len * 0.5f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &start);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > 150.0f)) {
            continue;
        }
        start.y = floorY;
        if (Proc_PathOk(play, &start, dx, dz, len)) {
            sProcStart = start;
            sProcDirX = dx;
            sProcDirZ = dz;
            sProcLen = len;
            return true;
        }
    }
    return false;
}

// Where a member walks this frame (distance `s` along the route).
static void Proc_PointAt(PlayState* play, f32 s, f32 nearY, Vec3f* out) {
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY;

    out->x = sProcStart.x + sProcDirX * s;
    out->y = nearY + 60.0f;
    out->z = sProcStart.z + sProcDirZ * s;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, out);
    out->y = (floorY > BGCHECK_Y_MIN) ? floorY : nearY;
}

static void Proc_SpawnOnPath(PlayState* play);

static void Proc_Spawn(PlayState* play) {
    if (!Proc_FindPath(play)) {
        sProcTurned = false;
        sProcCount = 0;
        return;
    }
    Proc_SpawnOnPath(play);
}

// V3 Friend Link: a friend picks a spot the column marches through (across their path).
static s32 Proc_PlaceAt(PlayState* play, f32 x, f32 z) {
    Player* player = GET_PLAYER(play);
    Actor* actor;
    f32 dx;
    f32 dz;
    f32 d;
    s32 pass;

    if (player == NULL) {
        return false;
    }
    dx = x - player->actor.world.pos.x;
    dz = z - player->actor.world.pos.z;
    d = sqrtf(dx * dx + dz * dz);
    if (d < 1.0f) {
        dx = Math_SinS(player->actor.shape.rot.y);
        dz = Math_CosS(player->actor.shape.rot.y);
        d = 1.0f;
    }
    // March sideways across the line from Link to the mark.
    {
        f32 t = dx / d;

        dx = -dz / d;
        dz = t;
    }
    for (pass = 0; pass < 2; pass++) {
        f32 len = (pass == 0) ? 1500.0f : 900.0f;
        Vec3f start;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;

        start.x = x - dx * len * 0.5f;
        start.y = player->actor.world.pos.y + 150.0f;
        start.z = z - dz * len * 0.5f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &start);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > 200.0f)) {
            continue;
        }
        start.y = floorY;
        if (!Proc_PathOk(play, &start, dx, dz, len)) {
            continue;
        }
        // The old column goes; the new one walks the friend's line.
        actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
        while (actor != NULL) {
            Actor* next = actor->next;

            if ((actor->update != NULL) && (Tag_Of(actor) == TAG_PROCESSION)) {
                Ev_Poof(play, &actor->world.pos, 1);
                Actor_Kill(actor);
            }
            actor = next;
        }
        sProcStart = start;
        sProcDirX = dx;
        sProcDirZ = dz;
        sProcLen = len;
        Proc_SpawnOnPath(play);
        return sProcCount > 0;
    }
    return false;
}

static void Proc_SpawnOnPath(PlayState* play) {
    s32 n = LVL3(ID_PR_LENGTH, 8, 12, 16);
    s32 i;

    sProcTurned = false;
    sProcCount = 0;
    sProcHead = (n - 1) * PROC_SPACING;
    for (i = 0; i < n; i++) {
        f32 s = sProcHead - i * PROC_SPACING;
        Vec3f pos;
        Actor* rd;

        Proc_PointAt(play, s, sProcStart.y, &pos);
        rd = Ev_SpawnPuppet(play, sPool[6].actorId, sPool[6].objectId, &pos, Ev_YawOf(sProcDirX, sProcDirZ),
                            sPool[6].params, TAG_PROCESSION);
        if (rd != NULL) {
            TagData* t = Tag_Get(rd);

            if (t != NULL) {
                t->group = i;
            }
            rd->shape.rot.y = rd->world.rot.y = Math_Vec3f_Yaw(&sProcStart, &pos);
            Puppet_ChangeAnim(play, rd, ENRD_SKELANIME(rd), RD_WALK_ANIM);
            sProcCount++;
        }
    }
}

static void Proc_Turn(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    s32 cried = false;

    sProcTurned = true;
    sProcBanner = 2 * FPS;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_PROCESSION)) {
            t->tag = TAG_EVENT; // its own mind again
            actor->home.pos = actor->world.pos;
            if (!cried) {
                Actor_PlaySfx(actor, NA_SE_EN_REDEAD_CRY);
                cried = true;
            }
        }
        actor = actor->next;
    }
}

static void Tick_Procession(PlayState* play) {
    static const f32 sSpeed[INTENSITY_MAX] = { 0.9f, 1.2f, 1.6f };
    static const u8 sDamage[INTENSITY_MAX] = { 0x08, 0x10, 0x18 };
    Player* player = GET_PLAYER(play);
    Actor* actor;
    s16 dirYaw;

    if (player == NULL) {
        return;
    }
    if (sProcBanner > 0) {
        sProcBanner--;
    }
    if (sProcHurtCooldown > 0) {
        sProcHurtCooldown--;
    }

    // After the column is gone (beaten, or it turned and died), a new one comes.
    if (sProcTurned || (sProcCount == 0)) {
        if ((Ev_CountTagAll(play, TAG_PROCESSION) == 0) && (Ev_CountOurEnemies(play) == 0)) {
            if (++sProcRest > 5 * FPS) {
                sProcRest = 0;
                Proc_Spawn(play);
            }
        }
        if (sProcTurned) {
            return;
        }
    }

    sProcHead += sSpeed[Intensity()];
    dirYaw = Ev_YawOf(sProcDirX, sProcDirZ);

    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_PROCESSION)) {
            ColliderCylinder* col = ENRD_COLLIDER(actor);
            f32 s = sProcHead - t->group * PROC_SPACING;
            f32 wrapped = s - (s32)(s / sProcLen) * sProcLen;
            Vec3f pos;

            if (col->base.acFlags & AC_HIT) {
                col->base.acFlags &= ~AC_HIT;
                Proc_Turn(play);
                return;
            }

            // Past the end: it fades out and rejoins at the start.
            if ((t->savedGlow > wrapped + 1.0f) && (t->savedGlow > 0.0f)) {
                Ev_Poof(play, &actor->world.pos, 3);
                Proc_PointAt(play, wrapped, sProcStart.y, &pos);
                actor->world.pos = pos;
                Ev_Poof(play, &pos, 3);
            }
            t->savedGlow = wrapped;
            Proc_PointAt(play, wrapped, actor->world.pos.y, &pos);
            actor->world.pos = pos;
            actor->shape.rot.y = actor->world.rot.y = dirYaw;
            Puppet_Animate(play, actor, ENRD_SKELANIME(actor));

            // Walking into the column hurts.
            if ((sProcHurtCooldown == 0) && (Math_Vec3f_DistXZ(&pos, &player->actor.world.pos) < 45.0f) &&
                (fabsf(pos.y - player->actor.world.pos.y) < 60.0f)) {
                func_800B8D50(play, actor, 7.0f, Math_Vec3f_Yaw(&pos, &player->actor.world.pos), 5.0f,
                              sDamage[Intensity()]);
                Actor_PlaySfx(actor, NA_SE_EN_REDEAD_ATTACK);
                sProcHurtCooldown = 30;
            }
        }
        actor = actor->next;
    }
}

// ---------------------------------------------------------------------------
// Poltergeist: rocks tear out of the ground, circle you, then fly at you.
// ---------------------------------------------------------------------------

typedef enum { ROCK_RISE, ROCK_ORBIT, ROCK_FLY, ROCK_AIM } RockState;

#define ROCK_AIM_FRAMES 22

#define MAX_ROCKS 8

static s16 sRockSpin = 0;

static void Rock_Break(PlayState* play, Actor* rock) {
    Vec3f pos = rock->world.pos;

    EffectSsHahen_SpawnBurst(play, &pos, 8.0f, 0, 10, 5, 15, EV_HAHEN_DEFAULT_DEBRIS, 10, NULL);
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 20, NA_SE_EV_ROCK_BROKEN);
    Actor_Kill(rock);
}

static void Rock_Update(Actor* thisx, PlayState* play) {
    static const u8 sDamage[INTENSITY_MAX] = { 0x08, 0x10, 0x18 };
    Player* player = GET_PLAYER(play);
    TagData* t = Tag_Get(thisx);
    CollisionPoly* poly;
    s32 bgId;

    if ((t == NULL) || (player == NULL) || !Ev_IsOn(EV_POLTERGEIST)) {
        Actor_Kill(thisx);
        return;
    }
    thisx->shape.rot.x += 0x280;
    thisx->shape.rot.z += 0x1C0;

    switch (t->state) {
        case ROCK_RISE:
            thisx->world.pos.y += 2.5f;
            thisx->world.pos.x += Rand_CenteredFloat(2.0f);
            thisx->world.pos.z += Rand_CenteredFloat(2.0f);
            if (++t->timer >= 28) {
                t->state = ROCK_ORBIT;
                t->timer = 0;
            }
            break;

        case ROCK_ORBIT: {
            s16 ang = sRockSpin + (s16)(t->group * (0x10000 / MAX_ROCKS));
            Vec3f target;

            target.x = player->actor.world.pos.x + Math_SinS(ang) * 140.0f;
            target.y = player->actor.world.pos.y + 75.0f + Math_SinS((s16)(ang * 3)) * 15.0f;
            target.z = player->actor.world.pos.z + Math_CosS(ang) * 140.0f;
            thisx->world.pos.x += (target.x - thisx->world.pos.x) * 0.12f;
            thisx->world.pos.y += (target.y - thisx->world.pos.y) * 0.12f;
            thisx->world.pos.z += (target.z - thisx->world.pos.z) * 0.12f;
            break;
        }

        case ROCK_AIM: {
            // Pulls back away from you and shakes, then lets fly.
            s16 away = Math_Vec3f_Yaw(&player->actor.world.pos, &thisx->world.pos);
            Vec3f target;

            target.x = player->actor.world.pos.x + Math_SinS(away) * 280.0f;
            target.y = player->actor.world.pos.y + 90.0f;
            target.z = player->actor.world.pos.z + Math_CosS(away) * 280.0f;
            thisx->world.pos.x += (target.x - thisx->world.pos.x) * 0.15f + Rand_CenteredFloat(4.0f);
            thisx->world.pos.y += (target.y - thisx->world.pos.y) * 0.15f + Rand_CenteredFloat(4.0f);
            thisx->world.pos.z += (target.z - thisx->world.pos.z) * 0.15f + Rand_CenteredFloat(4.0f);
            if (++t->timer >= ROCK_AIM_FRAMES) {
                static const f32 sFlySpeed[INTENSITY_MAX] = { 11.0f, 13.0f, 15.0f };
                Vec3f chest = player->actor.world.pos;
                f32 dx;
                f32 dy;
                f32 dz;
                f32 d;

                // Aimed at where you are right now, then it flies straight. Keep moving.
                chest.y += 35.0f;
                dx = chest.x - thisx->world.pos.x;
                dy = chest.y - thisx->world.pos.y;
                dz = chest.z - thisx->world.pos.z;
                d = MAX(sqrtf(SQ(dx) + SQ(dy) + SQ(dz)), 1.0f);
                thisx->velocity.x = dx / d * sFlySpeed[Intensity()];
                thisx->velocity.y = dy / d * sFlySpeed[Intensity()];
                thisx->velocity.z = dz / d * sFlySpeed[Intensity()];
                t->state = ROCK_FLY;
                t->timer = 0;
                Actor_PlaySfx(thisx, NA_SE_EN_FFLY_ATTACK);
            }
            break;
        }

        case ROCK_FLY: {
            Vec3f chest = player->actor.world.pos;
            f32 floorY;

            chest.y += 35.0f;
            thisx->world.pos.x += thisx->velocity.x;
            thisx->world.pos.y += thisx->velocity.y;
            thisx->world.pos.z += thisx->velocity.z;
            if (Math_Vec3f_DistXYZ(&thisx->world.pos, &chest) < 30.0f) {
                func_800B8D50(play, thisx, 6.0f, Math_Vec3f_Yaw(&thisx->world.pos, &chest), 5.0f,
                              sDamage[Intensity()]);
                Rock_Break(play, thisx);
                return;
            }
            chest = thisx->world.pos;
            chest.y += 20.0f;
            floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &chest);
            if ((++t->timer > 80) || ((floorY > BGCHECK_Y_MIN) && (thisx->world.pos.y < floorY))) {
                Rock_Break(play, thisx);
                return;
            }
            break;
        }
    }
}

static void Rock_Draw(Actor* thisx, PlayState* play) {
    void* segment = Obj_GetBuffer(OBJECT_ISHI);
    TagData* t = Tag_Get(thisx);
    s32 aiming = (t != NULL) && (t->state == ROCK_AIM);

    if (segment == NULL) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, segment);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    if (aiming && (play->gameplayFrames & 2)) {
        gDPSetFogColor(POLY_OPA_DISP++, 255, 40, 40, 255); // flashing red: about to throw
        gSPFogPosition(POLY_OPA_DISP++, 0, 1200);
    }
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, ISHI_SMALL_ROCK_DL);
    if (aiming) {
        POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void Tick_Poltergeist(PlayState* play) {
    static const u8 sLaunchEvery[INTENSITY_MAX] = { 80, 60, 45 };
    Player* player = GET_PLAYER(play);
    s32 want = LVL3(ID_PG_ROCKS, 4, 6, 8);
    u8 used[MAX_ROCKS];
    s32 circling = 0;
    Actor* launch = NULL;
    Actor* actor;
    s32 i;

    if (player == NULL) {
        return;
    }
    sRockSpin += 0x180;

    for (i = 0; i < MAX_ROCKS; i++) {
        used[i] = false;
    }
    actor = play->actorCtx.actorLists[ACTORCAT_MISC].first;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_ROCK) && (t->state != ROCK_FLY)) {
            used[t->group % MAX_ROCKS] = true;
            circling++;
            if ((t->state == ROCK_ORBIT) && ((launch == NULL) || (Rand_ZeroOne() < 0.3f))) {
                launch = actor;
            }
        }
        actor = actor->next;
    }

    // One winds up and flies at you every so often.
    if ((launch != NULL) && ((sEventFrames % sLaunchEvery[Intensity()]) == 0)) {
        TagData* t = Tag_Get(launch);

        t->state = ROCK_AIM;
        t->timer = 0;
        Actor_PlaySfx(launch, NA_SE_EV_ROCK_BROKEN);
    }

    // New rocks tear loose to fill the ring.
    if ((circling < want) && ((sEventFrames % 12) == 0)) {
        Vec3f spot;
        Actor* rock;
        s32 slot;

        for (slot = 0; (slot < MAX_ROCKS) && used[slot]; slot++) {}
        if ((slot < MAX_ROCKS) && Ev_FindSpot(play, 90.0f, 220.0f, 0, 0x7FFF, false, &spot)) {
            spot.y -= 15.0f;
            rock = Ev_Spawn(play, ACTOR_EN_ITEM00, 0, &spot, 0, 0, 0, ITEM00_RUPEE_GREEN, TAG_ROCK);
            if (rock != NULL) {
                TagData* t = Tag_Get(rock);

                rock->update = Rock_Update;
                rock->draw = Rock_Draw;
                Actor_SetScale(rock, 0.12f);
                rock->shape.yOffset = 0.0f;
                rock->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
                if (t != NULL) {
                    t->state = ROCK_RISE;
                    t->group = slot;
                }
                EffectSsHahen_SpawnBurst(play, &spot, 5.0f, 0, 6, 3, 10, EV_HAHEN_DEFAULT_DEBRIS, 10, NULL);
                Actor_PlaySfx(rock, NA_SE_EV_ROCK_BROKEN);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Bullet Hell: a ring of Mad Scrubs fires sweeping fans of nuts.
// ---------------------------------------------------------------------------

static s16 sBulletRingYaw = 0;
static s32 sBulletRest = 0;
static s32 sBulletRingSlot = 0;

static void Bullet_Volley(PlayState* play, Actor* scrub) {
    Player* player = GET_PLAYER(play);
    s32 fan = LVL3(ID_BL_FAN, 3, 5, 7);
    s16 base = Math_Vec3f_Yaw(&scrub->world.pos, &player->actor.world.pos) +
               (s16)(Math_SinS((s16)(sEventFrames * 0x280)) * 0x1800); // the fan sweeps back and forth
    s32 k;

    for (k = 0; k < fan; k++) {
        s16 yaw = base + (s16)((k * 2 - (fan - 1)) * 0x0600);
        Vec3f pos = scrub->world.pos;
        Actor* nut;

        pos.x += Math_SinS(yaw) * 25.0f;
        pos.y += 25.0f;
        pos.z += Math_CosS(yaw) * 25.0f;
        nut = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_NUTSBALL, pos.x, pos.y, pos.z, 0, yaw, 0, 0);
        if ((nut != NULL) && (nut->update != NULL)) {
            Ev_TagActor(nut, TAG_BULLET);
            nut->gravity = 0.0f;
            nut->velocity.y = 0.0f;
            nut->world.rot.y = yaw;
        }
    }
    Actor_PlaySfx(scrub, NA_SE_EN_NUTS_THROW);
}

static void Tick_BulletHell(PlayState* play) {
    static const u8 sEvery[INTENSITY_MAX] = { 60, 42, 28 };
    Player* player = GET_PLAYER(play);
    s32 ring = LVL3(ID_BL_RING, 4, 6, 8);
    s32 scrubs = 0;
    Actor* actor;

    if (player == NULL) {
        return;
    }

    // The ring pops up around you, one scrub at a time.
    if ((sSpawnsLeft > 0) && ((sEventFrames % 3) == 0)) {
        s16 yaw = sBulletRingYaw + (s16)(sBulletRingSlot * (0x10000 / ring));
        Vec3f spot;
        Vec3f probe;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;

        probe.x = player->actor.world.pos.x + Math_SinS(yaw) * 280.0f;
        probe.y = player->actor.world.pos.y + 100.0f;
        probe.z = player->actor.world.pos.z + Math_CosS(yaw) * 280.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if ((floorY > BGCHECK_Y_MIN) && (fabsf(floorY - player->actor.world.pos.y) < 120.0f)) {
            spot = probe;
            spot.y = floorY;
        } else if (!Ev_FindSpot(play, 220.0f, 380.0f, yaw, 0x2000, false, &spot)) {
            spot.y = BGCHECK_Y_MIN;
        }
        if (spot.y > BGCHECK_Y_MIN) {
            Ev_Spawn(play, sPool[23].actorId, sPool[23].objectId, &spot, 0,
                     Math_Vec3f_Yaw(&spot, &player->actor.world.pos), 0, sPool[23].params, TAG_EVENT);
        }
        sBulletRingSlot++;
        sSpawnsLeft--;
    }

    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        if ((actor->update != NULL) && (actor->id == ACTOR_EN_DEKUNUTS) && (Tag_Of(actor) == TAG_EVENT) &&
            (actor->colChkInfo.health > 0)) {
            scrubs++;
            if (((sEventFrames % sEvery[Intensity()]) == 0) &&
                (Ev_CountTagged(play, ACTORCAT_PROP, TAG_BULLET) < 45)) {
                Bullet_Volley(play, actor);
            }
        }
        actor = actor->next;
    }

    // Nuts that flew off into the distance.
    actor = play->actorCtx.actorLists[ACTORCAT_PROP].first;
    while (actor != NULL) {
        if ((actor->update != NULL) && (Tag_Of(actor) == TAG_BULLET) && (actor->xzDistToPlayer > 1500.0f)) {
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    // Ring beaten: a new one pops up after a moment.
    if ((sSpawnsLeft == 0) && (scrubs == 0)) {
        if (++sBulletRest > 4 * FPS) {
            sBulletRest = 0;
            sBulletRingYaw = (s16)(Rand_ZeroOne() * 0xFFFF);
            sBulletRingSlot = 0;
            sSpawnsLeft = ring;
        }
    }
}

// ---------------------------------------------------------------------------
// Mirage: each enemy has shimmering twins. Only one is real; the fakes can't
// hurt you and vanish when hit.
// ---------------------------------------------------------------------------

// 3.1.8: Medium and Hard enemies (no Brutal ones): Wolfos, White Wolfos, Poe, Red Bubble,
// Like Like, Snapper, Garo, Dodongo, Peahat, Dinolfos.
static const u8 sMirageTypes[] = { 3, 4, 7, 11, 19, 24, POOL_GARO, 13, 28, 32 };

static u8 sMirageGroup = 1;

static Actor* Mirage_SpawnOne(PlayState* play, const SpawnDef* def, u8 tag, u8 group) {
    Player* player = GET_PLAYER(play);
    Actor* actor;
    Vec3f spot;

    if (!Ev_FindSpot(play, 300.0f, 650.0f, player->actor.shape.rot.y, 0x7FFF, true, &spot)) {
        return NULL;
    }
    spot.y += def->yOffset;
    actor = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                     def->rotZ, def->params, tag);
    if (actor != NULL) {
        TagData* t = Tag_Get(actor);

        Ev_AfterSpawn(play, actor);
        if (t != NULL) {
            t->group = group;
            t->timer2 = actor->colChkInfo.health;
        }
    }
    return actor;
}

static s32 Mirage_RealAlive(PlayState* play, u8 group) {
    static const u8 sCats[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            TagData* t = Tag_Get(actor);

            if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_MIRAGE_REAL) && (t->group == group) &&
                (actor->colChkInfo.health > 0)) {
                return true;
            }
            actor = actor->next;
        }
    }
    return false;
}

static void Tick_Mirage(PlayState* play) {
    static const u8 sCats[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    s32 c;

    // Two sets of three at a time (one real, two fakes each). The real ones are
    // tougher enemies than the Director lets a Normal event have, so only the
    // event's total budget limits them, and they take three times the hits.
    if (((sEventFrames % 30) == 0) && (Ev_CountTagAll(play, TAG_MIRAGE_REAL) < 2) &&
        (sSpawnFailFrames < 200)) {
        const SpawnDef* def = &sPool[sMirageTypes[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sMirageTypes)) %
                                                   ARRAY_COUNT(sMirageTypes)]];
        s32 cost = Dir_Cost(def->actorId);
        s32 fakes = 2;
        s32 f;
        Actor* real = NULL;

        if (sDirSpent + cost <= Dir_Budget() * 2) {
            real = Mirage_SpawnOne(play, def, TAG_MIRAGE_REAL, sMirageGroup);
        }
        if (real != NULL) {
            sDirSpent += cost;
            real->colChkInfo.health = (u8)MIN(real->colChkInfo.health * 3, 255);
            for (f = 0; f < fakes; f++) {
                Mirage_SpawnOne(play, def, TAG_MIRAGE_FAKE, sMirageGroup);
            }
            sMirageGroup = (sMirageGroup % 250) + 1;
        } else {
            sSpawnFailFrames += 30;
        }
    }

    // Fakes that got hit, or whose real one is gone, dissolve.
    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* actor = play->actorCtx.actorLists[sCats[c]].first;

        while (actor != NULL) {
            Actor* next = actor->next;
            TagData* t = Tag_Get(actor);

            if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_MIRAGE_FAKE) &&
                ((actor->colChkInfo.health < t->timer2) || !Mirage_RealAlive(play, t->group))) {
                Vec3f pos = actor->world.pos;

                Ev_Poof(play, &pos, 2);
                Actor_PlaySfx(actor, NA_SE_EN_PO_LAUGH);
                Ev_KillOne(play, actor);
            }
            actor = next;
        }
    }
}

// Takes a fake's attacks out of this frame's hit checks.
static void Mirage_StripAttacks(PlayState* play, Actor* actor) {
    CollisionCheckContext* ctx = &play->colChkCtx;
    s32 i;

    for (i = ctx->colATCount - 1; i >= 0; i--) {
        if ((ctx->colAT[i] != NULL) && (ctx->colAT[i]->actor == actor)) {
            ctx->colAT[i] = ctx->colAT[ctx->colATCount - 1];
            ctx->colAT[ctx->colATCount - 1] = NULL;
            ctx->colATCount--;
        }
    }
}

// ---------------------------------------------------------------------------
// Mirror Dance: enemies mirror your movement across an invisible line.
// ---------------------------------------------------------------------------

static const u8 sDancerTypes[] = { 5, 3, 32 }; // Stalchild, Wolfos, Dinolfos

static Vec3f sMirrorPoint;
static f32 sMirrorNX = 0.0f;
static f32 sMirrorNZ = 1.0f;
static s32 sMirrorRest = 0;

static void Mirror_Target(Player* player, s32 index, s32 count, Vec3f* out) {
    f32 d = (player->actor.world.pos.x - sMirrorPoint.x) * sMirrorNX +
            (player->actor.world.pos.z - sMirrorPoint.z) * sMirrorNZ;
    f32 side = (d <= 0.0f) ? 1.0f : -1.0f; // which side of the line the dancers are on
    f32 lateral = (index - (count - 1) * 0.5f) * 90.0f;
    f32 depth = fabsf(index - (count - 1) * 0.5f) * 35.0f;

    out->x = player->actor.world.pos.x - 2.0f * d * sMirrorNX + sMirrorNZ * lateral + sMirrorNX * depth * side;
    out->y = player->actor.world.pos.y;
    out->z = player->actor.world.pos.z - 2.0f * d * sMirrorNZ - sMirrorNX * lateral + sMirrorNZ * depth * side;
}

static void Mirror_Setup(PlayState* play) {
    Player* player = GET_PLAYER(play);
    const SpawnDef* def = &sPool[sDancerTypes[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sDancerTypes)) %
                                              ARRAY_COUNT(sDancerTypes)]];
    s32 count = LVL3(ID_MD_COUNT, 3, 5, 7);
    s32 i;

    // The mirror line is a little way in front of Link.
    sMirrorNX = Math_SinS(player->actor.shape.rot.y);
    sMirrorNZ = Math_CosS(player->actor.shape.rot.y);
    sMirrorPoint.x = player->actor.world.pos.x + sMirrorNX * 180.0f;
    sMirrorPoint.y = player->actor.world.pos.y;
    sMirrorPoint.z = player->actor.world.pos.z + sMirrorNZ * 180.0f;

    for (i = 0; i < count; i++) {
        Vec3f spot;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY;
        Actor* dancer;

        Mirror_Target(player, i, count, &spot);
        spot.y += 100.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &spot);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > 120.0f)) {
            if (!Ev_FindSpot(play, 250.0f, 450.0f, player->actor.shape.rot.y, 0x2000, false, &spot)) {
                continue;
            }
        } else {
            spot.y = floorY;
        }
        dancer = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, player->actor.shape.rot.y + 0x8000, def->rotZ,
                          def->params, TAG_MIRROR);
        if (dancer != NULL) {
            TagData* t = Tag_Get(dancer);

            Ev_AfterSpawn(play, dancer);
            if (t != NULL) {
                t->group = i;
                t->timer2 = count;
                t->state = 0; // dancing
            }
        }
    }
}

static void Mirror_Dance(PlayState* play, Actor* dancer, TagData* t) {
    Player* player = GET_PLAYER(play);
    Vec3f target;
    f32 vx;
    f32 vz;
    f32 dot;
    Vec3f facing;

    if ((player == NULL) || (t->state != 0) || (dancer->colChkInfo.health == 0)) {
        return;
    }
    // Close enough to fight: it stops dancing.
    if (dancer->xzDistToPlayer < 100.0f) {
        t->state = 1;
        return;
    }
    Mirror_Target(player, t->group, MAX(t->timer2, 1), &target);
    Ev_StepToward(play, dancer, &target, 14.0f);

    // Face the mirror image of Link's facing.
    vx = Math_SinS(player->actor.shape.rot.y);
    vz = Math_CosS(player->actor.shape.rot.y);
    dot = vx * sMirrorNX + vz * sMirrorNZ;
    facing = dancer->world.pos;
    facing.x += (vx - 2.0f * dot * sMirrorNX) * 10.0f;
    facing.z += (vz - 2.0f * dot * sMirrorNZ) * 10.0f;
    dancer->world.rot.y = dancer->shape.rot.y = Math_Vec3f_Yaw(&dancer->world.pos, &facing);
}

static void Tick_Mirror(PlayState* play) {
    if (Ev_CountTagAll(play, TAG_MIRROR) == 0) {
        if (++sMirrorRest > 3 * FPS) {
            sMirrorRest = 0;
            Mirror_Setup(play);
        }
    }
}

// ---------------------------------------------------------------------------
// Swarm Night: a storm of Keese circles you. Now and then a few break off
// and strike.
// ---------------------------------------------------------------------------

static void Swarm_Orbit(PlayState* play, Actor* keese, TagData* t) {
    Player* player = GET_PLAYER(play);
    s16 ang;
    Vec3f target;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 d;

    if ((player == NULL) || (t->state != 0) || (keese->colChkInfo.health == 0)) {
        return;
    }
    ang = (s16)(t->timer2 + sEventFrames * t->timer3);
    target.x = player->actor.world.pos.x + Math_SinS(ang) * t->savedGlow;
    target.y = player->actor.world.pos.y + t->origPos.y + Math_SinS((s16)(ang * 2)) * 25.0f;
    target.z = player->actor.world.pos.z + Math_CosS(ang) * t->savedGlow;
    dx = target.x - keese->world.pos.x;
    dy = target.y - keese->world.pos.y;
    dz = target.z - keese->world.pos.z;
    d = sqrtf(SQ(dx) + SQ(dy) + SQ(dz));
    if (d > 12.0f) {
        dx *= 12.0f / d;
        dy *= 12.0f / d;
        dz *= 12.0f / d;
    }
    keese->world.pos.x += dx;
    keese->world.pos.y += dy;
    keese->world.pos.z += dz;
    keese->world.rot.y = keese->shape.rot.y = ang + ((t->timer3 > 0) ? 0x4000 : -0x4000);
}

static void Tick_Swarm(PlayState* play) {
    static const u8 sStrikeEvery[INTENSITY_MAX] = { 60, 40, 25 };
    Player* player = GET_PLAYER(play);
    s32 want = LVL3(ID_SN_COUNT, 16, 24, 32);
    s32 alive = 0;
    Actor* pick = NULL;
    Actor* actor;

    if (player == NULL) {
        return;
    }

    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag == TAG_SWARM)) {
            alive++;
            if (t->state != 0) {
                // Striking; rejoin the swarm after a while.
                if (--t->timer <= 0) {
                    t->state = 0;
                }
            } else if ((pick == NULL) || (Rand_ZeroOne() < 0.1f)) {
                pick = actor;
            }
        }
        actor = actor->next;
    }

    if ((pick != NULL) && ((sEventFrames % sStrikeEvery[Intensity()]) == 0)) {
        TagData* t = Tag_Get(pick);

        t->state = 1;
        t->timer = 5 * FPS; // it's on its own now: Keese go for Link
    }

    // They pour down out of the sky.
    if ((alive < want) && ((sEventFrames % 3) == 0) && (sSpawnFailFrames < 200)) {
        s32 kind = 0;
        Vec3f spot;
        Actor* keese;

        if ((Intensity() >= INTENSITY_NORMAL) && (Rand_ZeroOne() < 0.25f)) {
            kind = 1; // Fire Keese
        } else if ((Intensity() == INTENSITY_BRUTAL) && (Rand_ZeroOne() < 0.15f)) {
            kind = 2; // Ice Keese
        }
        if (Ev_FindSpot(play, 100.0f, 400.0f, 0, 0x7FFF, false, &spot)) {
            spot.y = Ev_SkyHeight(play, &spot, 350.0f);
            keese = Ev_Spawn(play, sPool[kind].actorId, sPool[kind].objectId, &spot, 0, 0, 0, sPool[kind].params,
                             TAG_SWARM);
            if (keese != NULL) {
                TagData* t = Tag_Get(keese);

                if (t != NULL) {
                    t->state = 0;
                    t->timer2 = (s16)(Rand_ZeroOne() * 0xFFFF);
                    t->timer3 = (s16)((0x180 + Rand_ZeroOne() * 0x200) * ((Rand_ZeroOne() < 0.5f) ? 1 : -1));
                    t->savedGlow = 110.0f + Rand_ZeroOne() * 170.0f;
                    t->origPos.y = 50.0f + Rand_ZeroOne() * 170.0f;
                }
            } else {
                sSpawnFailFrames += 3;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Majora Looms: Majora's Mask hangs in the sky, locks onto where you stand,
// and brings a strike down there.
// ---------------------------------------------------------------------------

typedef enum { MJ_WATCH, MJ_TRACK, MJ_LOCKED } MajoraPhase;

static SkelAnime sMjSkel;
static Vec3s sMjJoints[32];
static Vec3s sMjMorph[32];
static s32 sMjReady = false;
static Vec3f sMjPos;
static f32 sMjDirX = 0.0f;
static f32 sMjDirZ = 1.0f;
static s32 sMjPhase = MJ_WATCH;
static s32 sMjTimer = 0;
static Vec3f sMjTarget;
static CollisionPoly* sMjTargetPoly = NULL;

static void Majora_Setup(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_BOSS07);
    Player* player = GET_PLAYER(play);
    uintptr_t saved = gSegments[6];
    s16 yaw;

    sMjReady = false;
    if ((buf == NULL) || (player == NULL)) {
        return;
    }
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_Init(play, &sMjSkel, MAJORA_MASK_SKEL, MAJORA_MASK_ANIM, sMjJoints, sMjMorph, 18 + 1);
    gSegments[6] = saved;

    // It appears ahead of Link, off in the distance.
    yaw = player->actor.shape.rot.y + (s16)Rand_CenteredFloat(0x4000);
    sMjDirX = Math_SinS(yaw);
    sMjDirZ = Math_CosS(yaw);
    sMjPos.x = player->actor.world.pos.x + sMjDirX * 2600.0f;
    sMjPos.y = player->actor.world.pos.y + 1300.0f;
    sMjPos.z = player->actor.world.pos.z + sMjDirZ * 2600.0f;
    sMjPhase = MJ_WATCH;
    sMjTimer = 2 * FPS;
    sMjReady = true;
}

#define MJ_FALL_FRAMES 36

static void Majora_Strike(PlayState* play) {
    Actor* mask = SkyRock_Launch(play, &sMjTarget, MJ_FALL_FRAMES, 0.035f);
    TagData* t = (mask != NULL) ? Tag_Get(mask) : NULL;

    if (t != NULL) {
        t->group = (u8)(Rand_ZeroOne() * 4.0f) & 3; // which boss mask it throws
        mask->draw = SkyRock_DrawMask;
    }
    Audio_PlaySfx(NA_SE_EN_LAST1_BEAM_OLD);
}

static void Tick_Majora(PlayState* play) {
    static const u8 sWatch[INTENSITY_MAX] = { 90, 65, 40 };
    Player* player = GET_PLAYER(play);
    Vec3f want;
    uintptr_t saved;
    void* buf = Obj_GetBuffer(OBJECT_BOSS07);

    if (!sMjReady || (player == NULL) || (buf == NULL)) {
        return;
    }
    saved = gSegments[6];
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_Update(&sMjSkel);
    gSegments[6] = saved;

    // It drifts along with you, always at the same spot in the sky.
    want.x = player->actor.world.pos.x + sMjDirX * 2600.0f;
    want.y = player->actor.world.pos.y + 1300.0f + Math_SinS((s16)(sEventFrames * 0x200)) * 60.0f;
    want.z = player->actor.world.pos.z + sMjDirZ * 2600.0f;
    sMjPos.x += (want.x - sMjPos.x) * 0.05f;
    sMjPos.y += (want.y - sMjPos.y) * 0.05f;
    sMjPos.z += (want.z - sMjPos.z) * 0.05f;

    switch (sMjPhase) {
        case MJ_WATCH:
            if (--sMjTimer <= 0) {
                sMjPhase = MJ_TRACK;
                sMjTimer = 14;
                Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
            }
            break;

        case MJ_TRACK:
        case MJ_LOCKED: {
            if (sMjPhase == MJ_TRACK) {
                CollisionPoly* poly;
                s32 bgId;
                Vec3f probe = player->actor.world.pos;
                f32 floorY;

                // The mark follows you...
                probe.y += 50.0f;
                floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                sMjTarget = player->actor.world.pos;
                sMjTargetPoly = NULL;
                if (floorY > BGCHECK_Y_MIN) {
                    sMjTarget.y = floorY;
                    sMjTargetPoly = poly;
                }
                if (--sMjTimer <= 0) {
                    // ...then stops. Move!
                    sMjPhase = MJ_LOCKED;
                    sMjTimer = MJ_FALL_FRAMES;
                    Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_E);
                    Majora_Strike(play);
                }
            } else if (--sMjTimer <= 0) {
                sMjPhase = MJ_WATCH;
                sMjTimer = sWatch[Intensity()];
                sMjTargetPoly = NULL;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Sky Leviathan: a colossal fish swims through the sky, then dives at you.
// ---------------------------------------------------------------------------

static SkelAnime sLevSkel;
static Vec3s sLevJoints[32];
static Vec3s sLevMorph[32];
static s32 sLevReady = false;
static Vec3f sLevPos;
static Vec3f sLevPrev;
static s16 sLevAngle = 0;

static void Leviathan_Setup(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_BOSS03);
    Player* player = GET_PLAYER(play);
    uintptr_t saved = gSegments[6];

    sLevReady = false;
    if ((buf == NULL) || (player == NULL)) {
        return;
    }
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_InitFlex(play, &sLevSkel, GYORG_SKEL, GYORG_SWIM_ANIM, sLevJoints, sLevMorph, 14 + 1);
    gSegments[6] = saved;

    sLevAngle = (s16)(Rand_ZeroOne() * 0xFFFF);
    sLevPos.x = player->actor.world.pos.x + Math_SinS(sLevAngle) * 1600.0f;
    sLevPos.y = player->actor.world.pos.y + 900.0f;
    sLevPos.z = player->actor.world.pos.z + Math_CosS(sLevAngle) * 1600.0f;
    sLevPrev = sLevPos;
    sLevReady = true;
}

// It only swims: big lazy circles high over you. It never attacks.
static void Tick_Leviathan(PlayState* play) {
    Player* player = GET_PLAYER(play);
    void* buf = Obj_GetBuffer(OBJECT_BOSS03);
    uintptr_t saved;
    Vec3f want;

    if (!sLevReady || (player == NULL) || (buf == NULL)) {
        return;
    }
    saved = gSegments[6];
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_Update(&sLevSkel);
    gSegments[6] = saved;
    sLevPrev = sLevPos;

    sLevAngle += 0x70;
    want.x = player->actor.world.pos.x + Math_SinS(sLevAngle) * 1600.0f;
    want.y = player->actor.world.pos.y + 900.0f + Math_SinS((s16)(sEventFrames * 0x150)) * 80.0f;
    want.z = player->actor.world.pos.z + Math_CosS(sLevAngle) * 1600.0f;
    sLevPos.x += (want.x - sLevPos.x) * 0.04f;
    sLevPos.y += (want.y - sLevPos.y) * 0.04f;
    sLevPos.z += (want.z - sLevPos.z) * 0.04f;
}

// ---------------------------------------------------------------------------
// Drawing things in the sky and on the ground
// ---------------------------------------------------------------------------

static void Ev_DrawGroundCircle(PlayState* play, Vec3f* pos, CollisionPoly* poly, f32 scale, u8 r, u8 g, u8 b,
                                u8 a) {
    MtxF mtx;

    OPEN_DISPS(play->state.gfxCtx);
    POLY_OPA_DISP = Gfx_SetupDL(POLY_OPA_DISP, SETUPDL_44);
    // Normal depth test instead of the shadow's "decal" mode, which only shows
    // where the circle sits exactly on the ground and so vanishes up close.
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0,
                      COMBINED);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, a);
    if (poly != NULL) {
        func_800C0094(poly, pos->x, pos->y + 2.0f, pos->z, &mtx);
        Matrix_Put(&mtx);
    } else {
        Matrix_Translate(pos->x, pos->y + 2.0f, pos->z, MTXMODE_NEW);
    }
    Matrix_Scale(scale, 1.0f, scale, MTXMODE_APPLY);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, CIRCLE_SHADOW_DL);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Fixed lighting for the sky giants, so the area's own lights don't wash them out.
static void Ev_SetSkyLights(PlayState* play, u8 ar, u8 ag, u8 ab) {
    Lights1* lights = GRAPH_ALLOC(play->state.gfxCtx, sizeof(Lights1));
    static const Lights1 sBase = gdSPDefLights1(120, 120, 120, 255, 255, 255, 0x00, 0x49, 0x49);

    *lights = sBase;
    lights->a.l.col[0] = lights->a.l.colc[0] = ar;
    lights->a.l.col[1] = lights->a.l.colc[1] = ag;
    lights->a.l.col[2] = lights->a.l.colc[2] = ab;
    OPEN_DISPS(play->state.gfxCtx);
    gSPSetLights1(POLY_OPA_DISP++, (*lights));
    CLOSE_DISPS(play->state.gfxCtx);
}

static void Majora_Draw(PlayState* play) {
    Player* player = GET_PLAYER(play);
    void* buf = Obj_GetBuffer(OBJECT_BOSS07);
    uintptr_t saved = gSegments[6];
    f32 scale = LVL3(ID_MJ_SIZE, 0.2f, 0.3f, 0.45f);
    Vec3f look;
    s16 yaw;
    s16 pitch;

    if (!sMjReady || (buf == NULL) || (player == NULL)) {
        return;
    }
    look = player->actor.world.pos;
    yaw = Math_Vec3f_Yaw(&sMjPos, &look);
    pitch = Math_Vec3f_Pitch(&sMjPos, &look);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, 0, 0, 0, 0, 0x3E7, 0x3200); // no distance haze on it
    gSPSegment(POLY_OPA_DISP++, 0x06, buf);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               (u8*)buf + ((sMjPhase == MJ_WATCH) ? MAJORA_EYES_DULL : MAJORA_EYES_NORMAL));
    CLOSE_DISPS(play->state.gfxCtx);
    Ev_SetSkyLights(play, 150, 120, 150);

    Matrix_Push();
    Matrix_Translate(sMjPos.x, sMjPos.y, sMjPos.z, MTXMODE_NEW);
    Matrix_RotateYS(yaw, MTXMODE_APPLY);
    Matrix_RotateXS(pitch, MTXMODE_APPLY); // tilt its face down toward Link
    Matrix_RotateZS((s16)(Math_SinS((s16)(play->gameplayFrames * 0x180)) * 0x600), MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_DrawOpa(play, sMjSkel.skeleton, sMjSkel.jointTable, NULL, NULL, NULL);
    gSegments[6] = saved;
    Matrix_Pop();

    OPEN_DISPS(play->state.gfxCtx);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);

    // The mark on the ground.
    if ((sMjPhase == MJ_TRACK) || (sMjPhase == MJ_LOCKED)) {
        u8 flash = (sMjPhase == MJ_LOCKED) && (play->gameplayFrames & 2);

        Ev_DrawGroundCircle(play, &sMjTarget, sMjTargetPoly, 1.4f, 200, flash ? 255 : 0, flash ? 255 : 120,
                            (sMjPhase == MJ_LOCKED) ? 220 : 150);
    }
}

static void Leviathan_Draw(PlayState* play) {
    void* buf = Obj_GetBuffer(OBJECT_BOSS03);
    uintptr_t saved = gSegments[6];
    f32 scale = LVL3(ID_LV_SIZE, 0.6f, 0.9f, 1.3f);
    f32 dx = sLevPos.x - sLevPrev.x;
    f32 dy = sLevPos.y - sLevPrev.y;
    f32 dz = sLevPos.z - sLevPrev.z;
    s16 yaw = Ev_YawOf(dx, dz);
    s16 pitch = Math_Vec3f_Pitch(&sLevPrev, &sLevPos); // nose down while diving
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe;
    f32 floorY;

    if (!sLevReady || (buf == NULL)) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, 0, 0, 0, 0, 0x3E7, 0x3200);
    gSPSegment(POLY_OPA_DISP++, 0x06, buf);
    CLOSE_DISPS(play->state.gfxCtx);
    Ev_SetSkyLights(play, 110, 120, 140);

    Matrix_Push();
    Matrix_Translate(sLevPos.x, sLevPos.y, sLevPos.z, MTXMODE_NEW);
    Matrix_RotateYS(yaw, MTXMODE_APPLY);
    Matrix_RotateXS(pitch, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSegments[6] = OS_K0_TO_PHYSICAL(buf);
    SkelAnime_DrawFlexOpa(play, sLevSkel.skeleton, sLevSkel.jointTable, sLevSkel.dListCount, NULL, NULL, NULL);
    gSegments[6] = saved;
    Matrix_Pop();

    OPEN_DISPS(play->state.gfxCtx);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);

    // Its shadow sweeps across the ground.
    probe = sLevPos;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY > BGCHECK_Y_MIN) {
        f32 height = CLAMP(sLevPos.y - floorY, 0.0f, 1500.0f);

        probe.y = floorY;
        Ev_DrawGroundCircle(play, &probe, poly, 6.0f + height * 0.004f, 0, 0, 0, 120);
    }
}

// ---------------------------------------------------------------------------
// Burning boulders from the sky (Moonfall, Majora Looms). They fall onto a
// fixed spot, marked on the ground, and burst there. Only Link gets hurt.
// ---------------------------------------------------------------------------

static void SkyRock_Update(Actor* thisx, PlayState* play) {
    TagData* t = Tag_Get(thisx);
    f32 u;

    if ((t == NULL) || (sActive < 0)) {
        Actor_Kill(thisx);
        return;
    }
    t->timer++;
    u = (f32)t->timer / (f32)MAX(t->timer2, 1);
    u = MIN(u, 1.0f);
    thisx->world.pos.x = thisx->home.pos.x + (t->origPos.x - thisx->home.pos.x) * u;
    thisx->world.pos.y = thisx->home.pos.y + (t->origPos.y - thisx->home.pos.y) * u * u; // speeds up as it falls
    thisx->world.pos.z = thisx->home.pos.z + (t->origPos.z - thisx->home.pos.z) * u;
    thisx->shape.rot.x += 0x600;
    thisx->shape.rot.z += 0x380;
    if ((t->timer % 3) == 0) {
        func_800B3030(play, &thisx->world.pos, &gZeroVec3f, &gZeroVec3f, 70, 0, 0); // smoke trail
    }
    if (t->timer >= t->timer2) {
        Vec3f blast = t->origPos;

        Ev_SpawnLinkBomb(play, &blast, 1);
        EffectSsHahen_SpawnBurst(play, &blast, 10.0f, 0, 12, 6, 20, EV_HAHEN_DEFAULT_DEBRIS, 10, NULL);
        Actor_RequestQuake(play, 3, 12);
        Actor_Kill(thisx);
    }
}

static void SkyRock_Draw(Actor* thisx, PlayState* play) {
    void* segment = Obj_GetBuffer(OBJECT_ISHI);
    TagData* t = Tag_Get(thisx);

    if (segment == NULL) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, segment);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    gDPSetFogColor(POLY_OPA_DISP++, 255, 110, 20, 255); // glowing hot
    gSPFogPosition(POLY_OPA_DISP++, 0, 1300);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, ISHI_SMALL_ROCK_DL);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);

    // Where it's going to land.
    if (t != NULL) {
        u8 a = 90 + (u8)(120.0f * CLAMP((f32)t->timer / (f32)MAX(t->timer2, 1), 0.0f, 1.0f));

        Ev_DrawGroundCircle(play, &t->origPos, NULL, 1.2f, 255, 60, 0, a);
    }
}

// Majora's version: one of the four boss masks, spinning down onto the mark.
static void SkyRock_DrawMask(Actor* thisx, PlayState* play) {
    static Gfx* const sMasks[4] = {
        (Gfx*)0x060149A0, // gBossMaskOdolwaDL
        (Gfx*)0x06016090, // gBossMaskGyorgDL
        (Gfx*)0x06017DE0, // gBossMaskGohtDL
        (Gfx*)0x06019328, // gBossMaskTwinmoldDL
    };
    void* buf = Obj_GetBuffer(OBJECT_BOSS07);
    TagData* t = Tag_Get(thisx);

    if ((buf == NULL) || (t == NULL)) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, buf);
    Gfx_SetupDL25_Opa(play->state.gfxCtx);
    POLY_OPA_DISP = Gfx_SetFog(POLY_OPA_DISP, 0, 0, 0, 0, 0x3E7, 0x3200); // no haze
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, sMasks[t->group & 3]);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}

static Actor* SkyRock_Launch(PlayState* play, Vec3f* target, s32 frames, f32 scale) {
    s16 dir = (s16)(Rand_ZeroOne() * 0xFFFF);
    Vec3f start = *target;
    Actor* rock;
    TagData* t;

    if (Obj_GetBuffer(OBJECT_ISHI) == NULL) {
        return NULL;
    }
    start.y = Ev_SkyHeight(play, target, 900.0f); // under any ceiling
    start.x += Math_SinS(dir) * 250.0f;
    start.z += Math_CosS(dir) * 250.0f;
    rock = Ev_Spawn(play, ACTOR_EN_ITEM00, 0, &start, 0, 0, 0, ITEM00_RUPEE_GREEN, TAG_SKYROCK);
    if (rock == NULL) {
        return NULL;
    }
    t = Tag_Get(rock);
    if (t == NULL) {
        Actor_Kill(rock);
        return NULL;
    }
    rock->update = SkyRock_Update;
    rock->draw = SkyRock_Draw;
    Actor_SetScale(rock, scale);
    rock->shape.yOffset = 0.0f;
    rock->shape.shadowDraw = NULL;
    rock->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    rock->home.pos = start;
    t->origPos = *target;
    t->timer = 0;
    t->timer2 = MAX(frames, 1);
    return rock;
}

// ---------------------------------------------------------------------------
// Fourth-wave hooks into the shared event flow
// ---------------------------------------------------------------------------

static void Wave4_OnStart(PlayState* play, s32 ev) {
    sStillFlow = 0.0f;
    sStillTint = 0.0f;
    switch (ev) {
        case EV_PROCESSION:
            sProcCount = 0;
            sProcTurned = false;
            sProcRest = 0;
            break;
        case EV_BULLETHELL:
            sBulletRest = 0;
            break;
        case EV_MIRROR:
            sMirrorRest = 3 * FPS; // first formation right away
            break;
        default:
            break;
    }
    Wave5_OnStart(play, ev);
}

static void Wave4_SceneSetup(PlayState* play) {
    switch (sActive) {
        case EV_PROCESSION:
            Proc_Spawn(play);
            sProcRest = 0;
            break;
        case EV_BULLETHELL:
            sBulletRingYaw = (s16)(Rand_ZeroOne() * 0xFFFF);
            sBulletRingSlot = 0;
            sSpawnsLeft = LVL3(ID_BL_RING, 4, 6, 8);
            break;
        case EV_MIRROR:
            sMirrorRest = 3 * FPS;
            break;
        case EV_MAJORA:
            if (!Ev_IsOutdoors(play)) {
                sMjReady = false;
                if (!sInComboPass) {
                    Menu_ShowToast("It can't follow you inside.");
                    Ev_Abort(play);
                }
                break;
            }
            Majora_Setup(play);
            if (!sMjReady && !sInComboPass) {
                Menu_ShowToast("The mask didn't come.");
                Ev_Abort(play);
            }
            break;
        case EV_LEVIATHAN:
            if (!Ev_IsOutdoors(play)) {
                sLevReady = false;
                if (!sInComboPass) {
                    Menu_ShowToast("It can't follow you inside.");
                    Ev_Abort(play);
                }
                break;
            }
            Leviathan_Setup(play);
            if (!sLevReady && !sInComboPass) {
                Menu_ShowToast("The sky stayed empty.");
                Ev_Abort(play);
            }
            break;
        default:
            Wave5_SceneSetup(play);
            break;
    }
}

static void Wave4_Tick(PlayState* play) {
    switch (sActive) {
        case EV_STILL:
            Tick_Still(play);
            break;
        case EV_IMPOSTER:
            Tick_Imposters(play);
            break;
        case EV_UPRISING:
            Tick_Uprising(play);
            break;
        case EV_PROCESSION:
            Tick_Procession(play);
            break;
        case EV_POLTERGEIST:
            Tick_Poltergeist(play);
            break;
        case EV_BULLETHELL:
            Tick_BulletHell(play);
            break;
        case EV_MIRAGE:
            Tick_Mirage(play);
            break;
        case EV_MIRROR:
            Tick_Mirror(play);
            break;
        case EV_SWARM:
            Tick_Swarm(play);
            break;
        case EV_MAJORA:
            Tick_Majora(play);
            break;
        case EV_LEVIATHAN:
            Tick_Leviathan(play);
            break;
        default:
            Wave5_Tick(play);
            break;
    }
}

static void Wave4_OnEnd(PlayState* play) {
    Wave5_OnEnd(play);
    sStillFreeze = false;
    sMjReady = false;
    sLevReady = false;
    sMjTargetPoly = NULL;
    sProcCount = 0;
}

static void Wave4_ResetForPlayInit(void) {
    Wave5_ResetForPlayInit();
    sStillFreeze = false;
    sMjReady = false;
    sLevReady = false;
    sMjTargetPoly = NULL;
    sProcCount = 0;
}

// Puppets that we move ourselves; some can still be hit.
static void Wave4_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should) {
    u8 tag;

    if (sActive < 0) {
        return;
    }
    tag = Tag_Of(actor);
    if (tag == TAG_IMPOSTER) {
        *should = false;
    } else if (tag == TAG_UPRISING) {
        *should = false;
        Puppet_RegisterCollider(play, actor, DAIKU_COLLIDER(actor));
    } else if (tag == TAG_PROCESSION) {
        *should = false;
        Puppet_RegisterCollider(play, actor, ENRD_COLLIDER(actor));
    } else if (EV_RUNNING(EV_STILL) && sStillFreeze && Still_ShouldFreeze(play, actor)) {
        *should = false;
    }
}

static void Wave4_AfterActorUpdate(PlayState* play, Actor* actor, TagData* t) {
    if (EV_RUNNING(EV_MIRAGE) && (t->tag == TAG_MIRAGE_FAKE)) {
        Mirage_StripAttacks(play, actor);
    } else if (EV_RUNNING(EV_MIRROR) && (t->tag == TAG_MIRROR)) {
        Mirror_Dance(play, actor, t);
    } else if (Ev_IsOn(EV_SWARM) && (t->tag == TAG_SWARM)) {
        Swarm_Orbit(play, actor, t);
    }
}

static void Wave4_DrawWorldEvent(PlayState* play) {
    if (EV_RUNNING(EV_MAJORA)) {
        Majora_Draw(play);
    } else if (EV_RUNNING(EV_LEVIATHAN)) {
        Leviathan_Draw(play);
    }
}

static void Wave4_DrawWorld(PlayState* play) {
    Wave4_DrawWorldEvent(play);
    EV_COMBO_PASS(Wave4_DrawWorldEvent(play));
    Wave5_DrawWorld(play);
}

static s32 Wave4_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b) {
    if (EV_RUNNING(EV_STILL) && sStillFreeze) {
        Ev_Append(line, 0, "Time stands still", size);
        *r = 150;
        *g = 210;
        *b = 255;
        return true;
    }
    if (EV_RUNNING(EV_PROCESSION) && (sProcBanner > 0)) {
        Ev_Append(line, 0, "The dead turn on you!", size);
        *r = 255;
        *g = 80;
        *b = 80;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Hooks into the shared event flow
// ---------------------------------------------------------------------------

static s32 Wave2_IsTimed(void) {
    return (sActive != EV_NEMESIS) && (sActive != EV_CHAMPION) && (sActive != EV_QUIZ);
}

static void Wave2_OnStart(PlayState* play, s32 ev) {
    switch (ev) {
        case EV_NEMESIS:
        case EV_CHAMPION:
            sNemPick = (ev == EV_CHAMPION) ? Champion_Pick() : Nemesis_Pick();
            sNemActor = NULL;
            sNemStarted = false;
            sNemHealth = 0;
            sNemMaxHealth = 1;
            sNemRespawnTimer = FPS;
            break;
        case EV_TREASURE:
            if (!sInComboPass) { // (as a combo's second half it doesn't set the whole combo's clock)
                sTotalFrames = sFramesLeft = Treasure_TimeFrames();
            }
            sTreasureValid = false;
            break;
        case EV_HUNGER:
            sHungerKills = 0;
            sHungerDrain = 0;
            break;
        case EV_SEARCH:
            sSpotAlert = 0;
            sSpotCooldown = 2 * FPS;
            sSpotTime = 0.0f;
            break;
        case EV_FATE: {
            s32 i;

            for (i = 0; i < MAX_FATE; i++) {
                sFate[i] = NULL;
            }
            sFateCooldown = 0;
            break;
        }
        case EV_REDLIGHT:
        case EV_GLARE:
            RL_StartGreen();
            sRLImmune = 0;
            sRLZapFlash = 0;
            break;
        case EV_WOLFPACK:
            sWolfPhase = WOLF_SPAWN;
            sWolfTimer = FPS;
            sWolfBanner = 0;
            break;
        case EV_INFIGHT:
            Infight_NewBrawl();
            break;
        case EV_ECHO:
            Echo_Reset();
            sEchoGhost = NULL;
            sEchoCooldown = 0;
            break;
        default:
            break;
    }
    Wave4_OnStart(play, ev);
}

static void Wave2_SceneSetup(PlayState* play) {
    switch (sActive) {
        case EV_NEMESIS:
            sNemActor = NULL;
            sNemRespawnTimer = FPS;
            break;
        case EV_CHAMPION:
            sNemActor = NULL;
            sNemRespawnTimer = FPS;
            sNemWaitFrames = 0;
            break;
        case EV_GLARE:
            Ev_EnsureMoon(play);
            break;
        case EV_WOLFPACK:
            // A fresh pack in each new area.
            sWolfPhase = WOLF_SPAWN;
            sWolfTimer = FPS;
            break;
        case EV_INFIGHT:
            // A fresh brawl in each new area, same two groups.
            sTeamToSpawn[0] = sTeamToSpawn[1] = Infight_TeamSize();
            sTeamHad[0] = sTeamHad[1] = false;
            sInfightRest = 0;
            sInfightWinnerShown = false;
            break;
        case EV_PHANTOM:
            sSpawnsLeft = Phantom_Size();
            break;
        case EV_TREASURE:
            if (!sTreasureValid && !Treasure_Place(play)) {
                Menu_ShowToast("No treasure could be hidden here.");
                Ev_Abort(play);
            }
            break;
        case EV_SEARCH:
            Search_Setup(play);
            break;
        case EV_FATE: {
            s32 i;

            for (i = 0; i < MAX_FATE; i++) {
                sFate[i] = NULL;
            }
            break;
        }
        case EV_ECHO:
            Echo_Reset();
            sEchoGhost = NULL;
            break;
        default:
            break;
    }
    Wave4_SceneSetup(play);
}

static void Wave2_Tick(PlayState* play) {
    switch (sActive) {
        case EV_NEMESIS:
        case EV_CHAMPION:
            Tick_Nemesis(play);
            break;
        case EV_GLARE:
            Tick_RedLight(play);
            break;
        case EV_KAMIKAZE:
            Tick_Kamikaze(play);
            break;
        case EV_WOLFPACK:
            Tick_WolfPack(play);
            break;
        case EV_INFIGHT:
            Tick_Infight(play);
            break;
        case EV_PHANTOM:
            Tick_Phantom(play);
            break;
        case EV_TREASURE:
            Tick_Treasure(play);
            break;
        case EV_HUNGER:
            Tick_Hunger(play);
            break;
        case EV_SEARCH:
            Tick_Search(play);
            break;
        case EV_FATE:
            Tick_Fate(play);
            break;
        case EV_REDLIGHT:
            Tick_RedLight(play);
            break;
        case EV_ECHO:
            Tick_Echo(play);
            break;
        default:
            Wave4_Tick(play);
            break;
    }
}

// Time ran out (timed events only).
static void Wave2_OnTimeout(PlayState* play) {
    if (EV_RUNNING(EV_PREY)) {
        Prey_OnTimeout(play);
    }
    if (EV_RUNNING(EV_TREASURE) && sTreasureValid) {
        static const u8 sFreeze[INTENSITY_MAX] = { 40, 60, 80 };

        sTreasureValid = false;
        sEventFailed = true; // missed it: no Marks, no streak
        Freeze_Start(play, sFreeze[Intensity()]);
        Menu_ShowToast("Too slow! Frozen!");
    }
}

// Cleanup when any event ends.
static void Wave2_OnEnd(PlayState* play) {
    s32 i;

    Fate_Release(play);
    for (i = 0; i < MAX_FATE; i++) {
        sFate[i] = NULL;
    }
    sNemActor = NULL;
    sEchoGhost = NULL;
    sTreasureValid = false;
    Treasure_RemoveLight(play);
    Search_RemoveLights(play);
    sSpotAlert = 0;
    sRLPhase = RL_GREEN;
    Wave4_OnEnd(play);
    // Phantoms are removed with everything else we spawned.
}

// Work that must continue even while Link is held in place.
static void Wave2_AlwaysUpdate(PlayState* play) {
    Freeze_Update(play);
    Fate_UpdateGrab(play);
}

static void Wave2_ResetForPlayInit(void) {
    s32 i;

    // The new area's light list and actors start fresh.
    sTreasureLight = NULL;
    sTreasureValid = false;
    for (i = 0; i < MAX_SPOTS; i++) {
        sSpots[i].light = NULL;
        sSpots[i].valid = false;
    }
    sSpotCount = 0;
    for (i = 0; i < MAX_FATE; i++) {
        sFate[i] = NULL;
    }
    sFateGrab = -1;
    sFateHitDelay = 0;
    sNemActor = NULL;
    sEchoGhost = NULL;
    sFreezeFrames = 0;
    sFreezeRelease = 0;
    Echo_Reset();
    Wave4_ResetForPlayInit();
}

// Scene objects are about to be reloaded (room change): drop pointers to our
// spawns, which are being removed.
static void Wave2_OnRoomChange(PlayState* play) {
    s32 i;

    Wave5_OnRoomChange(play);
    Fate_Release(play);
    for (i = 0; i < MAX_FATE; i++) {
        sFate[i] = NULL;
    }
    sEchoGhost = NULL;
    sNemActor = NULL;
}

// Events that should keep the world update frozen for their puppets.
static void Wave2_ShouldActorUpdate(Actor* actor, bool* should) {
    u8 tag = Tag_Of(actor);

    if ((tag == TAG_FATE) || (tag == TAG_ECHO)) {
        *should = false;
    }
}

// ---------------------------------------------------------------------------
// World drawing (spotlights and the treasure), after all actors are drawn.
// ---------------------------------------------------------------------------

static void Treasure_Draw(PlayState* play) {
    void* segment = Obj_GetBuffer(OBJECT_GI_RUPY);
    uintptr_t savedSegment6;
    f32 bob = Math_SinS((s16)(play->gameplayFrames * 0x600)) * 6.0f;

    if (segment == NULL) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, segment);
    gSPSegment(POLY_XLU_DISP++, 0x06, segment);
    CLOSE_DISPS(play->state.gfxCtx);

    Matrix_Translate(sTreasurePos.x, sTreasurePos.y + 35.0f + bob, sTreasurePos.z, MTXMODE_NEW);
    Matrix_RotateYS((s16)(play->gameplayFrames * 0x500), MTXMODE_APPLY);
    Matrix_Scale(0.5f, 0.5f, 0.5f, MTXMODE_APPLY);

    savedSegment6 = gSegments[6];
    gSegments[6] = OS_K0_TO_PHYSICAL(segment);
    GetItem_Draw(play, GID_RUPEE_HUGE);
    gSegments[6] = savedSegment6;
}

static PlayState* sDrawAllPlay = NULL;

RECOMP_HOOK("Actor_DrawAll") void Events_BeforeDrawAll(PlayState* play, ActorContext* actorCtx) {
    sDrawAllPlay = play;
}

RECOMP_HOOK_RETURN("Actor_DrawAll") void Events_AfterDrawAll(void) {
    PlayState* play = sDrawAllPlay;

    sDrawAllPlay = NULL;
    if (play != NULL) {
        V2_DrawWorld(play);
        V3_DrawFriendWorld(play);
    }
    if ((play == NULL) || !sSceneReady) {
        return;
    }
    if (EV_RUNNING(EV_SEARCH) && (sSpotCount > 0)) {
        Search_Draw(play);
    }
    if (EV_RUNNING(EV_TREASURE) && sTreasureValid) {
        Treasure_Draw(play);
    }
    if (Ev_IsOn(EV_BOMB_RAIN)) {
        s32 k;

        for (k = 0; k < sBombMarkCount; k++) {
            f32 f = 1.0f - sBombMarks[k].timer / 50.0f;

            Ev_DrawGroundCircle(play, &sBombMarks[k].pos, NULL, 0.6f + 0.6f * f, 255, 40, 20, (u8)(90 + 140 * f));
        }
    }
    if (Ev_IsOn(EV_MOONFALL)) {
        // Where each boulder will land.
        Actor* rock = play->actorCtx.actorLists[ACTORCAT_MISC].first;

        while (rock != NULL) {
            TagData* t = Tag_Get(rock);

            if ((rock->update != NULL) && (t != NULL) && (t->tag == TAG_SKYROCK) && (t->timer2 > 0)) {
                f32 f = CLAMP(t->timer / (f32)t->timer2, 0.0f, 1.0f);

                Ev_DrawGroundCircle(play, &t->origPos, NULL, 1.4f - 0.5f * f, 255, 60, 30, (u8)(90 + 150 * f));
            }
            rock = rock->next;
        }
    }
    Wave4_DrawWorld(play);
    V3_DrawWorld(play);
}

#include "ev_v11.inc"
#include "ev_v2.inc"
#include "ev_prey.inc"
#include "ev_remote.inc"
#include "ev_v3.inc"
#include "ev_exp.inc"

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------

static void Ev_TickEvent(PlayState* play) {
    switch (sActive) {
        case EV_BOMB_RAIN:
            Tick_BombRain(play);
            break;
        case EV_RUPOOR_RAIN:
            Tick_RupoorRain(play);
            break;
        case EV_BLOOD_MOON:
            Tick_BloodMoon(play);
            break;
        case EV_JOKE:
            Tick_Joke(play);
            break;
        case EV_MOONFALL:
            Tick_Moonfall(play);
            break;
        case EV_STALKER:
            Tick_Stalker(play);
            break;
        case EV_POE_HUNT:
            Tick_PoeHunt(play);
            break;
        default:
            if (V3_IsEvent(sActive)) {
                V3_Tick(play);
                break;
            }
            Wave2_Tick(play);
            break;
    }
}

static void Curse_EventTick(PlayState* play);

static void Ev_Tick(PlayState* play) {
    if (!(sComboHalt & 1)) {
        Ev_TickEvent(play);
    }
    Curse_EventTick(play);
    if ((sCombo >= 0) && !(sComboHalt & 2)) {
        s32 spawnsLeft = sSpawnsLeft;
        s32 failFrames = sSpawnFailFrames;

        EV_COMBO_PASS(Ev_TickEvent(play));
        sSpawnsLeft = spawnsLeft;
        sSpawnFailFrames = failFrames;
    }
    if (sActive >= 0) {
        Ev_Upkeep(play);
    }
}

// Atmosphere stays applied during cutscenes and menus too.
static s32 Ev_ApplyAtmosphereEvent(PlayState* play) {
    switch (sActive) {
        case EV_BLOOD_MOON:
            Ev_ApplyEnv(play, ENV_RED, Ev_Fade());
            return true;
        case EV_MOONFALL:
            Ev_ApplyEnv(play, ENV_ORANGE, Ev_Fade());
            return true;
        case EV_STALKER:
        case EV_POE_HUNT:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade());
            return true;
        case EV_PHANTOM:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.5f);
            return true;
        case EV_HUNGER:
            Ev_ApplyEnv(play, ENV_RED, Ev_Fade() * 0.45f);
            return true;
        case EV_SEARCH:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.8f);
            return true;
        case EV_FATE:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.4f);
            return true;
        case EV_ECHO:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.35f);
            return true;
        case EV_REDLIGHT:
            Ev_ApplyEnv(play, ENV_RED, RL_MoonGlow() * 0.7f);
            return true;
        case EV_GLARE:
            Ev_ApplyEnv(play, ENV_ORANGE, MIN(1.0f, Ev_Fade() * 0.3f + RL_MoonGlow() * 0.6f));
            return true;
        case EV_WOLFPACK:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.75f);
            return true;
        case EV_STILL:
            Ev_ApplyEnv(play, ENV_COLD, Ev_Fade() * sStillTint * 0.8f);
            return true;
        case EV_IMPOSTER:
        case EV_POLTERGEIST:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.35f);
            return true;
        case EV_PROCESSION:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.55f);
            return true;
        case EV_SWARM:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.7f);
            return true;
        case EV_MAJORA:
            Ev_ApplyEnv(play, ENV_RED, Ev_Fade() * 0.35f);
            return true;
        case EV_LEVIATHAN:
            Ev_ApplyEnv(play, ENV_DARK, Ev_Fade() * 0.3f);
            return true;
        default:
            return false;
    }
}

// The first event's look, or the combo's if the first has none.
static void Ev_ApplyAtmosphere(PlayState* play) {
    s32 done = Ev_ApplyAtmosphereEvent(play);

    if (!done && (sCombo >= 0)) {
        EV_COMBO_PASS(done = Ev_ApplyAtmosphereEvent(play));
    }
}

void Events_Update(PlayState* play) {
    s32 playing = Ev_GameplayActive(play);

    sStillFreeze = false; // Time Moves When You Move decides again each frame, while playing
    V3_Always(play);
    Linger_Update(play, playing);

    Ev_UpdateMusic(play);
    Stats_CheckDeath(play);
    if (sPostEventFrames > 0) {
        sPostEventFrames--;
    }

    // New cycle or save loaded: whatever was running stops, but events stay
    // on if the player turned them on.
    if (sPendingCycleReset) {
        sPendingCycleReset = false;
        Ev_Abort(play);
        sRouletteTimer = 0;
        sUntilNext = Ev_NextDelay();
        sPrevClock = gSaveContext.save.time;
    }

    if (sBannerTimer > 0) {
        sBannerTimer--;
    }
    sPauseReason = Ev_PauseReason(play);
    V2_Update(play, playing); // first, so it sees who dealt the final blow
    // Dying ends the event (and clears out what it brought). Nothing else
    // happens this frame, so no new event can slip in while Link goes down.
    if (sEndOnDeath) {
        sEndOnDeath = false;
        if (sActive >= 0) {
            Menu_ShowToast("You fell. The event is over.");
            sComboHalt = 3; // both halves end
            Ev_End(play);
        }
        return;
    }
    Curse_NightCheck(play, playing);
    if (sCurseCardTimer > 0) {
        sCurseCardTimer--;
    } else if (sCurseCardPending && playing && (sActive < 0) && (sRouletteTimer == 0) && gOpt[ID_EV_CURSE] &&
               gOpt[ID_EV_MASTER] && (sCurse >= 0)) {
        sCurseCardPending = false;
        sCurseCardTimer = CARD_FRAMES;
        Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
    }

    if (!gMenuOpen && !IS_PAUSED(&play->pauseCtx)) {
        Wave2_AlwaysUpdate(play);
    }

    if (sActive >= 0) {
        // Safe zone (shop, minigame, boss room): the event waits outside.
        // Its hazards are cleared here and set up again once you leave.
        if (sPauseReason == 1) {
            if (sSceneReady) {
                Wave2_OnRoomChange(play);
                Ev_KillOurs(play);
                Ev_RestoreMoons(play);
                Ev_ClearEnv(play);
                Ev_RestoreLighting(play);
                Treasure_RemoveLight(play);
                Search_RemoveLights(play);
                Ev_StopMusicSoon();
                Obj_RemoveAll(play);
                sSceneReady = false;
            }
            return;
        }
        if (!sSceneReady && playing) {
            Ev_SceneSetup(play);
            sSceneReady = true;
            if (sActive < 0) {
                return;
            }
        }
        if (sSceneReady) {
            Ev_ApplyAtmosphere(play);
        }
        if (playing && sSceneReady) {
            if (sMusicCheckFrames > 0) {
                sMusicCheckFrames--;
                if (((sMusicCheckFrames % 10) == 0) && (Ev_EventSong() != NA_BGM_DISABLED)) {
                    Ev_StartMusic(Ev_EventSong());
                }
            }
            Ev_Tick(play);
            if (sActive < 0) {
                return; // it ended itself (won, found, ...)
            }
            sEventFrames++;
            // Time Moves When You Move: its clock only runs while you do.
            // The clock waits while Time Moves When You Move holds still, while a Pop Quiz
            // waits for your answer, and while a ported event's how-to is still up.
            if (Wave2_IsTimed() && !(EV_RUNNING(EV_STILL) && sStillFreeze) && !Quiz_Asking() &&
                !((V3_Cur() >= 0) && (sBannerTimer > 0))) {
                sFramesLeft--;
                if ((sFramesLeft <= 0) && (sCombo >= 0) && !(sComboHalt & 2) && Ev_HasGoal(sCombo) &&
                    !(V3_IsLong(sCombo) && (sEventFrames >= 60 * FPS + 5 * FPS))) {
                    // Time's up for the first half; the second still has to be cleared.
                    sFramesLeft = 0;
                    if (!(sComboHalt & 1)) {
                        Wave2_OnTimeout(play);
                        Ev_End(play); // marks the first half cleared
                    }
                } else if (sFramesLeft <= 0) {
                    static char sEndToast[40];
                    s32 hasOwnMessage = EV_RUNNING(EV_TREASURE) || Prey_HasTarget(play);
                    s32 n;

                    if (!hasOwnMessage) {
                        n = Ev_Append(sEndToast, 0, Curse_Is(CURSE_UNSEEN) ? "It" : Events_Name(sActive),
                                      sizeof(sEndToast));
                        Ev_Append(sEndToast, n, (sCombo >= 0) ? " + combo survived!" : " survived!",
                                  sizeof(sEndToast));
                        Menu_ShowToast(sEndToast);
                    }
                    Wave2_OnTimeout(play);
                    sComboHalt = 3; // time's up for everything still running
                    Ev_End(play); // may replace the message with a streak or wrath one
                }
            }
        }
        return;
    }

    if (sRouletteTimer > 0) {
        if (playing) {
            Roulette_Update(play);
        }
        return;
    }
    if (!gOpt[ID_EV_MASTER]) {
        return;
    }
    // Real time: the countdown keeps going while they're paused or in a menu
    // (the event itself still waits until they're back in action).
    {
        static OSTime sLastTick = 0;
        static u32 sMsBank = 0;
        OSTime now = osGetTime();
        u32 ms = (sLastTick != 0) ? (u32)(now - sLastTick) / (OS_CPU_COUNTER / 1000) : 0;

        sLastTick = now;
        if (playing) {
            sMsBank = 0;
        } else if ((sActive < 0) && (sCurseCardTimer == 0) && (sPauseReason == 0) && !Curse_Is(CURSE_BELL)) {
            sMsBank += MIN(ms, 2000);
            while ((sMsBank >= 1000 / FPS) && (sUntilNext > 1)) {
                sMsBank -= 1000 / FPS;
                sUntilNext--;
            }
        }
    }
    if (playing) {
        if ((sActive >= 0) || (sCurseCardTimer > 0) || (sPauseReason != 0)) {
            return; // safe zones and Active Hours hold the countdown
        }
        if (Aftershock_Update(play) || Ambush_Update(play)) {
            return;
        }
        if (Curse_Is(CURSE_BELL)) {
            return; // Bell Toll: events come on the clock instead (see Curse_NightCheck)
        }
        sUntilNext--;
        if (gOpt[ID_EV_WARNSOUND] && Ev_ShowWarning() && (sFakeTimer == 0) && ((sUntilNext % FPS) == 0)) {
            Audio_PlaySfx(NA_SE_SY_WARNING_COUNT_N);
        }
        if (sUntilNext <= 0) {
            Ev_BeginRandom(play);
        }
    }
}

static s32 sConfigApplied = false;

void Events_OnPlayInit(PlayState* play) {
    // "Events On At Start" (mod config): switch events on once per session.
    if (!sConfigApplied) {
        sConfigApplied = true;
        if (recomp_get_config_u32("events_on_start") != 0) {
            gOpt[ID_EV_MASTER] = true;
            sUntilNext = Ev_NextDelay();
        }
    }
    Ambush_OnNewArea();
    V2_OnNewArea();
    sLingerN = 0;
    // New area: our spawns, object entries, light and music are all gone.
    sInjectionCount = 0;
    sSceneReady = false;
    sSkySaved = false;
    sEnvDirty = false;
    sLinkLight = NULL;
    // Music is left as is: some area changes keep the current song playing,
    // and the event code checks what's actually playing before changing it.
    sMusicCheckFrames = 0;
    sOdolwaLightSaved = false;
    sDogPoints = NULL;
    sDogPathsReady = false;
    sRoomHadEnemies = false;
    sMeteorUpdating = NULL;
    sMoonUpdating = NULL;
    sMoonTintPlay = NULL;
    sOdolwaUpdating = NULL;
    sGaroIniting = NULL;
    sGaroUpdating = NULL;
    if (sActive < 0) {
        sDoubled = false; // (a running doubled event stays doubled in the new area)
    }
    Wave2_ResetForPlayInit();
    sPlayReady = true;
}

RECOMP_HOOK("Play_Destroy") void Events_OnPlayDestroy(GameState* thisx) {
    // Leaving the scene: note a Nemesis you got away from (or an enemy that
    // wore you down) before everything is torn down.
    if (sPlayReady) {
        V2_OnRoomChange();
        Auto_Flush(); // owl saves and quitting happen right after this
    }
    sPlayReady = false;
    sInjectionCount = 0;
}

// Right before a room's object list is loaded, remove everything we spawned
// and take our object entries back out, so the game places its own objects
// correctly. The event sets itself up again in the new room.
RECOMP_HOOK("Scene_CommandObjectList") void Events_BeforeObjectList(PlayState* play, SceneCmd* cmd) {
    if (!sPlayReady) {
        return;
    }
    if (sActive >= 0) {
        Wave2_OnRoomChange(play);
    }
    V2_OnRoomChange();
    sRoomChanging = true;
    Ev_KillOurs(play);
    sRoomChanging = false;
    Ev_RestoreMoons(play);
    Obj_RemoveEx(play, false); // the room's object list is about to be rebuilt
    if (sActive >= 0) {
        sSceneReady = false;
    }
}

// ---------------------------------------------------------------------------
// New cycle / save load: the running event stops. Events stay switched on.
// A new cycle rolls a new curse; a new save file starts a fresh run report.
// ---------------------------------------------------------------------------

void Events_ResetForNewCycle(void) {
    sPendingCycleReset = true;
}

RECOMP_HOOK("Sram_SaveEndOfCycle") void Events_OnEndOfCycle(PlayState* play) {
    Events_ResetForNewCycle();
    V2_OnNewCycle();
}

RECOMP_CALLBACK("*", recomp_on_moon_crash) void Events_OnMoonCrash(void* sramCtx) {
    Events_ResetForNewCycle();
    V2_OnNewCycle();
}

RECOMP_CALLBACK("*", recomp_after_load_save) void Events_OnLoadSave(void* fileSelect, void* sramCtx) {
    Events_ResetForNewCycle();
    V2_OnLoadSave(false);
}

RECOMP_CALLBACK("*", recomp_after_init_save) void Events_OnInitSave(void* fileSelect, void* sramCtx) {
    Events_ResetForNewCycle();
    V2_OnLoadSave(true);
    Stats_Reset();
}

// ---------------------------------------------------------------------------
// Menu glue
// ---------------------------------------------------------------------------

void Events_Init(void) {
    sTagExt = z64recomp_extend_actor_all(sizeof(TagData));
    sTagExtValid = true;
}

static void Ev_FormatTime(char* out, s32 frames) {
    s32 seconds = (MAX(frames, 0) + FPS - 1) / FPS;
    s32 minutes = seconds / 60;

    seconds %= 60;
    if (minutes > 9) {
        out[0] = '0' + ((minutes / 10) % 10);
        out[1] = '0' + (minutes % 10);
        out += 2;
    } else {
        out[0] = '0' + minutes;
        out += 1;
    }
    out[0] = ':';
    out[1] = '0' + (seconds / 10);
    out[2] = '0' + (seconds % 10);
    out[3] = '\0';
}

static s32 Ev_Append(char* out, s32 n, const char* text, s32 maxLen) {
    s32 i;

    for (i = 0; (text[i] != '\0') && (n < maxLen - 1); i++) {
        out[n++] = text[i];
    }
    out[n] = '\0';
    return n;
}

void Events_StatusText(char* out, s32 maxLen) {
    char time[8];
    s32 n;

    time[0] = '\0';
    if (sActive >= 0) {
        n = Ev_Append(out, 0,
                      Curse_Is(CURSE_UNSEEN) ? "???" : Events_ShortName(sActive), // (full names don't fit with the clock)
                      maxLen);
        if (sCombo >= 0) {
            n = Ev_Append(out, n, "+", maxLen);
        } else if (Ev_IsDoubled()) {
            n = Ev_Append(out, n, " x2", maxLen);
        }
        if (Wave2_IsTimed()) {
            Ev_FormatTime(time, sFramesLeft); // untimed events (Nemesis, Pop Quiz...) show no clock
        }
    } else if (gOpt[ID_EV_MASTER] && (sPauseReason != 0)) {
        n = Ev_Append(out, 0,
                      (sPauseReason == 1) ? "Safe zone" : (sPauseReason == 2) ? "Waits for day" : "Waits for night",
                      maxLen);
    } else if (gOpt[ID_EV_MASTER] && Curse_Is(CURSE_BELL)) {
        n = Ev_Append(out, 0, "Next on the bell", maxLen);
    } else if (gOpt[ID_EV_MASTER]) {
        // The exact time is a secret (your friend can see it). Foresight shows what's coming.
        n = Ev_Append(out, 0, "Next: ", maxLen);
        n = Ev_Append(out, n, (sForesight && (sFriendNext >= 0)) ? Events_ShortName(sFriendNext) : "???", maxLen);
    } else {
        n = Ev_Append(out, 0, "Off", maxLen);
    }
    if (time[0] != '\0') {
        n = Ev_Append(out, n, " ", maxLen);
        Ev_Append(out, n, time, maxLen);
    }
}

// An event the moon sent, early on: skipping it costs Moon Marks (End Current Event),
// so nothing else ends it for free. (Free in the cheats build, or after 2 minutes.)
static s32 Ev_MoonSentRunning(void) {
#ifdef GE_CHEATS
    return false;
#else
    return (sActive >= 0) && V2_ON(ID_V2_BOUNTY) && !sEventWasCalled && !sEventByExp && (sEventFrames < 120 * FPS);
#endif
}

s32 Events_RunAction(PlayState* play, s32 id) {
    s32 i;

    if ((id >= ID_SUB_V2) && (id <= ID_V2_LAST)) {
        return V2_RunAction(play, id);
    }
    if (id == ID_EV_TRIGGER) {
        if (Ev_MoonSentRunning()) {
            Menu_ShowToast("Finish or end this event first.");
            return false;
        }
        if (Ev_PickRandom(play) >= 0) {
            if (sActive >= 0) {
                Ev_Abort(play);
            }
            sRouletteTimer = 0;
            sManualTrigger = true;
            sFreeRollPending = true; // started from the menu: no survival Marks
            Ev_BeginRandom(play);
            sManualTrigger = false;
            return true;
        }
        Menu_ShowToast("No event can run here.");
    } else if (id == ID_ALL_OFF) {
        // "Turn Off All Mods" also stops events (one the moon just sent still runs out).
        if (!Ev_MoonSentRunning()) {
            Ev_Abort(play);
        }
        sRouletteTimer = 0;
        gOpt[ID_EV_MASTER] = false;
    } else if (id == ID_EV_END) {
        // Skipping an event the moon sent costs Moon Marks (free if you started it, or after 2 minutes).
        if (Ev_MoonSentRunning()) {
            if (sMarks < END_PRICE) {
                Menu_ShowToast("Ending it costs 60 Moon Marks.");
                return false;
            }
            sMarks -= END_PRICE;
        }
        if ((sActive >= 0) || (sRouletteTimer > 0)) {
            Ev_Abort(play);
            sRouletteTimer = 0;
            Menu_ShowToast("Event ended.");
        } else {
            Menu_ShowToast("No event is running.");
        }
    } else if (id == ID_RR_RESET) {
        Stats_Reset();
        Menu_ShowToast("Run report cleared.");
    } else if ((id >= ID_EV_GO_FIRST) && (id < ID_EV_GO_FIRST + EV_COUNT)) {
        s32 ev = id - ID_EV_GO_FIRST;

        if (sBanMask & (1ull << ev)) {
            Menu_ShowToast("You banned it this cycle.");
            return false;
        }
        if (Ev_MoonSentRunning()) {
            Menu_ShowToast("Finish or end this event first.");
            return false;
        }
        if (!Ev_CanRun(play, ev)) {
            Menu_ShowToast((ev == EV_MOONFALL)      ? "Moonfall needs to be outdoors."
                       : (ev == EV_BLOOD_MOON) ? "No enemy for it here."
                                               : "It can't run here.");
            return false;
        }
        sRouletteTimer = 0;
        sCallPending = true; // started from the menu: no survival Marks
        Ev_Start(play, ev);
        sCallPending = false;
        return true;
    } else if ((id == ID_EV_ALL_ON) || (id == ID_EV_ALL_OFF)) {
        for (i = 0; i < EV_COUNT; i++) {
            gOpt[ID_EV_ON_FIRST + i] = (id == ID_EV_ALL_ON);
        }
        Menu_ShowToast((id == ID_EV_ALL_ON) ? "All events on." : "All events off.");
    } else if (id == ID_POOL_ALL_ON) {
        for (i = 0; i < POOL_COUNT; i++) {
            if (!POOL_IS_BOSS(i)) {
                gOpt[ID_POOL_FIRST + i] = true;
            }
        }
        Menu_ShowToast("All enemies on (bosses unchanged)");
    } else if (id == ID_POOL_ALL_OFF) {
        for (i = 0; i < POOL_COUNT; i++) {
            gOpt[ID_POOL_FIRST + i] = false;
        }
        Menu_ShowToast("All enemies off.");
    }
    return false;
}

void Events_OnOptionChanged(PlayState* play, s32 id) {
    if ((id == ID_V2_MUT) && !gOpt[ID_V2_MUT] && (sMutCount > 0)) {
        while (sMutCount > 0) {
            Pick_RemoveLast(); // turning the Moon Draft off drops your picks (and their Marks bonus)
        }
        Menu_ShowToast("Moon Draft off: picks dropped.");
    }
    if ((id == ID_V2_AUTOSAVE) && gOpt[ID_V2_AUTOSAVE] && (sAutoKey[0] == '\0')) {
        Menu_ShowToast("Auto-Save starts on your next load.");
    }
    if (id == ID_EV_MASTER) {
        if (gOpt[ID_EV_MASTER]) {
            sUntilNext = Ev_NextDelay();
            Menu_ShowToast("Global Events: ON");
        } else {
            if (!Ev_MoonSentRunning()) {
                Ev_Abort(play);
                Menu_ShowToast("Global Events: OFF");
            } else {
                Menu_ShowToast("Events off. This one runs out first.");
            }
            sRouletteTimer = 0;
        }
    } else if (id == ID_EV_INTERVAL) {
        sUntilNext = Ev_NextDelay();
    } else if (id == ID_EV_CURSE) {
        if (!gOpt[ID_EV_CURSE]) {
            sCurseCardPending = false;
            sCurseCardTimer = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// On-screen banner and countdown
// ---------------------------------------------------------------------------

static s32 Ev_ShowWarning(void) {
    if ((sFakeTimer > 0) && (sActive < 0) && (sRouletteTimer == 0) && (sCurseCardTimer == 0) &&
        !Curse_Is(CURSE_SILENT) && !Curse_Is(CURSE_BELL) && (sPauseReason == 0) && gOpt[ID_EV_MASTER]) {
        return true; // your friend's Fake Out (only where a real warning could show)
    }
    return !Curse_Is(CURSE_SILENT) && !Curse_Is(CURSE_BELL) && (sPauseReason == 0) && (sActive < 0) && gOpt[ID_EV_MASTER] && (sUntilNext > 0) && (sUntilNext <= 5 * FPS);
}

s32 Events_WantsDraw(PlayState* play) {
    if (IS_PAUSED(&play->pauseCtx) || (play->transitionMode != TRANS_MODE_OFF)) {
        return false;
    }
    if ((CutsceneManager_GetCurrentCsId() != CS_ID_NONE) && (CutsceneManager_GetCurrentCsId() != CS_ID_GLOBAL_TALK)) {
        return false;
    }
    return (sActive >= 0) || Ev_ShowWarning() || (sFateBanner > 0) || (sRouletteTimer > 0) || (sCurseCardTimer > 0) ||
           NextClock_Wanted() || V2_WantsDraw();
}

// Countdown box sits under the hearts and magic meter, pinned to the left
// edge like the rest of the HUD.
#define TIMER_X 18
#define TIMER_Y 74

static Gfx* Ev_BeginLeftAligned(Gfx* gfx) {
    gEXSetRectAlign(gfx++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gEXSetScissorAlign(gfx++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, -SCREEN_WIDTH, 0, 0, 0, SCREEN_WIDTH,
                       SCREEN_HEIGHT);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    return gfx;
}

static Gfx* Ev_EndAligned(Gfx* gfx) {
    gEXSetRectAlign(gfx++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gEXSetScissorAlign(gfx++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    return gfx;
}

// Treasure Hunt: how close you are, as a word and a color.
static const char* Treasure_Temperature(f32 dist, u8* r, u8* g, u8* b) {
    f32 t = CLAMP(1.0f - dist / MAX(sTreasureStartDist, 1.0f), 0.0f, 1.0f);

    *r = (u8)(60 + 195 * t);
    *g = (u8)(120 - 80 * t);
    *b = (u8)(255 - 225 * t);
    if (dist < 150.0f) {
        return "BURNING!";
    }
    if (dist < 350.0f) {
        return "Hot";
    }
    if (dist < 650.0f) {
        return "Warm";
    }
    if (dist < 1100.0f) {
        return "Cold";
    }
    return "Freezing";
}

// Treasure Hunt: which way to go, relative to the camera.
static const char* Treasure_Arrow(PlayState* play, Player* player) {
    static const char* sArrows[8] = { "^", "<^", "<", "<v", "v", "v>", ">", "^>" };
    Camera* cam = GET_ACTIVE_CAM(play);
    s16 rel;

    if (cam == NULL) {
        return "";
    }
    rel = Math_Vec3f_Yaw(&player->actor.world.pos, &sTreasurePos) - Camera_GetCamDirYaw(cam);
    return sArrows[((u16)(rel + 0x1000) >> 13) & 7];
}

// Second line under the countdown, for events that have more to say.
static s32 Ev_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b, f32* meter) {
    Player* player = GET_PLAYER(play);

    *meter = -1.0f;
    *r = *g = *b = 230;

    if (sPauseReason == 1) {
        Ev_Append(line, 0, "Paused here (safe zone)", size);
        *r = 150;
        *g = 220;
        *b = 255;
        return true;
    }

    if (false && EV_RUNNING(EV_TREASURE) && sTreasureValid && (player != NULL)) { // V3: no hints, just the rupee
        f32 dist = Math_Vec3f_DistXYZ(&player->actor.world.pos, &sTreasurePos);
        s32 n = Ev_Append(line, 0, Treasure_Temperature(dist, r, g, b), size);

        if (gOpt[ID_TH_ARROW]) {
            n = Ev_Append(line, n, "  ", size);
            Ev_Append(line, n, Treasure_Arrow(play, player), size);
        }
        *meter = CLAMP(1.0f - dist / MAX(sTreasureStartDist, 1.0f), 0.0f, 1.0f);
        return true;
    }
    if (EV_RUNNING(EV_HUNGER)) {
        char num[8];
        s32 k = sHungerKills;
        s32 n = 0;
        s32 i;

        // Small number to text.
        if (k > 999) {
            k = 999;
        }
        if (k >= 100) {
            num[n++] = '0' + (k / 100);
        }
        if (k >= 10) {
            num[n++] = '0' + ((k / 10) % 10);
        }
        num[n++] = '0' + (k % 10);
        num[n] = '\0';
        i = Ev_Append(line, 0, "Kills: ", size);
        Ev_Append(line, i, num, size);
        *r = 255;
        *g = 90;
        *b = 90;
        return true;
    }
    if (EV_RUNNING(EV_NEMESIS) && (sNemActor == NULL)) {
        Ev_Append(line, 0, Flags_GetClear(play, play->roomCtx.curRoom.num) ? "Lurking... move on" : "It's coming...",
                  size);
        *r = 255;
        *g = 120;
        *b = 120;
        return true;
    }
    if (EV_RUNNING(EV_CHAMPION) && (sNemActor == NULL)) {
        Ev_Append(line, 0,
                  Flags_GetClear(play, play->roomCtx.curRoom.num) ? "It waits elsewhere..." : "A challenger approaches",
                  size);
        *r = 255;
        *g = 210;
        *b = 90;
        return true;
    }
    if (EV_RUNNING(EV_INFIGHT)) {
        char num[4];
        s32 n;
        s32 a = MIN(Infight_CountTeam(play, 0), 9);
        s32 b2 = MIN(Infight_CountTeam(play, 1), 9);

        if ((sTeamPick[0] < 0) || (sTeamPick[1] < 0) || ((a == 0) && (b2 == 0))) {
            return false;
        }
        n = Ev_Append(line, 0, Pool_Name(sTeamPick[0]), size);
        num[0] = ' ';
        num[1] = '0' + a;
        num[2] = '\0';
        n = Ev_Append(line, n, num, size);
        n = Ev_Append(line, n, " vs ", size);
        num[0] = '0' + b2;
        num[1] = ' ';
        n = Ev_Append(line, n, num, size);
        Ev_Append(line, n, Pool_Name(sTeamPick[1]), size);
        *r = 255;
        *g = 200;
        *b = 120;
        return true;
    }
    if (Wave5_SubLine(play, line, size, r, g, b, meter)) {
        return true;
    }
    if (Wave4_SubLine(play, line, size, r, g, b)) {
        return true;
    }
    if (EV_RUNNING(EV_WOLFPACK)) {
        if (sWolfPhase == WOLF_CIRCLE) {
            Ev_Append(line, 0, "The pack circles...", size);
            *r = 200;
            *g = 200;
            *b = 255;
            return true;
        }
        if (sWolfBanner > 0) {
            Ev_Append(line, 0, "They attack!", size);
            *r = 255;
            *g = 80;
            *b = 80;
            return true;
        }
        return false;
    }
    // (3.1.9) No flavor/how-to line under the running event: just its name, timer and goals.
    return false;
}

static Gfx* Ev_DrawTimer(PlayState* play, Gfx* gfx) {
    GfxPrint printer;
    char line[40];
    char sub[40];
    char time[8];
    s32 n;
    s32 w;
    s32 barW;
    s32 hasSub = false;
    u8 subR, subG, subB;
    f32 meter = -1.0f;

    s32 compact = ((gOpt[ID_EV_TIMERSTYLE] % LVL_MAX) == LVL_MID);

    if (compact) {
        // Compact: just the clock (or a short name for untimed events), no bars.
        if (sActive < 0) {
            Ev_Append(line, 0, "Incoming...", sizeof(line));
        } else if ((sComboHalt & 1) && (sCombo >= 0)) {
            Ev_Append(line, 0, Events_ShortName(sCombo), sizeof(line));
        } else if (Wave2_IsTimed()) {
            Ev_FormatTime(line, sFramesLeft);
        } else {
            Ev_Append(line, 0, Curse_Is(CURSE_UNSEEN) ? "???" : Events_ShortName(sActive), sizeof(line));
        }
    } else if (EV_RUNNING(EV_NEMESIS) || EV_RUNNING(EV_CHAMPION)) {
        n = Ev_Append(line, 0, EV_RUNNING(EV_CHAMPION) ? "CHAMPION: " : "NEMESIS: ", sizeof(line));
        Ev_Append(line, n, (sNemPick >= 0) ? Pool_Name(sNemPick) : "???", sizeof(line));
    } else if ((sActive >= 0) && (sComboHalt & 1) && (sCombo >= 0)) {
        // The first half is done: the clock belongs to the half that's left.
        n = Ev_Append(line, 0, "Now: ", sizeof(line));
        n = Ev_Append(line, n, Events_ShortName(sCombo), sizeof(line));
        if (V3_IsLong(sCombo)) {
            n = Ev_Append(line, n, " ", sizeof(line));
            Ev_FormatTime(time, MAX(65 * FPS - sEventFrames, 0));
            Ev_Append(line, n, time, sizeof(line));
        }
    } else if (sActive >= 0) {
        Card_ComboName(line, sizeof(line), sActive, sCombo);
        n = Ui_StrLen(line);
        n = Ev_Append(line, n, " ", sizeof(line));
        Ev_FormatTime(time, sFramesLeft);
        Ev_Append(line, n, time, sizeof(line));
    } else {
        Ev_Append(line, 0, "Something is coming...", sizeof(line));
    }
    if (sActive >= 0) {
        hasSub = Ev_SubLine(play, sub, sizeof(sub), &subR, &subG, &subB, &meter);
    }
    w = Ui_StrLen(line) * 8 + 12;

    gfx = Ev_BeginLeftAligned(gfx);
    gfx = Ui_DrawRect(gfx, TIMER_X, TIMER_Y, TIMER_X + w, TIMER_Y + 20, 0, 0, 0, 170);
    if (compact) {
        // no bar
    } else if (EV_RUNNING(EV_NEMESIS) || EV_RUNNING(EV_CHAMPION)) {
        // Its health instead of a clock.
        barW = (sNemMaxHealth > 0) ? (w - 8) * CLAMP(sNemHealth, 0, sNemMaxHealth) / sNemMaxHealth : 0;
        gfx = Ui_DrawRect(gfx, TIMER_X + 4, TIMER_Y + 15, TIMER_X + 4 + (w - 8), TIMER_Y + 18, 60, 0, 0, 200);
        gfx = Ui_DrawRect(gfx, TIMER_X + 4, TIMER_Y + 15, TIMER_X + 4 + barW, TIMER_Y + 18, 255, 60, 20, 240);
    } else if ((sActive >= 0) && !(sComboHalt & 1)) {
        barW = (sTotalFrames > 0) ? (w - 8) * MAX(sFramesLeft, 0) / sTotalFrames : 0;
        gfx = Ui_DrawRect(gfx, TIMER_X + 4, TIMER_Y + 15, TIMER_X + 4 + barW, TIMER_Y + 18, 220, 30, 30, 230);
    }
    if (hasSub) {
        s32 sw = MAX(Ui_StrLen(sub) * 8 + 12, compact ? 20 : 100);

        gfx = Ui_DrawRect(gfx, TIMER_X, TIMER_Y + 22, TIMER_X + sw, TIMER_Y + 40, 0, 0, 0, 150);
        if ((meter >= 0.0f) && !compact) {
            gfx = Ui_DrawRect(gfx, TIMER_X + 4, TIMER_Y + 35, TIMER_X + 4 + (s32)((sw - 8) * meter), TIMER_Y + 38,
                              subR, subG, subB, 230);
        }
    }

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT); // crisp letters: no bleed from the next glyph when upscaled
    printer.flags |= (1 << 2); // text shadow
    if (sActive >= 0) {
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
    } else {
        GfxPrint_SetColor(&printer, 255, (play->gameplayFrames & 8) ? 80 : 200, 80, 255);
    }
    Ui_Print(&printer, TIMER_X + 6, TIMER_Y + 4, line);
    if (hasSub) {
        GfxPrint_SetColor(&printer, subR, subG, subB, 255);
        Ui_Print(&printer, TIMER_X + 6, TIMER_Y + 26, sub);
    }
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    return Ev_EndAligned(gfx);
}

// Big centered warning (Red Light, Green Light and Terrible Fate).
static Gfx* Ev_DrawCenterMessage(PlayState* play, Gfx* gfx) {
    GfxPrint printer;
    const char* text = NULL;
    u8 r = 255, g = 255, b = 255;
    s32 w;
    s32 x;
    s32 flash = (play->gameplayFrames & 4) != 0;

    if ((sActive >= 0) && (sPauseReason == 1)) {
        return gfx; // paused in a safe zone: no stale RED LIGHT or IN THE LIGHT
    }
    if (EV_RUNNING(EV_SEARCH) && (sSpotExposure > 0) && (sSpotAlert == 0)) {
        text = "IN THE LIGHT!";
        r = 255;
        g = flash ? 240 : 170;
        b = 90;
    } else if (sFateBanner > 0) {
        text = "A TERRIBLE FATE...";
        r = 255;
        g = flash ? 40 : 0;
        b = flash ? 40 : 0;
    } else if (EV_RUNNING(EV_REDLIGHT) || EV_RUNNING(EV_GLARE)) {
        s32 glare = EV_RUNNING(EV_GLARE);

        if (sRLZapFlash > 0) {
            text = glare ? "STRUCK!" : "ZAPPED!";
            r = glare ? 255 : 120;
            g = glare ? 140 : 200;
            b = glare ? 40 : 255;
        } else if (sRLPhase == RL_RED) {
            text = glare ? "DON'T MOVE" : "RED LIGHT";
            r = 255;
            g = flash ? 60 : 20;
            b = flash ? 60 : 20;
        } else if (sRLPhase == RL_WARN) {
            text = "...";
            r = 255;
            g = 220;
            b = 60;
        } else {
            text = glare ? NULL : "GREEN LIGHT";
            r = 60;
            g = 255;
            b = 90;
        }
    }
    if (text == NULL) {
        return gfx;
    }

    w = Ui_StrLen(text) * 8 + 16;
    x = (SCREEN_WIDTH - w) / 2;
    gfx = Ui_DrawRect(gfx, x, 46, x + w, 62, 0, 0, 0, 170);
    gfx = Ui_DrawRect(gfx, x, 61, x + w, 62, r, g, b, 230);

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT); // crisp letters: no bleed from the next glyph when upscaled
    printer.flags |= (1 << 2);
    GfxPrint_SetColor(&printer, r, g, b, 255);
    Ui_Print(&printer, x + 8, 50, text);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    return gfx;
}

static Gfx* NextClock_Draw(PlayState* play, Gfx* gfx) {
    GfxPrint printer;
    char line[24];
    s32 n;

    n = Ev_Append(line, 0, "Next: ", sizeof(line));
    Ev_Append(line, n, Events_ShortName(sFriendNext), sizeof(line));
    gfx = Ev_BeginLeftAligned(gfx);
    gfx = Ui_DrawRect(gfx, TIMER_X, TIMER_Y, TIMER_X + Ui_StrLen(line) * 8 + 12, TIMER_Y + 14, 0, 0, 0, 120);
    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2);
    GfxPrint_SetColor(&printer, 200, 200, 200, 230);
    Ui_Print(&printer, TIMER_X + 6, TIMER_Y + 3, line);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    return Ev_EndAligned(gfx);
}

void Events_Draw(PlayState* play, Gfx** gfxP) {
    Gfx* gfx = *gfxP;

    if (EV_RUNNING(EV_QUIZ) && (sBannerTimer <= 0)) {
        gfx = Quiz_Draw(play, gfx);
    } else if ((sActive >= 0) && (sBannerTimer > 0)) {
        // Title card for the first few seconds, then the countdown.
        gfx = Card_DrawEvent(play, gfx);
    } else if ((sActive < 0) && (sRouletteTimer > 0)) {
        gfx = Roulette_Draw(play, gfx);
    } else if ((sActive < 0) && (sCurseCardTimer > 0)) {
        gfx = Card_DrawCurse(play, gfx);
    } else if ((sActive >= 0) || Ev_ShowWarning()) {
        if ((gOpt[ID_EV_TIMERSTYLE] % LVL_MAX) != LVL_HIGH) { // Hidden: no timer box
            gfx = Ev_DrawTimer(play, gfx);
        }
        gfx = Ev_DrawCenterMessage(play, gfx);
    } else if (NextClock_Wanted()) {
        gfx = NextClock_Draw(play, gfx);
    } else if (sFateBanner > 0) {
        gfx = Ev_DrawCenterMessage(play, gfx);
    }
    if ((sActive >= 0) && (sBannerTimer <= 0)) {
        gfx = V3_Draw2D(play, gfx);
    }
    gfx = V2_Draw(play, gfx);
    *gfxP = gfx;
}
