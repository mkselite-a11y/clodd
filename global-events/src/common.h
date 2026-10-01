#ifndef CARPENTER_MENU_COMMON_H
#define CARPENTER_MENU_COMMON_H

// Bounty slots: 3 regular, 1 WANTED (Day 3), 1 rare (hunt streak).
#define V2_BOUNTY_SLOTS 6 // + 1 posted by your friend (Friend Link)

#include "modding.h"
#include "global.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "rt64_extended_gbi.h"

// ---------------------------------------------------------------------------
// Global events
// ---------------------------------------------------------------------------

typedef enum {
    EV_BOMB_RAIN,
    EV_RUPOOR_RAIN,
    EV_BLOOD_MOON,
    EV_JOKE,
    EV_MOONFALL,
    EV_STALKER,
    EV_POE_HUNT,
    EV_NEMESIS,
    EV_PHANTOM,
    EV_TREASURE,
    EV_HUNGER,
    EV_SEARCH,
    EV_FATE,
    EV_REDLIGHT,
    EV_ECHO,
    EV_GLARE,
    EV_KAMIKAZE,
    EV_WOLFPACK,
    EV_CHAMPION,
    EV_INFIGHT,
    EV_STILL,
    EV_IMPOSTER,
    EV_UPRISING,
    EV_PROCESSION,
    EV_POLTERGEIST,
    EV_BULLETHELL,
    EV_MIRAGE,
    EV_MIRROR,
    EV_SWARM,
    EV_MAJORA,
    EV_LEVIATHAN,
    // 1.1
    EV_TINGLE,
    EV_STAMPEDE,
    EV_CURSETAG,
    EV_QUIZ,
    EV_LOWGRAV,
    // 2.1
    EV_PREY,
    // V3: brought over from Moonfall Mayhem (ev_v3.inc)
    EV_STARFALL,
    EV_DOWSING,
    EV_GALE,
    EV_SINKING,
    EV_FROST,
    EV_SCYTHE,
    EV_MOTHS,
    EV_SILENCE,
    EV_TETHER,
    EV_GRASP,
    EV_HEARTBEAT,
    EV_REFLEX,
    EV_GORGON,
    EV_DISSONANCE,
    EV_MADNESS,
    EV_VOLLEY,
    EV_GIANT,
    EV_SIMON,
    EV_SEALS,
    EV_COUNT
} GlobalEventId;

#define POOL_COUNT 35 // Blood Moon enemy pool (last one is a boss)
#define POOL_FIRST_BOSS 34
#define CRIT_COUNT 8  // Joke Event critter types
#define GIVE_ITEM_COUNT 16 // cheat build: single items you can give yourself
#define GIVE_MASK_COUNT 24

// ---------------------------------------------------------------------------
// Menu entries. Every entry has an ID; toggles and multi-choice entries keep
// their current value in gOpt[id].
// ---------------------------------------------------------------------------

