#ifndef MF_H
#define MF_H

#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "z64recomp_api.h"
#include "gfxalloc.h"
#include "z64quake.h"
#include "libu64/gfxprint.h"

// ---------------------------------------------------------------------------
// General
// ---------------------------------------------------------------------------

#define FPS 20 // MM gameplay logic runs at 20 updates per second
#define SEC(x) ((s32)((x) * FPS))
#define MF_SAVE_VERSION 3

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MF_CLAMP(v, lo, hi) (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
#define MF_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MF_MAX(a, b) (((a) > (b)) ? (a) : (b))

// ---------------------------------------------------------------------------
// Native library (moonfall_net.dll)
// ---------------------------------------------------------------------------

RECOMP_IMPORT(".", s32 mfn_init(void));
RECOMP_IMPORT(".", void mfn_configure(const char* url, const char* room, const char* key, s32 enabled));
RECOMP_IMPORT(".", void mfn_push_state(const char* json));
RECOMP_IMPORT(".", s32 mfn_poll_command(char* out, s32 maxLen));
RECOMP_IMPORT(".", s32 mfn_status(void));
RECOMP_IMPORT(".", s32 mfn_save_blob(const char* path, void* words, s32 numWords));
RECOMP_IMPORT(".", s32 mfn_load_blob(const char* path, void* words, s32 numWords));
RECOMP_IMPORT(".", void mfn_log(const char* msg));

// ---------------------------------------------------------------------------
// Enemy roster (roster.c)
// ---------------------------------------------------------------------------

#define SPF_ROAM (1 << 0)       // Can walk around and chase: eligible to become a nemesis
#define SPF_FLY (1 << 1)        // Airborne: spawn a little above the floor
#define SPF_STATIONARY (1 << 2) // Rooted in place: bounty-only
#define SPF_NIGHT (1 << 3)      // Flavor: prefers the night

typedef struct {
    const char* name;
    s16 actorId;
    s16 objectId;
    s16 params;
    u8 difficulty; // 1 (trivial) .. 10 (brutal); drives rewards and stat scaling
    u8 flags;
    s16 spawnYOffset;
} Species;

extern const Species gSpecies[];
extern const s32 gSpeciesCount;
s32 Roster_FindByActor(s16 actorId, s16 params);
s32 Roster_RandomRoaming(s32 maxDifficulty);
s32 Roster_Random(s32 minDifficulty, s32 maxDifficulty);

// ---------------------------------------------------------------------------
// World graph (world.c)
// ---------------------------------------------------------------------------

typedef struct {
    const char* name;
    s16 scenes[2]; // scene ids that map to this zone (seasonal variants)
    u8 links[10];  // neighbouring zone indices, 0xFF terminated
    u8 danger;     // 0 = town, higher = wilder
} Zone;

#define ZONE_NONE 0xFF
extern const Zone gZones[];
extern const s32 gZoneCount;
s32 World_ZoneForScene(s16 sceneId);
s32 World_NextStepToward(s32 from, s32 to);
s32 World_Distance(s32 from, s32 to);
s32 World_RandomNeighbour(s32 zone);
void World_Tick(PlayState* play);
void World_Log(const char* text);
void World_SetRumor(s32 on);
void World_Logf2(const char* a, const char* b, const char* c);

// ---------------------------------------------------------------------------
// Persistent save data (save.c). Everything here is stored as u32 words.
// ---------------------------------------------------------------------------

#define NUM_EVENTS 32
#define NUM_CURSES 10
#define NUM_TRAITS 20
#define NUM_POUCH_ITEMS 24
#define MAX_BOUNTIES 8
#define MAX_CLAIMED 3
#define LOG_LINES 20
#define LOG_WIDTH 60

typedef struct {
    u8 enabled;
    u8 weight; // relative frequency 0..10
    u8 p[3];   // event-specific parameters (see event table)
    u8 pad[3];
} EventSettings;

typedef enum {
    STAT_HEALTH,
    STAT_POWER,
    STAT_TRACKING,
    STAT_SPEED,
    STAT_MAX
} NemesisStat;

typedef enum {
    NEM_NONE,
    NEM_HUNTING, // travelling toward the player
    NEM_DORMANT, // licking its wounds; returns later
} NemesisState;

typedef struct {
    u8 state;
    u8 species;
    u8 level;
    u8 zone;
    u8 stats[STAT_MAX];
    u32 traits;
    u8 nameA;
    u8 nameB;
    u8 scarred;
    u8 promotionReason;
    u16 kills;      // times it killed the player
    u16 encounters; // times it manifested near the player
    u16 escapes;    // times the player fled from it
    u16 dormantSecs;
    u16 travelSecs;
    u8 hpPercent; // wounds carried between encounters
    u8 friendBuffs;
    u16 stolenMarks; // Thief trait: paid back double when slain
} NemesisRec;

typedef enum {
    MOTIVE_ROAM,
    MOTIVE_MIGRATE,
    MOTIVE_NEST,
    MOTIVE_HUNT,
    MOTIVE_FLEE,
    MOTIVE_MAX
} BountyMotive;

typedef struct {
    u8 active;
    u8 species;
    u8 rank; // 0=D .. 4=S
    u8 zone;
    u8 claimed;
    u8 motive;
    u8 targetZone;
    u8 sponsored;
    u16 reward;
    u16 expireMins;
    u32 traits;
    u8 nameA;
    u8 nameB;
    u8 hpPercent;
    u8 pad;
    char sponsor[12];
} BountyRec;

typedef struct {
    u8 eventsEnabled;
    u8 nemesisEnabled;
    u8 bountiesEnabled;
    u8 friendsAllowed;
    u16 eventIntervalSecs;
    u8 eventJitterSecs;
    u8 eventMinSecs;
    u8 eventMaxSecs;
    u8 promotionChance; // percent
    u8 allowDungeons;
    u8 crossZoneObjects;
    u16 objectBudgetKB;
    u8 hudBanners;
    u8 friendHurtful; // allow friends to trigger dangerous events
    u8 friendCooldownSecs;
    u8 difficultyPct; // 50..200, on top of the master difficulty
    u8 notifyRumors;
    u8 pad[3];
} GlobalSettings;

typedef struct {
    u32 magic;
    u32 version;
    u32 moonMarks;
    u32 lifetimeMarks;
    u16 curseId;
    u16 curseCycle; // threeDayResetCount the curse belongs to
    u8 curseWarded; // Curse Ward active until the next dawn
    u8 wardDay;
    u8 pouchSel;
    u8 markMagnet; // remaining boosted payouts
    u8 pouch[NUM_POUCH_ITEMS];
    EventSettings events[NUM_EVENTS];
    GlobalSettings settings;
    NemesisRec nemesis;
    BountyRec bounties[MAX_BOUNTIES];
    u16 eventsSurvived;
    u16 eventsFailed;
    u16 bountiesKilled;
    u16 nemesesSlain;
    u16 deaths;
    u16 worldSecs;
    u16 spyglassCycle;
    u16 pad2;
    u32 rngState;
    char log[LOG_LINES][LOG_WIDTH];
    u8 logHead;
    u8 logCount;
    u8 pad3[2];
} SaveData;

extern SaveData gMf;
void Save_Defaults(void);
void Save_MarkDirty(void);
void Save_Update(PlayState* play);
void Save_LoadForCurrentFile(void);
void Save_FlushNow(void);

// ---------------------------------------------------------------------------
// Runtime state (main.c)
// ---------------------------------------------------------------------------

typedef struct {
    PlayState* play;
    s32 frame;
    s16 sceneId;
    s16 prevSceneId;
    s32 zone;      // current zone or ZONE_NONE if indoors
    s32 lastZone;  // last overworld zone the player stood in
    u8 sceneChanged;
    u8 menuOpen;
    u8 gameplayOk; // normal gameplay: not paused, no cutscene, no textbox
    u8 playerDead;
    s16 lastHealth;
    s16 damageThisFrame; // health lost this frame (positive)
    Actor* lastAttacker;
    s32 framesSinceSceneStart;
    u16 threeDayCount;
    u8 loaded;
    u8 difficultyIdx; // master difficulty from the config
    u8 debugMenu;
    u8 pad;
    // Input captured before the game sees it
    u16 btnCur;
    u16 btnPress;
    s8 stickX;
    s8 stickY;
    u8 suppressGameInput;
    u8 lHeldCombo; // L was used for a pouch combo during this hold
} Runtime;

extern Runtime gRt;

// ---------------------------------------------------------------------------
// Util (util.c)
// ---------------------------------------------------------------------------

u32 Rng_Next(void);
s32 Rng_Range(s32 lo, s32 hi); // inclusive
f32 Rng_Float(void);
s32 Rng_Chance(s32 percent);

s32 Str_Len(const char* s);
void Str_Copy(char* dst, const char* src, s32 max);
void Str_Cat(char* dst, const char* src, s32 max);
void Str_CatInt(char* dst, s32 value, s32 max);
void Str_CatHex(char* dst, u32 value, s32 max);
s32 Str_ParseInt(const char** s);
s32 Str_StartsWith(const char* s, const char* prefix);

Player* Mf_Player(void);
void Mf_DamagePlayer(s32 quarterHearts, s32 knockback, s16 fromYaw);
void Mf_HealPlayer(s32 quarterHearts);
void Mf_ShockPlayer(s32 quarterHearts);
void Mf_BurnPlayer(void);
f32 Mf_FloorY(Vec3f* pos, f32 fallback);
s32 Mf_FindSpawnPoint(PlayState* play, Vec3f* out, f32 minDist, f32 maxDist, s16 preferYaw);
s16 Mf_YawTo(Vec3f* from, Vec3f* to);
f32 Mf_DistXZ(Vec3f* a, Vec3f* b);
s32 Mf_IsOutdoors(PlayState* play);
s32 Mf_HasWater(PlayState* play);
s32 Mf_PlayerGrounded(void);
f32 Mf_StickMag(void);
s16 Mf_StickWorldYaw(PlayState* play);
s32 Mf_DifficultyScale(s32 value); // scales damage by master difficulty + settings
void Mf_Sfx(u16 sfxId);

// ---------------------------------------------------------------------------
// Drawing (draw.c)
// ---------------------------------------------------------------------------

typedef struct {
    u8 r, g, b, a;
} MfColor;

void Draw2D_Begin(GraphicsContext* gfxCtx);
void Draw2D_Rect(s32 x, s32 y, s32 w, s32 h, MfColor c);
void Draw2D_Text(s32 x, s32 y, MfColor c, const char* text); // pixel position
void Draw2D_TextCentered(s32 y, MfColor c, const char* text);
void Draw2D_End(void);
void Draw2D_WorldMarker(PlayState* play, Vec3f* pos, MfColor c, const char* label);

void Draw3D_Begin(PlayState* play);
void Draw3D_Disk(Vec3f* pos, f32 radius, MfColor c);
void Draw3D_Ring(Vec3f* pos, f32 radius, f32 thickness, MfColor c);
void Draw3D_Orb(Vec3f* pos, f32 radius, MfColor c);
void Draw3D_Pillar(Vec3f* pos, f32 radius, f32 height, MfColor c);
void Draw3D_Wedge(Vec3f* pos, f32 radius, s16 yaw, s16 halfAngle, MfColor c);
void Draw3D_End(void);
MfColor Mf_Rainbow(s32 t, u8 alpha);

// ---------------------------------------------------------------------------
// HUD (hud.c)
// ---------------------------------------------------------------------------

void Hud_Notify(const char* text, MfColor c);          // queued toast at the top
void Hud_Banner(const char* title, const char* sub);   // big centered banner
void Hud_SetPrompt(const char* text);                  // one-frame prompt near the bottom
void Hud_Draw(PlayState* play);
void Hud_Update(void);

// ---------------------------------------------------------------------------
// Events (events.c)
// ---------------------------------------------------------------------------

typedef enum {
    EVK_HARMLESS,
    EVK_BENEFIT,
    EVK_DANGER,
} EventKind;

typedef struct EventDef {
    const char* name;
    const char* counter;   // one-line instruction shown to the player
    const char* flavor;    // chronicle / tooltip text
    u8 kind;
    u8 danger;             // 0..5 : drives the Moon Mark payout
    u8 defaults[3];        // default parameter values
    const char* paramNames[3];
    u8 paramMax[3];
    s32 (*eligible)(PlayState* play);
    void (*start)(PlayState* play);
    void (*update)(PlayState* play);
    void (*draw3d)(PlayState* play);
    void (*draw2d)(PlayState* play);
    void (*end)(PlayState* play, s32 success);
} EventDef;

extern const EventDef gEvents[NUM_EVENTS];

typedef struct {
    s8 id; // -1 when idle
    u8 forced;
    u8 failed;     // set by the event when the player failed the counter
    u8 succeeded;  // set by the event to end early with success
    s32 timer;     // frames elapsed
    s32 duration;  // frames
    s32 nextIn;    // frames until the next event
    s32 hits;      // generic counters for event logic
    s32 score;
    f32 fa, fb;
    Vec3f origin;
    s32 friendBonus;
    s8 omenId;     // pre-rolled next event (Omen Scroll can reveal it)
    u8 wasOk;
    s16 notesAtStart;
    char byFriend[12];
} EventRuntime;

extern EventRuntime gEv;
void Events_Update(PlayState* play);
void Events_Draw3D(PlayState* play);
void Events_Draw2D(PlayState* play);
void Events_Start(PlayState* play, s32 id, s32 forced, const char* byFriend);
void Events_Stop(PlayState* play, s32 success);
void Events_OnDeath(PlayState* play);
void Events_OnSceneChange(PlayState* play);
s32 Events_Param(s32 id, s32 idx);
void Events_ResetSettings(void);
s32 Events_RandomEligible(PlayState* play, s32 dangerOnly);

// ---------------------------------------------------------------------------
// Curses (curses.c)
// ---------------------------------------------------------------------------

typedef enum {
    CURSE_HASTE,     // events come faster
    CURSE_FRAILTY,   // event damage up
    CURSE_VENDETTA,  // nemesis tracks faster and grows faster
    CURSE_HUNTED,    // bounty targets hunt you
    CURSE_POVERTY,   // Moon Mark income down, prices up
    CURSE_PROMOTION, // enemies promote to nemesis more easily
    CURSE_CROWD,     // friends get cheaper, faster meddling
    CURSE_SEALED,    // Moon Pouch items have a cooldown
    CURSE_LONGNIGHT, // events last longer
    CURSE_BRITTLE,   // claimed bounties expire faster and flee
} CurseId;

typedef struct {
    const char* name;
    const char* system;
    const char* desc;
} CurseDef;

extern const CurseDef gCurses[NUM_CURSES];
s32 Curse_Active(s32 id);
void Curse_Update(PlayState* play);
void Curse_Roll(s32 announce);

// ---------------------------------------------------------------------------
// Moon Marks & Pouch (pouch.c)
// ---------------------------------------------------------------------------

typedef struct {
    const char* name;
    const char* desc;
    u16 cost;
    u8 maxStack;
    u8 category;
} PouchItemDef;

extern const PouchItemDef gPouchItems[NUM_POUCH_ITEMS];
void Marks_Add(s32 amount, const char* reason);
s32 Marks_Spend(s32 amount);
s32 Marks_Scale(s32 amount); // curse + magnet adjustments
s32 Pouch_Price(s32 item);
s32 Pouch_Buy(s32 item);
void Pouch_Update(PlayState* play);
void Pouch_DrawHud(PlayState* play);
s32 Pouch_Use(PlayState* play, s32 item);
void Pouch_Give(s32 item, s32 count);
extern s32 gBuffTimers[NUM_POUCH_ITEMS];
s32 Pouch_BuffActive(s32 item);

enum {
    PI_LUNAR_TONIC,
    PI_FULL_MOON_DRAUGHT,
    PI_MOONSHIELD,
    PI_STILLWATER_CHARM,
    PI_OMEN_SCROLL,
    PI_NEMESIS_LURE,
    PI_SMOKE_VEIL,
    PI_BOUNTY_COMPASS,
    PI_HUNTERS_WHETSTONE,
    PI_MOONSTEEL_WARD,
    PI_SECOND_WIND,
    PI_MAGIC_FLASK,
    PI_QUIVER_REFILL,
    PI_BOMB_SATCHEL,
    PI_DEKU_BUNDLE,
    PI_CURSE_WARD,
    PI_MARK_MAGNET,
    PI_CHRONICLE_SPYGLASS,
    PI_STASIS_ORB,
    PI_RIPOSTE_CHARM,
    PI_EVENT_TEMPTER,
    PI_NEMESIS_BRAND,
    PI_BOUNTY_REROLL,
    PI_HEART_SIPHON,
};

// ---------------------------------------------------------------------------
// Tagged actors (actors.c)
// ---------------------------------------------------------------------------

typedef enum {
    TAG_NONE,
    TAG_NEMESIS,
    TAG_BOUNTY,
    TAG_EVENT, // spawned by an event (ambush)
    TAG_MINION,
} ActorTagKind;

typedef struct {
    u8 kind;
    u8 index;   // bounty slot, event slot, etc.
    u8 applied; // stats applied after init
    u8 species;
    u8 lastHealth;
    u8 maxHealth;
    u8 hitsOnPlayer;
    u8 damagedByPlayer;
    s16 regenTimer;
    s16 blinkTimer;
    s32 aliveFrames;
    s32 aggroFrames;
    u8 fleeing;
    u8 pad[3];
} ActorTag;

ActorTag* Tag_Get(Actor* actor);
Actor* Actors_SpawnSpecies(PlayState* play, s32 species, Vec3f* pos, s16 yaw, u8 kind, u8 index);
s32 Actors_SpeciesObjectReady(PlayState* play, s32 species);
void Actors_OnInit(void);
void Actors_PreloadForScene(PlayState* play);
void Actors_Update(PlayState* play);
s32 Actors_CountTagged(u8 kind, s32 index);
Actor* Actors_FindTagged(u8 kind, s32 index);
void Actors_KillTagged(u8 kind);
s32 Actors_IsEnemyLike(Actor* a);
s32 Actors_TakeFledSpecies(void);
extern char gPreloadNote[64];

// ---------------------------------------------------------------------------
// Nemesis (nemesis.c)
// ---------------------------------------------------------------------------

typedef struct {
    const char* name;
    const char* desc;
} TraitDef;

extern const TraitDef gTraits[NUM_TRAITS];

enum {
    TR_IRONHIDE,
    TR_REGENERATING,
    TR_BERSERKER,
    TR_VAMPIRIC,
    TR_VENOMOUS,
    TR_FROSTBRAND,
    TR_STORMCALLER,
    TR_PYROMANIAC,
    TR_BLINKER,
    TR_AMBUSHER,
    TR_RELENTLESS,
    TR_THIEF,
    TR_MARK_HUNGRY,
    TR_MOONBLESSED,
    TR_GIANT,
    TR_SWIFT,
    TR_HEXCALLER,
    TR_COWARD,
    TR_WARLORD,
    TR_GRUDGE,
};

#define HAS_TRAIT(mask, t) (((mask) >> (t)) & 1)

void Nemesis_Name(char* out, s32 max, u8 a, u8 b, s32 species);
void Nemesis_Update(PlayState* play);
void Nemesis_OnSceneChange(PlayState* play);
void Nemesis_OnPlayerDeath(PlayState* play, Actor* killer);
void Nemesis_OnPlayerHit(PlayState* play, Actor* attacker, s32 damage);
void Nemesis_Promote(PlayState* play, s32 species, s32 reason, Actor* actor);
void Nemesis_OnTaggedKilled(PlayState* play, Actor* actor, ActorTag* tag);
void Nemesis_ApplyStats(Actor* actor, ActorTag* tag);
void Nemesis_ActorUpdate(PlayState* play, Actor* actor, ActorTag* tag);
void Nemesis_WorldTick(PlayState* play);
void Nemesis_Summon(PlayState* play);
void Nemesis_AddStat(s32 stat, s32 amount);
void Nemesis_GainTrait(void);
extern u8 gPromoteNextHit;
extern s32 gChillTimer;
extern s32 gPoisonTimer;
s32 Nemesis_FilterDamage(Actor* actor, ActorTag* tag, s32 drop);
u32 Tagged_Traits(ActorTag* tag);
s32 Tagged_Stat(ActorTag* tag, s32 stat);
void Tagged_Name(ActorTag* tag, char* out, s32 max);
extern const char* gPromotionReasons[];

// ---------------------------------------------------------------------------
// Bounties (bounty.c)
// ---------------------------------------------------------------------------

extern const char* gRankNames[5];
void Bounty_Update(PlayState* play);
void Bounty_OnSceneChange(PlayState* play);
void Bounty_DrawHud(PlayState* play);
const char* Bounty_MotiveName(s32 m);
void Bounty_WorldTick(PlayState* play);
void Bounty_Refresh(s32 keepClaimed);
void Bounty_Generate(BountyRec* b, s32 zoneHint);
void Bounty_Name(char* out, s32 max, BountyRec* b);
void Bounty_OnTaggedKilled(PlayState* play, Actor* actor, ActorTag* tag);
void Bounty_ApplyStats(Actor* actor, ActorTag* tag);
s32 Bounty_ClaimedCount(void);
s32 Bounty_Add(s32 species, s32 zone, s32 reward, const char* sponsor);
extern s32 gCompassTimer;

// ---------------------------------------------------------------------------
// Menu (menu.c) and town interactables (town.c)
// ---------------------------------------------------------------------------

void Menu_Open(s32 page);
void Menu_Close(void);
void Menu_Update(PlayState* play);
void Menu_Draw(PlayState* play);

enum {
    PAGE_MAIN,
    PAGE_BOUNTY,
    PAGE_SHOP,
};

void Town_Update(PlayState* play);

// ---------------------------------------------------------------------------
// Ocarina (ocarina.c)
// ---------------------------------------------------------------------------

void Ocarina_Update(PlayState* play);
extern s32 gOcarinaNotesPlayed; // counts notes, events can watch it
void Ocarina_Close(PlayState* play);

// ---------------------------------------------------------------------------
// Friends (friends.c)
// ---------------------------------------------------------------------------

void Friends_Init(void);
void Friends_Update(PlayState* play);
s32 Friends_Status(void);
extern char gFriendsLastMsg[64];

#endif
