#include "common.h"

// ---------------------------------------------------------------------------
// The actual mods behind each menu entry.
// ---------------------------------------------------------------------------

#define LOW_GRAVITY_FACTOR 0.6f // fraction of gravity cancelled
#define MOON_JUMP_VELOCITY 10.0f
#define GIANT_SCALE 3.0f
#define TINY_SCALE 0.5f
#define BIG_HEAD_SCALE 2.5f
#define RAINBOW_FOG_FAR 1600


static char sToastBuf[48];

static void Toast_Set(const char* label, const char* value) {
    s32 n = 0;
    s32 i;

    for (i = 0; (label[i] != '\0') && (n < 30); i++) {
        sToastBuf[n++] = label[i];
    }
    if (value != NULL) {
        sToastBuf[n++] = ':';
        sToastBuf[n++] = ' ';
        for (i = 0; (value[i] != '\0') && (n < 46); i++) {
            sToastBuf[n++] = value[i];
        }
    }
    sToastBuf[n] = '\0';
    Menu_ShowToast(sToastBuf);
}

// ---------------------------------------------------------------------------
// Time speed
// ---------------------------------------------------------------------------

static s32 sSavedTimeOffset = 0;
static s32 sTimeOffsetSaved = false;

static s32 sForcedTimeOffset = 0x7FFF; // what the cheat last set (to tell it apart from a song)

static void Time_Apply(void) {
    switch (gOpt[ID_TIMESPEED]) {
        case TIMESPEED_FROZEN:
            gSaveContext.save.timeSpeedOffset = -R_TIME_SPEED;
            sForcedTimeOffset = gSaveContext.save.timeSpeedOffset;
            break;
        case TIMESPEED_FAST:
            gSaveContext.save.timeSpeedOffset = 15;
            sForcedTimeOffset = 15;
            break;
        default:
            break;
    }
}

static void Time_OnChanged(void) {
    if (gOpt[ID_TIMESPEED] != TIMESPEED_NORMAL) {
        if (!sTimeOffsetSaved) {
            sSavedTimeOffset = gSaveContext.save.timeSpeedOffset;
            sTimeOffsetSaved = true;
        }
        Time_Apply();
    } else if (sTimeOffsetSaved) {
        // Only undo our own change: if a song set the speed since, keep that.
        if (gSaveContext.save.timeSpeedOffset == sForcedTimeOffset) {
            gSaveContext.save.timeSpeedOffset = sSavedTimeOffset;
        }
        sTimeOffsetSaved = false;
        sForcedTimeOffset = 0x7FFF;
    }
}

// ---------------------------------------------------------------------------
// Disco lighting
// ---------------------------------------------------------------------------

static void Disco_Clear(PlayState* play) {
    s32 i;

    for (i = 0; i < 3; i++) {
        play->envCtx.adjLightSettings.ambientColor[i] = 0;
        play->envCtx.adjLightSettings.light1Color[i] = 0;
        play->envCtx.adjLightSettings.fogColor[i] = 0;
    }
}

static void Disco_Update(PlayState* play) {
    u8 rgb[3];
    s32 i;

    HueToRgb(BaseHue(play) * 3, &rgb[0], &rgb[1], &rgb[2]);
    for (i = 0; i < 3; i++) {
        s16 shift = (s16)((rgb[i] - 128) * 6 / 10);

        play->envCtx.adjLightSettings.ambientColor[i] = shift;
        play->envCtx.adjLightSettings.light1Color[i] = shift;
        play->envCtx.adjLightSettings.fogColor[i] = shift / 2;
    }
}

// ---------------------------------------------------------------------------
// Option changes
// ---------------------------------------------------------------------------