typedef enum {
    // Main menu
    ID_SUB_EVENTS,
    ID_SUB_PLAYER,
    ID_SUB_ITEMS,
    ID_SUB_WORLD,
    ID_SUB_FUN,
    ID_SUB_TELEPORT,
    ID_SUB_CARPENTER,
    ID_SUB_SETTINGS,
    ID_CLOSE,

    // Player
    ID_GOD,
    ID_MAGIC,
    ID_RUPEES,
    ID_MOONJUMP,
    ID_SPEED,
    ID_LOWGRAV,
    ID_REFILL,

    // Inventory
    ID_AMMO,
    ID_GIVE_ITEMS,
    ID_GIVE_MASKS,
    ID_GIVE_QUEST,
    ID_MAX_UPGRADES,
    ID_FAIRIES,

    // World
    ID_TIMESPEED,
    ID_TIME_DAWN,
    ID_TIME_NOON,
    ID_TIME_DUSK,
    ID_TIME_MIDNIGHT,
    ID_FREEZE_ENEMIES,
    ID_KILL_ENEMIES,
    ID_DISCO,

    // Fun
    ID_LINKSIZE,
    ID_BIGHEAD,
    ID_RAINBOWLINK,
    ID_BOMBRAIN,
    ID_RUPEERAIN,

    // Teleport
    ID_SAVEPOS,
    ID_LOADPOS,
    ID_WARP_CLOCKTOWN,
    ID_WARP_MILKROAD,
    ID_WARP_SWAMP,
    ID_WARP_WOODFALL,
    ID_WARP_MOUNTAIN,
    ID_WARP_SNOWHEAD,
    ID_WARP_GREATBAY,
    ID_WARP_ZORACAPE,
    ID_WARP_IKANA,
    ID_WARP_STONETOWER,

    // Carpenter
    ID_C_LOOP,
    ID_C_RAINBOW,
    ID_C_GLOW,
    ID_C_SPEED,
    ID_C_REVERSE,

    // Settings
    ID_THEME,
    ID_SOUNDS,
    ID_ALL_OFF,

    // Global events page
    ID_EV_MASTER,
    ID_EV_STATUS,
    ID_EV_TRIGGER,
    ID_EV_END,
    ID_EV_INTERVAL,
    ID_EV_DURATION,
    ID_EV_INTENSITY,
    ID_SUB_EV_LIST,
    ID_SUB_EV_START,
    ID_SUB_EV_SETTINGS,

    // Event settings page (one sub-page per event with options)
    ID_SUB_EV_POOL,
    ID_SUB_EV_CRITTERS,
    ID_SUB_EV_MOONFALL,
    ID_SUB_EV_NEMESIS,
    ID_SUB_EV_PHANTOM,
    ID_SUB_EV_TREASURE,
    ID_SUB_EV_HUNGER,
    ID_SUB_EV_SEARCH,
    ID_SUB_EV_FATE,
    ID_SUB_EV_REDLIGHT,
    ID_SUB_EV_ECHO,
    ID_SUB_EV_GLARE,
    ID_SUB_EV_KAMIKAZE,
    ID_SUB_EV_WOLFPACK,
    ID_SUB_EV_CHAMPION,
    ID_SUB_EV_INFIGHT,
    ID_SUB_EV_STILL,
    ID_SUB_EV_IMPOSTER,
    ID_SUB_EV_UPRISING,
    ID_SUB_EV_PROCESSION,
    ID_SUB_EV_POLTERGEIST,
    ID_SUB_EV_BULLETHELL,
    ID_SUB_EV_MIRAGE,
    ID_SUB_EV_MIRROR,
    ID_SUB_EV_SWARM,
    ID_SUB_EV_MAJORA,
    ID_SUB_EV_LEVIATHAN,

    // Per-event enable toggles, and "start this event now" actions
    ID_EV_ON_FIRST,
    ID_EV_GO_FIRST = ID_EV_ON_FIRST + EV_COUNT,

    // Blood Moon pool page
    ID_POOL_HORDE = ID_EV_GO_FIRST + EV_COUNT,
    ID_POOL_ALL_ON,
    ID_POOL_ALL_OFF,
    ID_POOL_FIRST,

    // Joke critters page
    ID_CRIT_AMOUNT = ID_POOL_FIRST + POOL_COUNT,
    ID_CRIT_FIRST,

    // Moonfall options page
    ID_MF_METEOR_SIZE = ID_CRIT_FIRST + CRIT_COUNT,
    ID_MF_SWOOP,
    ID_MF_MUSIC,
    ID_MF_QUAKE,

    // Nemesis
    ID_NEM_TYPE,
    ID_NEM_SIZE,
    ID_NEM_GLOW,

    // Phantom Army
    ID_PH_SIZE,
    ID_PH_SOLDIERS,
    ID_PH_SHIMMER,

    // Treasure Hunt
    ID_TH_DIST,
    ID_TH_TIME,
    ID_TH_ARROW,

    // Blood Hunger
    ID_BH_HEAL,
    ID_BH_ENEMIES,

    // Searchlights
    ID_SL_COUNT,
    ID_SL_SIZE,
    ID_SL_BACKUP,

    // Terrible Fate
    ID_TF_COUNT,
    ID_TF_SPEED,
    ID_TF_WALLS,

    // Red Light, Green Light
    ID_RL_REACT,
    ID_RL_GREEN,
    ID_RL_RED,

    // Echo
    ID_EC_DELAY,
    ID_EC_CLOSER,

    // Moon's Glare
    ID_MG_LENGTH,
    ID_MG_REACT,

    // Kamikaze Keese
    ID_KK_SWARM,
    ID_KK_SPEED,

    // Wolf Pack
    ID_WP_SIZE,
    ID_WP_WHITE,

    // Champion Duel
    ID_CD_WHO,
    ID_CD_HEALTH,
    ID_CD_FREEZE,

    // Infighting
    ID_IF_SIZE,

    // Choose Events page
    ID_EV_ALL_ON,
    ID_EV_ALL_OFF,

    // Cheat build: give single items / masks
    ID_SUB_GIVE_ITEM,
    ID_SUB_GIVE_MASK,
    ID_GIVEITEM_FIRST,
    ID_GIVEMASK_FIRST = ID_GIVEITEM_FIRST + GIVE_ITEM_COUNT,
    ID_GIVEMASK_LAST = ID_GIVEMASK_FIRST + GIVE_MASK_COUNT - 1,

    // Fourth wave
    ID_ST_COUNT,
    ID_ST_STILL,
    ID_IM_CROWD,
    ID_IM_SHARE,
    ID_CU_COUNT,
    ID_CU_HITS,
    ID_PR_LENGTH,
    ID_PG_ROCKS,
    ID_BL_RING,
    ID_BL_FAN,
    ID_MI_GROUPS,
    ID_MI_FAKES,
    ID_MD_COUNT,
    ID_SN_COUNT,
    ID_MJ_SIZE,
    ID_LV_SIZE,
    // 1.1: event features, run report and three new events
    ID_EV_CURSE_INFO,
    ID_SUB_EV_FEATURES,
    ID_SUB_EV_REPORT,
    ID_EV_ROULETTE,
    ID_EV_COMBOS,
    ID_EV_CURSE,
    ID_EV_PRESSURE,
    ID_RR_SEEN,
    ID_RR_SURVIVED,
    ID_RR_DEATHS,
    ID_RR_KILLER,
    ID_RR_KILLER_NAME,
    ID_RR_COMBOS,
    ID_RR_RESET,
    ID_RR_EV_FIRST,
    ID_RR_EV_LAST = ID_RR_EV_FIRST + EV_COUNT - 1,
    ID_SUB_EV_TINGLE,
    ID_SUB_EV_STAMPEDE,
    ID_SUB_EV_CURSETAG,
    ID_TA_RATE,
    ID_TA_BOMBS,
    ID_GS_LANES,
    ID_GS_SPEED,
    ID_CT_FUSE,
    ID_CT_ENEMIES,
    // 1.6 features
    ID_EV_SAFEZONES,
    ID_EV_TIMERSTYLE,
    ID_EV_NEXTCLOCK,
    ID_EV_WARNSOUND,
    ID_EV_STREAKS,
    ID_EV_WRATH,
    ID_EV_HOURS,
    ID_EV_MERCY,
    ID_EV_WILD,
    ID_SUB_EV_LOG,
    ID_RR_STREAK,
    ID_RR_BEST,
    ID_RR_WRATH,
    ID_LOG_FIRST,
    ID_LOG_LAST = ID_LOG_FIRST + 9,
    // 2.0: Moon Marks, Nemesis, Friend Link
    ID_SUB_V2,
    ID_SUB_V2_BOUNTY,
    ID_SUB_V2_SHOP,
    ID_SUB_V2_BAN,
    ID_SUB_V2_CALL,
    ID_SUB_V2_NEM,
    ID_SUB_V2_MUTS,
    ID_SUB_V2_ITEMS,
    ID_SUB_V2_SERVICES,
    ID_SUB_V2_LINK, // Friend Link page
    ID_V2_MUT,
    ID_V2_BOUNTY,
    ID_V2_NEM,
    ID_V2_POUCH,
    ID_V2_NEMDIFF,
    ID_V2_MARKS,
    ID_V2_SAVECODE,
    ID_V2_AUTOSAVE, // save Moon progress with each game file
    ID_V2_DRAFT_A,
    ID_V2_DRAFT_B,
    ID_V2_DRAFT_REROLL, // draft screen: reroll both offers for Marks
    ID_V2_TRACKER,
    ID_V2_STREAK,
    ID_V2_RANK,
    ID_V2_REMOTE,
    ID_V2_LINK,      // Friend Link on/off
    ID_V2_LINKSTATE, // connection info row
    ID_V2_NEWS,      // V3: world news pop-ups
    ID_V2_BANNERS,   // V3: big banners (off: they show as pop-ups)
    ID_V2_BNT_FIRST,
    ID_V2_CON_FIRST = ID_V2_BNT_FIRST + V2_BOUNTY_SLOTS,
    ID_V2_SHOP_FIRST = ID_V2_CON_FIRST + 3,
    ID_V2_BAN_FIRST = ID_V2_SHOP_FIRST + 32, // pouch items + services
    ID_V2_CALL_FIRST = ID_V2_BAN_FIRST + EV_COUNT,
    ID_V2_NEMROW_FIRST = ID_V2_CALL_FIRST + EV_COUNT,
    ID_V2_MUTROW_FIRST = ID_V2_NEMROW_FIRST + 12,
    ID_V2_BINFO_FIRST = ID_V2_MUTROW_FIRST + 6, // bounty details: target, stars, place, moves in, reward
    ID_V2_BTRAIT_FIRST = ID_V2_BINFO_FIRST + 5, // one row per trait (16)
    ID_V2_BACCEPT = ID_V2_BTRAIT_FIRST + 16,
    ID_V2_BELITE = ID_V2_BACCEPT + 1, // WANTED-only trait
    ID_V2_BBONUS = ID_V2_BELITE + 1, // bounty bonus objective
    ID_V2_BUPGRADE = ID_V2_BBONUS + 1, // bounty details: buy a star
    // V3 menu: Overview rows and the event details page
    ID_OV_NEM,
    ID_OV_BNT_FIRST,
    ID_OV_CONTRACTS = ID_OV_BNT_FIRST + 3,
    ID_OV_MUTS,
    ID_OV_FRIENDS,
    ID_EVD_TIER,
    ID_EVD_HOW,
    // V3 Friend Link: one row per friend (Friends tab)
    ID_LNK_FR_FIRST,
    ID_LNK_FR_LAST = ID_LNK_FR_FIRST + 11,
    ID_V2_LAST = ID_LNK_FR_LAST,
    ID_MAX
} MenuEntryId;