void Mods_OnOptionChanged(PlayState* play, s32 id) {
    const char* label = NULL;

    switch (id) {
        case ID_TIMESPEED:
            Time_OnChanged();
            Toast_Set("Time Speed",
                      (gOpt[id] == TIMESPEED_FROZEN) ? "Frozen" : (gOpt[id] == TIMESPEED_FAST) ? "Fast" : "Normal");
            return;
        case ID_LINKSIZE:
            Toast_Set("Link Size",
                      (gOpt[id] == LINKSIZE_GIANT) ? "Giant" : (gOpt[id] == LINKSIZE_TINY) ? "Tiny" : "Normal");
            return;
        case ID_C_SPEED:
            Toast_Set("Carpenter Speed", (gOpt[id] == CSPEED_1X) ? "1x" : (gOpt[id] == CSPEED_4X) ? "4x" : "2x");
            return;
        case ID_THEME:
            Toast_Set("Menu Color Changed", NULL);
            return;
        case ID_DISCO:
            if (!gOpt[id]) {
                Disco_Clear(play);
            }
            label = "Disco Lights";
            break;
        case ID_GOD:
            label = "God Mode";
            break;
        case ID_MAGIC:
            label = "Infinite Magic";
            break;
        case ID_RUPEES:
            label = "Infinite Rupees";
            break;
        case ID_MOONJUMP:
            label = "Moon Jump";
            break;
        case ID_SPEED:
            label = "Super Speed";
            break;
        case ID_LOWGRAV:
            label = "Low Gravity";
            break;
        case ID_AMMO:
            label = "Infinite Ammo";
            break;
        case ID_FREEZE_ENEMIES:
            label = "Freeze Enemies";
            break;
        case ID_BIGHEAD:
            label = "Big Head Mode";
            break;
        case ID_RAINBOWLINK:
            label = "Rainbow Link";
            break;
        case ID_BOMBRAIN:
            label = "Bomb Rain";
            break;
        case ID_RUPEERAIN:
            label = "Rupee Rain";
            break;
        case ID_C_LOOP:
            label = "Tower Loop";
            break;
        case ID_C_RAINBOW:
            label = "Carpenter Rainbow";
            break;
        case ID_C_GLOW:
            label = "Carpenter Glow";
            break;
        case ID_C_REVERSE:
            label = "Reverse Loop";
            break;
        case ID_SOUNDS:
            label = "Menu Sounds";
            break;
        default:
            break;
    }

    if (label != NULL) {
        Toast_Set(label, gOpt[id] ? "ON" : "OFF");
    }
}

void Mods_TurnAllOff(PlayState* play) {
    s32 hadDisco = gOpt[ID_DISCO];

    gOpt[ID_GOD] = gOpt[ID_MAGIC] = gOpt[ID_RUPEES] = gOpt[ID_MOONJUMP] = false;
    gOpt[ID_SPEED] = gOpt[ID_LOWGRAV] = gOpt[ID_AMMO] = gOpt[ID_FREEZE_ENEMIES] = false;
    gOpt[ID_DISCO] = gOpt[ID_BIGHEAD] = gOpt[ID_RAINBOWLINK] = false;
    gOpt[ID_BOMBRAIN] = gOpt[ID_RUPEERAIN] = false;
    gOpt[ID_LINKSIZE] = LINKSIZE_NORMAL;
    gOpt[ID_TIMESPEED] = TIMESPEED_NORMAL;
    Time_OnChanged();
    if (hadDisco) {
        Disco_Clear(play);
    }
}

// ---------------------------------------------------------------------------
// One-shot actions
// ---------------------------------------------------------------------------

static void Give_AllItems(void) {
    if (CUR_UPG_VALUE(UPG_QUIVER) == 0) {
        Inventory_ChangeUpgrade(UPG_QUIVER, 1);
    }
    if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 0) {
        Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
    }
    if (CUR_UPG_VALUE(UPG_DEKU_STICKS) == 0) {
        Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 1);
    }
    if (CUR_UPG_VALUE(UPG_DEKU_NUTS) == 0) {
        Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 1);
    }

    INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_OCARINA_OF_TIME;
    INV_CONTENT(ITEM_BOW) = ITEM_BOW;
    INV_CONTENT(ITEM_ARROW_FIRE) = ITEM_ARROW_FIRE;
    INV_CONTENT(ITEM_ARROW_ICE) = ITEM_ARROW_ICE;
    INV_CONTENT(ITEM_ARROW_LIGHT) = ITEM_ARROW_LIGHT;
    INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
    INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
    INV_CONTENT(ITEM_DEKU_STICK) = ITEM_DEKU_STICK;
    INV_CONTENT(ITEM_DEKU_NUT) = ITEM_DEKU_NUT;
    INV_CONTENT(ITEM_MAGIC_BEANS) = ITEM_MAGIC_BEANS;
    INV_CONTENT(ITEM_POWDER_KEG) = ITEM_POWDER_KEG;
    INV_CONTENT(ITEM_PICTOGRAPH_BOX) = ITEM_PICTOGRAPH_BOX;
    INV_CONTENT(ITEM_LENS_OF_TRUTH) = ITEM_LENS_OF_TRUTH;
    INV_CONTENT(ITEM_HOOKSHOT) = ITEM_HOOKSHOT;
    INV_CONTENT(ITEM_SWORD_GREAT_FAIRY) = ITEM_SWORD_GREAT_FAIRY;

    AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
    AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
    AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
    AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
    AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
    AMMO(ITEM_MAGIC_BEANS) = 20;
    AMMO(ITEM_POWDER_KEG) = 1;
}

// Single items for the cheat build's "Give Item" page.
static const u8 sGiveItems[GIVE_ITEM_COUNT] = {
    ITEM_OCARINA_OF_TIME, ITEM_BOW,         ITEM_ARROW_FIRE,       ITEM_ARROW_ICE,
    ITEM_ARROW_LIGHT,     ITEM_BOMB,        ITEM_BOMBCHU,          ITEM_DEKU_STICK,
    ITEM_DEKU_NUT,        ITEM_MAGIC_BEANS, ITEM_POWDER_KEG,       ITEM_PICTOGRAPH_BOX,
    ITEM_LENS_OF_TRUTH,   ITEM_HOOKSHOT,    ITEM_SWORD_GREAT_FAIRY, ITEM_BOTTLE,
};