// Menu pages store entry IDs as u16.
_Static_assert(ID_MAX <= 2000, "too many menu entries");

// Values for multi-choice entries.
typedef enum { TIMESPEED_NORMAL, TIMESPEED_FROZEN, TIMESPEED_FAST, TIMESPEED_MAX } TimeSpeedMode;
typedef enum { LINKSIZE_NORMAL, LINKSIZE_GIANT, LINKSIZE_TINY, LINKSIZE_MAX } LinkSizeMode;
typedef enum { CSPEED_1X, CSPEED_2X, CSPEED_4X, CSPEED_MAX } CarpenterSpeedMode;
typedef enum { THEME_RAINBOW, THEME_RED, THEME_BLUE, THEME_GREEN, THEME_GOLD, THEME_MAX } ThemeMode;
typedef enum {
    INTERVAL_1, INTERVAL_2, INTERVAL_3, INTERVAL_4, INTERVAL_5, INTERVAL_6, INTERVAL_7,
    INTERVAL_8, INTERVAL_9, INTERVAL_10, INTERVAL_15, INTERVAL_20, INTERVAL_25, INTERVAL_30, INTERVAL_MAX
} EventIntervalMode;
typedef enum { DURATION_15S, DURATION_30S, DURATION_1M, DURATION_2M, DURATION_3M, DURATION_4M, DURATION_5M, DURATION_MAX } EventDurationMode;
typedef enum { INTENSITY_MILD, INTENSITY_NORMAL, INTENSITY_BRUTAL, INTENSITY_MAX } EventIntensityMode;
typedef enum { HORDE_10, HORDE_20, HORDE_30, HORDE_40, HORDE_MAX } HordeSizeMode;
typedef enum { CRITAMT_30, CRITAMT_60, CRITAMT_100, CRITAMT_MAX } CritterAmountMode;
typedef enum { METEOR_SMALL, METEOR_MEDIUM, METEOR_LARGE, METEOR_MAX } MeteorSizeMode;
typedef enum { NEMTYPE_ANY, NEMTYPE_REGULAR, NEMTYPE_MINIBOSS, NEMTYPE_MAX } NemesisTypeMode;
typedef enum { SOLDIERS_STALCHILD, SOLDIERS_MIXED, SOLDIERS_MAX } PhantomSoldierMode;
typedef enum { CHAMP_RANDOM, CHAMP_IRON_KNUCKLE, CHAMP_DINOLFOS, CHAMP_GARO, CHAMP_MAX } ChampionMode;
// Generic low/medium/high choice used by many event settings (names differ per entry).
typedef enum { LVL_LOW, LVL_MID, LVL_HIGH, LVL_MAX } Level3Mode;
// Generic four-step choice (names differ per entry).
typedef enum { STEP_1, STEP_2, STEP_3, STEP_4, STEP_MAX } Step4Mode;

extern u8 gOpt[ID_MAX];

// Shared helpers
void HueToRgb(s32 hue, u8* r, u8* g, u8* b);
s32 BaseHue(PlayState* play);

// Menu (menu.c)
extern s32 gMenuOpen;
void Menu_Open(PlayState* play);
void Menu_ShowToast(const char* text);
// V3 pop-ups: kinds pick the colour. Banners fall back to a pop-up when turned off.
typedef enum { NOTE_PLAIN, NOTE_MARKS, NOTE_FRIEND, NOTE_WARN, NOTE_NEMESIS, NOTE_NEWS } NoteKind;
void Menu_Notify(const char* text, s32 kind);
void Menu_ShowBanner(const char* title, const char* sub);
// Friend Link (ev_remote.inc): friends listed on the Friends tab.
s32 Link_FriendRows(void);
void Menu_PlaySfx(u16 sfxId);
Gfx* Ui_DrawRect(Gfx* gfx, s32 x1, s32 y1, s32 x2, s32 y2, u8 r, u8 g, u8 b, u8 a);
void Ui_Print(GfxPrint* printer, s32 x, s32 y, const char* text);
s32 Ui_StrLen(const char* s);

// Mods (mods.c)
void Mods_RunAction(PlayState* play, s32 id);
void Mods_OnOptionChanged(PlayState* play, s32 id);
void Mods_TurnAllOff(PlayState* play);
void Mods_RequestWarp(u16 entrance);
s32 Mods_WarpPending(void);
void Mods_OnPlayInit(PlayState* play);
void Mods_OnPlayUpdate(PlayState* play);