const char* Give_ItemName(s32 index) {
    static const char* sNames[GIVE_ITEM_COUNT] = {
        "Ocarina of Time", "Hero's Bow",  "Fire Arrow",      "Ice Arrow",
        "Light Arrow",     "Bombs",       "Bombchus",        "Deku Sticks",
        "Deku Nuts",       "Magic Beans", "Powder Keg",      "Pictograph Box",
        "Lens of Truth",   "Hookshot",    "Great Fairy Sword", "Empty Bottle",
    };

    return ((index >= 0) && (index < GIVE_ITEM_COUNT)) ? sNames[index] : "???";
}

const char* Give_MaskName(s32 index) {
    static const char* sNames[GIVE_MASK_COUNT] = {
        "Deku Mask",      "Goron Mask",     "Zora Mask",       "Fierce Deity",   "Mask of Truth",
        "Kafei's Mask",   "All-Night Mask", "Bunny Hood",      "Keaton Mask",    "Garo's Mask",
        "Romani's Mask",  "Circus Leader",  "Postman's Hat",   "Couple's Mask",  "Great Fairy Mask",
        "Gibdo Mask",     "Don Gero's Mask", "Kamaro's Mask",  "Captain's Hat",  "Stone Mask",
        "Bremen Mask",    "Blast Mask",     "Mask of Scents",  "Giant's Mask",
    };

    return ((index >= 0) && (index < GIVE_MASK_COUNT)) ? sNames[index] : "???";
}

static void Give_OneItem(u8 item) {
    s32 i;

    switch (item) {
        case ITEM_BOW:
        case ITEM_ARROW_FIRE:
        case ITEM_ARROW_ICE:
        case ITEM_ARROW_LIGHT:
            if (CUR_UPG_VALUE(UPG_QUIVER) == 0) {
                Inventory_ChangeUpgrade(UPG_QUIVER, 1);
            }
            INV_CONTENT(ITEM_BOW) = ITEM_BOW;
            AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
            INV_CONTENT(item) = item;
            break;
        case ITEM_BOMB:
        case ITEM_BOMBCHU:
            if (CUR_UPG_VALUE(UPG_BOMB_BAG) == 0) {
                Inventory_ChangeUpgrade(UPG_BOMB_BAG, 1);
            }
            INV_CONTENT(item) = item;
            AMMO(item) = CUR_CAPACITY(UPG_BOMB_BAG);
            break;
        case ITEM_DEKU_STICK:
            if (CUR_UPG_VALUE(UPG_DEKU_STICKS) == 0) {
                Inventory_ChangeUpgrade(UPG_DEKU_STICKS, 1);
            }
            INV_CONTENT(item) = item;
            AMMO(item) = CUR_CAPACITY(UPG_DEKU_STICKS);
            break;
        case ITEM_DEKU_NUT:
            if (CUR_UPG_VALUE(UPG_DEKU_NUTS) == 0) {
                Inventory_ChangeUpgrade(UPG_DEKU_NUTS, 1);
            }
            INV_CONTENT(item) = item;
            AMMO(item) = CUR_CAPACITY(UPG_DEKU_NUTS);
            break;
        case ITEM_MAGIC_BEANS:
            INV_CONTENT(item) = item;
            AMMO(item) = 20;
            break;
        case ITEM_POWDER_KEG:
            INV_CONTENT(item) = item;
            AMMO(item) = 1;
            break;
        case ITEM_BOTTLE:
            // Into the first empty bottle slot.
            for (i = SLOT_BOTTLE_1; i <= SLOT_BOTTLE_6; i++) {
                if (gSaveContext.save.saveInfo.inventory.items[i] == ITEM_NONE) {
                    gSaveContext.save.saveInfo.inventory.items[i] = ITEM_BOTTLE;
                    break;
                }
            }
            break;
        default:
            INV_CONTENT(item) = item;
            break;
    }
}

static char sGiveToast[40];

static void Give_Toast(const char* name) {
    s32 n = 0;
    s32 i;
    const char* head = "Got: ";

    for (i = 0; head[i] != '\0'; i++) {
        sGiveToast[n++] = head[i];
    }
    for (i = 0; (name[i] != '\0') && (n < (s32)sizeof(sGiveToast) - 1); i++) {
        sGiveToast[n++] = name[i];
    }
    sGiveToast[n] = '\0';
    Menu_ShowToast(sGiveToast);
}

static void Give_AllMasks(void) {
    s32 item;

    for (item = ITEM_MASK_DEKU; item <= ITEM_MASK_GIANT; item++) {
        INV_CONTENT(item) = item;
    }
}