// Global events (events.c)
void Events_Init(void);
void Events_SetDefaults(void);
void Events_OnPlayInit(PlayState* play);
void Events_Update(PlayState* play);
void Events_Draw(PlayState* play, Gfx** gfxP);
s32 Events_RunAction(PlayState* play, s32 id); // returns true if the menu should close
s32 Events_WantsDraw(PlayState* play);
void Events_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should);
s32 Events_KillAllEnemies(PlayState* play);
void Events_OnOptionChanged(PlayState* play, s32 id);
void Events_StatusText(char* out, s32 maxLen);
const char* Events_Name(s32 ev);
const char* Events_StartLabel(s32 ev);
const char* Pool_Name(s32 index);
const char* Critter_Name(s32 index);
void Events_ResetForNewCycle(void);
s32 Events_IsEventActor(Actor* actor);      // spawned by an event
s32 Events_IsHidden(s32 ev);                // retired events (not offered anywhere)
const char* Give_ItemName(s32 index);
const char* Give_MaskName(s32 index);
s32 Events_IsRainbowCarpenter(Actor* actor); // Carpenter Uprising
void Events_AfterHealthChange(void);
s32 Events_EnabledCount(s32* total);
f32 Events_LowGravity(void);
// 2.0
extern s32 gMenuAtBoard;                    // menu opened at the rainbow carpenter (the Moon Shop works)
void Menu_OpenV2(PlayState* play, s32 which);
s32 Carpenter_BoardSpot(Vec3f* out, s16* yaw); // 0: Moon Marks page, 1: mutator draft
const char* V2_Label(s32 id);
extern s32 gV2InfoBounty;
extern s32 gEvDetail; // V3: the event on the details page
s32 V2_NemStatValue(s32 k); // V3: menu stat bars (-1: none)
s32 V2_RowUnaffordable(s32 id); // V3: shop price in red
s32 V2_BountyActive(s32 i);
s32 V2_BountyElite(s32 i);
s32 V2_BountyBonus(s32 i);
extern s32 gFriendLinkSize; // Friend Link: temporary Link size (LinkSizeMode), 0 = none
s32 V2_BountyUpgradable(s32 i);
s32 V2_ShopItemCount(void);
s32 V2_ShopServiceCount(void);
s32 V2_ServiceHidden(s32 sv);
u16 V2_BountyTraits(s32 i);
u16 V2_NemTraitMask(void);
const char* V2_Value(s32 id, char* out, s32 maxLen, u8* r, u8* g, u8* b);
const char* V2_Desc(s32 id); // footer text for a Moon Marks / Nemesis / Friend Link row
const char* Events_ShortName(s32 ev);       // fits a run report row
// Text for an info row (status, curse, run report). Returns the value text.
const char* Events_InfoText(s32 id, char* out, s32 maxLen, u8* r, u8* g, u8* b);
const char* Events_InfoLabel(s32 id);       // label override for info rows, or NULL
const char* Events_InfoDesc(s32 id);        // footer text while the row is highlighted, or NULL
void Events_CycleCurse(s32 dir);            // cheat build: pick the curse by hand

// Carpenter (carpenter.c)
void Carpenter_OnOptionChanged(PlayState* play, s32 id);
void Carpenter_OnPlayInit(PlayState* play);
Actor* Carpenter_RainbowActor(void);

#endif