static void Give_SongsAndRemains(void) {
    static const u8 sQuestItems[] = {
        QUEST_SONG_SONATA,   QUEST_SONG_LULLABY, QUEST_SONG_BOSSA_NOVA, QUEST_SONG_ELEGY,
        QUEST_SONG_OATH,     QUEST_SONG_TIME,    QUEST_SONG_HEALING,    QUEST_SONG_EPONA,
        QUEST_SONG_SOARING,  QUEST_SONG_STORMS,  QUEST_REMAINS_ODOLWA,  QUEST_REMAINS_GOHT,
        QUEST_REMAINS_GYORG, QUEST_REMAINS_TWINMOLD, QUEST_BOMBERS_NOTEBOOK,
    };
    s32 i;

    INV_CONTENT(ITEM_OCARINA_OF_TIME) = ITEM_OCARINA_OF_TIME;
    for (i = 0; i < ARRAY_COUNT(sQuestItems); i++) {
        SET_QUEST_ITEM(sQuestItems[i]);
    }
}

static void Give_MaxUpgrades(PlayState* play) {
    Player* player = GET_PLAYER(play);

    Inventory_ChangeUpgrade(UPG_QUIVER, 3);
    Inventory_ChangeUpgrade(UPG_BOMB_BAG, 3);
    Inventory_ChangeUpgrade(UPG_WALLET, 2);
    INV_CONTENT(ITEM_BOW) = ITEM_BOW;
    INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
    AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
    AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);

    // Gilded Sword on B and the Mirror Shield.
    SET_EQUIP_VALUE(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_GILDED);
    BUTTON_ITEM_EQUIP(0, EQUIP_SLOT_B) = ITEM_SWORD_GILDED;
    if (gSaveContext.save.playerForm == PLAYER_FORM_HUMAN) {
        Interface_LoadItemIconImpl(play, EQUIP_SLOT_B);
    }
    SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
    if (player != NULL) {
        Player_SetEquipmentData(play, player);
    }

    // 20 hearts, double defense, double magic.
    gSaveContext.save.saveInfo.playerData.healthCapacity = 20 * 0x10;
    gSaveContext.save.saveInfo.playerData.health = 20 * 0x10;
    gSaveContext.save.saveInfo.playerData.doubleDefense = true;
    gSaveContext.save.saveInfo.inventory.defenseHearts = 20;
    gSaveContext.save.saveInfo.playerData.isMagicAcquired = true;
    gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired = true;
    gSaveContext.save.saveInfo.playerData.magicLevel = 0; // makes the game re-read the meter size
    gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
    gSaveContext.save.saveInfo.playerData.magic = MAGIC_DOUBLE_METER;
}

static void Give_FairyBottles(PlayState* play) {
    s32 i;

    for (i = SLOT_BOTTLE_1; i <= SLOT_BOTTLE_6; i++) {
        gSaveContext.save.saveInfo.inventory.items[i] = ITEM_FAIRY;
    }

    // Update any bottle already on a C button.
    for (i = EQUIP_SLOT_C_LEFT; i <= EQUIP_SLOT_C_RIGHT; i++) {
        s32 slot = C_SLOT_EQUIP(0, i);

        if ((slot >= SLOT_BOTTLE_1) && (slot <= SLOT_BOTTLE_6)) {
            BUTTON_ITEM_EQUIP(0, i) = ITEM_FAIRY;
            if (gSaveContext.save.playerForm == PLAYER_FORM_HUMAN) {
                Interface_LoadItemIconImpl(play, i);
            }
        }
    }
}

static void Kill_AllEnemies(PlayState* play) {
    // Uses the events code's safe removal (gives back a stolen shield, etc.).
    s32 count = Events_KillAllEnemies(play);

    Toast_Set(count > 0 ? "Enemies Eliminated" : "No Enemies Nearby", NULL);
}

// Save / load position (same area and room only).
static s32 sSavedPosValid = false;
static s16 sSavedScene;
static s8 sSavedRoom;
static Vec3f sSavedPos;
static s16 sSavedYaw;

static void Position_Save(PlayState* play) {
    Player* player = GET_PLAYER(play);

    sSavedPosValid = true;
    sSavedScene = play->sceneId;
    sSavedRoom = play->roomCtx.curRoom.num;
    sSavedPos = player->actor.world.pos;
    sSavedYaw = player->actor.shape.rot.y;
    Toast_Set("Position Saved", NULL);
}

static void Position_Load(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (!sSavedPosValid || (sSavedScene != play->sceneId) || (sSavedRoom != play->roomCtx.curRoom.num)) {
        Toast_Set("No Saved Position Here", NULL);
        Menu_PlaySfx(NA_SE_SY_ERROR);
        return;
    }
    player->actor.world.pos = sSavedPos;
    player->actor.prevPos = sSavedPos;
    player->actor.velocity.x = player->actor.velocity.y = player->actor.velocity.z = 0.0f;
    player->actor.shape.rot.y = player->actor.world.rot.y = sSavedYaw;
    player->yaw = sSavedYaw;
    Toast_Set("Position Loaded", NULL);
}

// Warps go through the same entrances as the Song of Soaring.
static u16 sPendingWarp = 0;
static s32 sWarpPending = false;

s32 Mods_WarpPending(void) {
    return sWarpPending;
}

void Mods_RequestWarp(u16 entrance) {
    sPendingWarp = entrance;
    sWarpPending = true;
}

static void Warp_Start(PlayState* play) {
    u16 entrance = sPendingWarp;

    sWarpPending = false;
    if ((entrance == ENTRANCE(SOUTHERN_SWAMP_POISONED, 10)) && CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_WOODFALL_TEMPLE)) {
        entrance = ENTRANCE(SOUTHERN_SWAMP_CLEARED, 10);
    } else if ((entrance == ENTRANCE(MOUNTAIN_VILLAGE_WINTER, 8)) &&
               CHECK_WEEKEVENTREG(WEEKEVENTREG_CLEARED_SNOWHEAD_TEMPLE)) {
        entrance = ENTRANCE(MOUNTAIN_VILLAGE_SPRING, 8);
    }

    play->nextEntrance = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
}

void Mods_RunAction(PlayState* play, s32 id) {
    if ((id >= ID_GIVEITEM_FIRST) && (id < ID_GIVEITEM_FIRST + GIVE_ITEM_COUNT)) {
        Give_OneItem(sGiveItems[id - ID_GIVEITEM_FIRST]);
        Give_Toast(Give_ItemName(id - ID_GIVEITEM_FIRST));
        return;
    }
    if ((id >= ID_GIVEMASK_FIRST) && (id <= ID_GIVEMASK_LAST)) {
        s32 mask = ITEM_MASK_DEKU + (id - ID_GIVEMASK_FIRST);

        INV_CONTENT(mask) = mask;
        Give_Toast(Give_MaskName(id - ID_GIVEMASK_FIRST));
        return;
    }
    switch (id) {
        case ID_REFILL:
            gSaveContext.save.saveInfo.playerData.health = gSaveContext.save.saveInfo.playerData.healthCapacity;
            if (gSaveContext.save.saveInfo.playerData.isMagicAcquired) {
                gSaveContext.save.saveInfo.playerData.magic = gSaveContext.magicCapacity;
            }
            Toast_Set("Health and Magic Refilled", NULL);
            break;

        case ID_GIVE_ITEMS:
            Give_AllItems();
            Toast_Set("All Items Added", NULL);
            break;
        case ID_GIVE_MASKS:
            Give_AllMasks();
            Toast_Set("All 24 Masks Added", NULL);
            break;
        case ID_GIVE_QUEST:
            Give_SongsAndRemains();
            Toast_Set("Songs and Remains Added", NULL);
            break;
        case ID_MAX_UPGRADES:
            Give_MaxUpgrades(play);
            Toast_Set("Maxed Out", NULL);
            break;
        case ID_FAIRIES:
            Give_FairyBottles(play);
            Toast_Set("6 Fairy Bottles", NULL);
            break;

        case ID_TIME_DAWN:
            gSaveContext.save.time = CLOCK_TIME(6, 30);
            Toast_Set("Time Set: 6:30 AM", NULL);
            break;
        case ID_TIME_NOON:
            gSaveContext.save.time = CLOCK_TIME(12, 0);
            Toast_Set("Time Set: Noon", NULL);
            break;
        case ID_TIME_DUSK:
            gSaveContext.save.time = CLOCK_TIME(18, 30);
            Toast_Set("Time Set: 6:30 PM", NULL);
            break;
        case ID_TIME_MIDNIGHT:
            gSaveContext.save.time = CLOCK_TIME(0, 0);
            Toast_Set("Time Set: Midnight", NULL);
            break;

        case ID_KILL_ENEMIES:
            Kill_AllEnemies(play);
            break;

        case ID_SAVEPOS:
            Position_Save(play);
            break;
        case ID_LOADPOS:
            Position_Load(play);
            break;

        case ID_WARP_CLOCKTOWN:
            Mods_RequestWarp(ENTRANCE(SOUTH_CLOCK_TOWN, 9));
            break;
        case ID_WARP_MILKROAD:
            Mods_RequestWarp(ENTRANCE(MILK_ROAD, 4));
            break;
        case ID_WARP_SWAMP:
            Mods_RequestWarp(ENTRANCE(SOUTHERN_SWAMP_POISONED, 10));
            break;
        case ID_WARP_WOODFALL:
            Mods_RequestWarp(ENTRANCE(WOODFALL, 4));
            break;
        case ID_WARP_MOUNTAIN:
            Mods_RequestWarp(ENTRANCE(MOUNTAIN_VILLAGE_WINTER, 8));
            break;
        case ID_WARP_SNOWHEAD:
            Mods_RequestWarp(ENTRANCE(SNOWHEAD, 3));
            break;
        case ID_WARP_GREATBAY:
            Mods_RequestWarp(ENTRANCE(GREAT_BAY_COAST, 11));
            break;
        case ID_WARP_ZORACAPE:
            Mods_RequestWarp(ENTRANCE(ZORA_CAPE, 6));
            break;
        case ID_WARP_IKANA:
            Mods_RequestWarp(ENTRANCE(IKANA_CANYON, 4));
            break;
        case ID_WARP_STONETOWER:
            Mods_RequestWarp(ENTRANCE(STONE_TOWER, 3));
            break;

        case ID_ALL_OFF:
            Mods_TurnAllOff(play);
#ifdef GE_CHEATS
            Toast_Set("All Mods Turned Off", NULL);
#else
            Toast_Set("Events turned off.", NULL);
#endif
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Per-frame effects (run right before the game's update each frame)
// ---------------------------------------------------------------------------

static s32 sLHeld = false;
static u32 sFrameCounter = 0;

static void Ammo_Refill(void) {
    if ((INV_CONTENT(ITEM_BOW) == ITEM_BOW) && (CUR_UPG_VALUE(UPG_QUIVER) != 0)) {
        AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
    }
    if ((INV_CONTENT(ITEM_BOMB) == ITEM_BOMB) && (CUR_UPG_VALUE(UPG_BOMB_BAG) != 0)) {
        AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
    }
    if ((INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) && (CUR_UPG_VALUE(UPG_BOMB_BAG) != 0)) {
        AMMO(ITEM_BOMBCHU) = CUR_CAPACITY(UPG_BOMB_BAG);
    }
    if ((INV_CONTENT(ITEM_DEKU_STICK) == ITEM_DEKU_STICK) && (CUR_UPG_VALUE(UPG_DEKU_STICKS) != 0)) {
        AMMO(ITEM_DEKU_STICK) = CUR_CAPACITY(UPG_DEKU_STICKS);
    }
    if ((INV_CONTENT(ITEM_DEKU_NUT) == ITEM_DEKU_NUT) && (CUR_UPG_VALUE(UPG_DEKU_NUTS) != 0)) {
        AMMO(ITEM_DEKU_NUT) = CUR_CAPACITY(UPG_DEKU_NUTS);
    }
    if (INV_CONTENT(ITEM_MAGIC_BEANS) == ITEM_MAGIC_BEANS) {
        AMMO(ITEM_MAGIC_BEANS) = 20;
    }
    if (INV_CONTENT(ITEM_POWDER_KEG) == ITEM_POWDER_KEG) {
        AMMO(ITEM_POWDER_KEG) = 1;
    }
}

static void Rain_Update(PlayState* play, Player* player) {
    Vec3f pos;

    if (gOpt[ID_BOMBRAIN] && ((sFrameCounter % 10) == 0) &&
        (play->actorCtx.actorLists[ACTORCAT_EXPLOSIVES].length < 12)) {
        pos.x = player->actor.world.pos.x + Rand_CenteredFloat(500.0f);
        pos.y = player->actor.world.pos.y + 250.0f;
        pos.z = player->actor.world.pos.z + Rand_CenteredFloat(500.0f);
        // rotX = explosive type (regular bomb), params = bomb body
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, pos.x, pos.y, pos.z, 0, 0, 0, 0);
    }

    if (gOpt[ID_RUPEERAIN] && ((sFrameCounter % 6) == 0) && (play->actorCtx.actorLists[ACTORCAT_MISC].length < 45)) {
        static const u8 sRupees[] = { ITEM00_RUPEE_GREEN, ITEM00_RUPEE_GREEN, ITEM00_RUPEE_BLUE, ITEM00_RUPEE_RED };

        pos.x = player->actor.world.pos.x + Rand_CenteredFloat(400.0f);
        pos.y = player->actor.world.pos.y + 200.0f;
        pos.z = player->actor.world.pos.z + Rand_CenteredFloat(400.0f);
        Item_DropCollectible(play, &pos, sRupees[sFrameCounter % ARRAY_COUNT(sRupees)]);
    }
}

void Mods_OnPlayUpdate(PlayState* play) {
    Player* player = GET_PLAYER(play);
    SavePlayerData* data = &gSaveContext.save.saveInfo.playerData;


    sFrameCounter++;
    sLHeld = CHECK_BTN_ALL(CONTROLLER1(&play->state)->cur.button, BTN_L);

    if (sWarpPending && !gMenuOpen && (play->transitionTrigger == TRANS_TRIGGER_OFF)) {
        Warp_Start(play);
    }

    if (gOpt[ID_GOD] && (data->health > 0)) {
        data->health = data->healthCapacity;
    }
    if (gOpt[ID_MAGIC] && data->isMagicAcquired) {
        data->magic = gSaveContext.magicCapacity;
    }
    if (gOpt[ID_RUPEES]) {
        data->rupees = CUR_CAPACITY(UPG_WALLET);
    }
    if (gOpt[ID_AMMO]) {
        Ammo_Refill();
    }
    if (gOpt[ID_TIMESPEED] != TIMESPEED_NORMAL) {
        Time_Apply();
    }
    if (gOpt[ID_DISCO]) {
        Disco_Update(play);
    }

    if ((player != NULL) && !gMenuOpen && !IS_PAUSED(&play->pauseCtx) &&
        (play->transitionTrigger == TRANS_TRIGGER_OFF)) {
        Rain_Update(play, player);
    }
}

// Keep God Mode from letting a single big hit kill Link.
RECOMP_HOOK_RETURN("Health_ChangeBy") void Mods_AfterHealthChange(void) {
    SavePlayerData* data = &gSaveContext.save.saveInfo.playerData;

    Events_AfterHealthChange();
    if (gOpt[ID_GOD]) {
        data->health = data->healthCapacity;
    }
}

RECOMP_CALLBACK("*", recomp_should_actor_update) void Mods_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should) {
    if (gOpt[ID_FREEZE_ENEMIES] && (actor->category == ACTORCAT_ENEMY)) {
        *should = false;
    }
    Events_ShouldActorUpdate(play, actor, should);
}

void Mods_OnPlayInit(PlayState* play) {
    sWarpPending = false;
}

// ---------------------------------------------------------------------------
// Player movement mods
// ---------------------------------------------------------------------------

static Player* sUpdatingPlayer = NULL;
static PlayState* sUpdatingPlay = NULL;


RECOMP_HOOK("Player_Update") void Mods_BeforePlayerUpdate(Actor* thisx, PlayState* play) {
    sUpdatingPlayer = (Player*)thisx;
    sUpdatingPlay = play;
}

RECOMP_HOOK_RETURN("Player_Update") void Mods_AfterPlayerUpdate(void) {
    Player* player = sUpdatingPlayer;
    PlayState* play = sUpdatingPlay;
    s32 free;

    sUpdatingPlayer = NULL;
    if ((player == NULL) || (play == NULL) || (&player->actor != &GET_PLAYER(play)->actor)) {
        return;
    }

    free = !gMenuOpen && (player->csAction == PLAYER_CSACTION_NONE) &&
           !(player->stateFlags1 & (PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD | PLAYER_STATE1_20000000));
    if (!free) {
        return;
    }

    if (gOpt[ID_SPEED]) {
        f32 dx = player->actor.world.pos.x - player->actor.prevPos.x;
        f32 dz = player->actor.world.pos.z - player->actor.prevPos.z;
        f32 distSq = SQ(dx) + SQ(dz);

        // Move him the same distance again, unless that would go through a wall.
        if ((distSq > 0.25f) && (distSq < SQ(30.0f))) {
            Vec3f from;
            Vec3f to;
            Vec3f hit;
            CollisionPoly* poly;
            s32 bgId;

            from = player->actor.world.pos;
            from.y += 25.0f;
            to.x = from.x + dx * 1.5f;
            to.y = from.y;
            to.z = from.z + dz * 1.5f;
            if (!BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
                player->actor.world.pos.x += dx;
                player->actor.world.pos.z += dz;
            }
        }
    }

    // Experimental friends: extra speed, gravity and a trampoline.
    if (gExp.speed != 1.0f) {
        f32 dx = (player->actor.world.pos.x - player->actor.prevPos.x) * (gExp.speed - 1.0f);
        f32 dz = (player->actor.world.pos.z - player->actor.prevPos.z) * (gExp.speed - 1.0f);

        if ((SQ(dx) + SQ(dz) > 0.01f) && (SQ(dx) + SQ(dz) < SQ(200.0f))) {
            Vec3f from;
            Vec3f to;
            Vec3f hit;
            CollisionPoly* poly;
            s32 bgId;

            from = player->actor.world.pos;
            from.y += 25.0f;
            to.x = from.x + dx * 1.5f;
            to.y = from.y;
            to.z = from.z + dz * 1.5f;
            if ((gExp.speed < 1.0f) ||
                !BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, false, false, true, &bgId)) {
                player->actor.world.pos.x += dx;
                player->actor.world.pos.z += dz;
            }
        }
    }
    if ((gExp.gravity != 1.0f) && !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
        !(player->stateFlags1 & PLAYER_STATE1_8000000)) {
        player->actor.velocity.y += player->actor.gravity * (gExp.gravity - 1.0f);
    }

    if (gOpt[ID_MOONJUMP] && sLHeld) {
        player->actor.velocity.y = MOON_JUMP_VELOCITY;
    } else if ((gOpt[ID_LOWGRAV] || (Events_LowGravity() > 0.0f)) &&
               !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && !(player->stateFlags1 & PLAYER_STATE1_8000000) &&
               (player->actor.velocity.y < 20.0f)) {
        // The cheat's low gravity, or the Low Gravity event / Weightless Moon curse.
        f32 factor = MAX(gOpt[ID_LOWGRAV] ? LOW_GRAVITY_FACTOR : 0.0f, Events_LowGravity());

        player->actor.velocity.y -= player->actor.gravity * factor;
    }
}

// ---------------------------------------------------------------------------
// Player visual mods: size, rainbow, big head
// ---------------------------------------------------------------------------

static Actor* sScaledPlayer = NULL;
static Vec3f sSavedPlayerScale;
static PlayState* sPlayerDrawPlay = NULL;
static s32 sPlayerTinted = false;

RECOMP_HOOK("Player_Draw") void Mods_BeforePlayerDraw(Actor* thisx, PlayState* play) {
    sScaledPlayer = NULL;
    sPlayerTinted = false;
    sPlayerDrawPlay = play;

    if (thisx != &GET_PLAYER(play)->actor) {
        return;
    }

    // Link size (skipped while the Giant's Mask already makes him huge).
    // A size from Friend Link wins while it lasts.
    if (((gFriendLinkSize != LINKSIZE_NORMAL) || (gOpt[ID_LINKSIZE] != LINKSIZE_NORMAL)) &&
        (thisx->scale.y <= 0.015f)) {
        s32 size = (gFriendLinkSize != LINKSIZE_NORMAL) ? gFriendLinkSize : gOpt[ID_LINKSIZE];
        f32 mult = (size == LINKSIZE_GIANT) ? GIANT_SCALE : TINY_SCALE;

        Matrix_Scale(mult, mult, mult, MTXMODE_APPLY);
        sSavedPlayerScale = thisx->scale;
        sScaledPlayer = thisx;
        thisx->scale.x *= mult;
        thisx->scale.y *= mult;
        thisx->scale.z *= mult;
    }

    // Experimental friends: any size per axis (0 while hidden).
    if (gExp.invisible || (gExp.scale[0] != 1.0f) || (gExp.scale[1] != 1.0f) || (gExp.scale[2] != 1.0f)) {
        f32 sx = gExp.invisible ? 0.0001f : gExp.scale[0];
        f32 sy = gExp.invisible ? 0.0001f : gExp.scale[1];
        f32 sz = gExp.invisible ? 0.0001f : gExp.scale[2];

        Matrix_Scale(sx, sy, sz, MTXMODE_APPLY);
        if (sScaledPlayer == NULL) {
            sSavedPlayerScale = thisx->scale;
            sScaledPlayer = thisx;
        }
        thisx->scale.x *= sx;
        thisx->scale.y *= sy;
        thisx->scale.z *= sz;
    }

    // Rainbow tint, same fog trick as the carpenter.
    if (gOpt[ID_RAINBOWLINK] || gExp.rainbow) {
        u8 r, g, b;

        HueToRgb(BaseHue(play) * 2, &r, &g, &b);
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetFogColor(POLY_OPA_DISP++, r, g, b, 255);
        gSPFogPosition(POLY_OPA_DISP++, 0, RAINBOW_FOG_FAR);
        gDPPipeSync(POLY_XLU_DISP++);
        gDPSetFogColor(POLY_XLU_DISP++, r, g, b, 255);
        gSPFogPosition(POLY_XLU_DISP++, 0, RAINBOW_FOG_FAR);
        CLOSE_DISPS(play->state.gfxCtx);
        sPlayerTinted = true;
    }
}

RECOMP_HOOK_RETURN("Player_Draw") void Mods_AfterPlayerDraw(void) {
    PlayState* play = sPlayerDrawPlay;

    if (sScaledPlayer != NULL) {
        sScaledPlayer->scale = sSavedPlayerScale;
        sScaledPlayer = NULL;
    }

    if (sPlayerTinted && (play != NULL)) {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
        gDPPipeSync(POLY_XLU_DISP++);
        POLY_XLU_DISP = Play_SetFog(play, POLY_XLU_DISP);
        CLOSE_DISPS(play->state.gfxCtx);
        sPlayerTinted = false;
    }
}

// Big head: apply the head's own position and rotation ourselves, scale it up,
// and zero what the skeleton code would apply afterwards.
RECOMP_HOOK("Player_OverrideLimbDrawGameplayDefault")
void Mods_BeforePlayerLimb(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, Actor* actor) {
    f32 head = gOpt[ID_BIGHEAD] ? BIG_HEAD_SCALE : 1.0f;

    if (gExp.head != 1.0f) {
        head = gExp.head; // an Experimental friend's head size wins
    }
    if ((head == 1.0f) || (limbIndex != PLAYER_LIMB_HEAD) || (actor != &GET_PLAYER(play)->actor)) {
        return;
    }

    Matrix_TranslateRotateZYX(pos, rot);
    Matrix_Scale(head, head, head, MTXMODE_APPLY);
    pos->x = pos->y = pos->z = 0.0f;
    rot->x = rot->y = rot->z = 0;
}
