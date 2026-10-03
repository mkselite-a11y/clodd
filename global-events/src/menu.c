#include "common.h"

// ---------------------------------------------------------------------------
// Call of Duty style mod menu, opened by talking to the plank carpenter.
//
// D-pad / stick up and down to scroll, A to select, B to go back, left and
// right to change multi-choice entries. Drawn with the game's built-in debug
// text printer on top of the HUD. While it's open, Link is held in a "silent
// talk" state and all controller input is kept away from the game.
// ---------------------------------------------------------------------------

u8 gOpt[ID_MAX];
s32 gMenuOpen = false;

typedef enum {
    MENU_MAIN,
    MENU_EVENTS,
    MENU_EV_LIST,
    MENU_EV_START,
    MENU_POOL,
    MENU_CRITTERS,
    MENU_MOONFALL,
    MENU_EV_SETTINGS,
    MENU_NEMESIS,
    MENU_PHANTOM,
    MENU_TREASURE,
    MENU_HUNGER,
    MENU_SEARCH,
    MENU_FATE,
    MENU_REDLIGHT,
    MENU_ECHO,
    MENU_GLARE,
    MENU_KAMIKAZE,
    MENU_WOLFPACK,
    MENU_CHAMPION,
    MENU_INFIGHT,
    MENU_STILL,
    MENU_IMPOSTER,
    MENU_UPRISING,
    MENU_PROCESSION,
    MENU_POLTERGEIST,
    MENU_BULLETHELL,
    MENU_MIRAGE,
    MENU_MIRROR,
    MENU_SWARM,
    MENU_MAJORA,
    MENU_LEVIATHAN,
    MENU_EV_FEATURES,
    MENU_EV_REPORT,
    MENU_EV_LOG,
    MENU_V2,
    MENU_V2_BOUNTY,
    MENU_V2_SHOP,
    MENU_V2_BAN,
    MENU_V2_CALL,
    MENU_V2_NEM,
    MENU_V2_MUTS,
    MENU_V2_LINK,
    MENU_V2_DRAFT,
    MENU_V2_BINFO,
    MENU_V2_ITEMS,
    MENU_V2_SERVICES,
    MENU_TINGLE,
    MENU_STAMPEDE,
    MENU_CURSETAG,
    MENU_PLAYER,
    MENU_ITEMS,
    MENU_WORLD,
    MENU_FUN,
    MENU_TELEPORT,
    MENU_CARPENTER,
    MENU_SETTINGS,
    MENU_GIVE_ITEM,
    MENU_GIVE_MASK,
    MENU_OVERVIEW,  // V3
    MENU_EV_DETAIL, // V3
    MENU_COUNT
} MenuPage;

typedef enum {
    ENTRY_SUBMENU,
    ENTRY_ACTION,
    ENTRY_TOGGLE,
    ENTRY_CHOICE,
    ENTRY_INFO
} EntryType;

// Two builds come from this code: "Global Events Mod" and, with GE_CHEATS
// defined, "Global Events Mod + Cheats" (player, inventory, world and teleport
// pages).
#ifdef GE_CHEATS
static const u16 sMainEntries[] = { ID_SUB_PLAYER, ID_SUB_ITEMS, ID_SUB_WORLD, ID_SUB_TELEPORT, ID_SUB_FUN, ID_CLOSE };
#else
static const u16 sMainEntries[] = { ID_SUB_EVENTS, ID_SUB_V2, ID_SUB_V2_NEM, ID_SUB_V2_LINK, ID_SUB_SETTINGS, ID_CLOSE };
#endif
s32 gMenuAtBoard = false;
// Moon Marks: money, the board, the shop and the pouch.
static const u16 sV2Entries[] = { ID_V2_MARKS,  ID_SUB_V2_BOUNTY, ID_SUB_V2_SHOP, ID_V2_BOUNTY,  ID_V2_POUCH,
                                  ID_V2_REMOTE, ID_V2_TRACKER,    ID_V2_AUTOSAVE };
static const u16 sV2LinkEntries[] = { ID_V2_LINK, ID_V2_LINKSTATE, ID_LNK_APROOM, ID_LNK_EXPUNDO };
static u16 sV2LinkDyn[4 + 12];
static u16 sV2BountyDyn[V2_BOUNTY_SLOTS + 2 + 3];
static const u16 sV2NemEntries[] = { ID_V2_NEMROW_FIRST,     ID_V2_NEMROW_FIRST + 1, ID_V2_NEMROW_FIRST + 2,
                                     ID_V2_NEMROW_FIRST + 3, ID_V2_NEMROW_FIRST + 4, ID_V2_NEMROW_FIRST + 5,
                                     ID_V2_NEMROW_FIRST + 6, ID_V2_NEMROW_FIRST + 7 };
static const u16 sV2MutEntries[] = { ID_V2_MUTROW_FIRST,     ID_V2_MUTROW_FIRST + 1, ID_V2_MUTROW_FIRST + 2,
                                     ID_V2_MUTROW_FIRST + 3, ID_V2_MUTROW_FIRST + 4, ID_V2_MUTROW_FIRST + 5 };
static const u16 sV2DraftEntries[] = { ID_V2_DRAFT_A, ID_V2_DRAFT_B, ID_V2_DRAFT_REROLL, ID_V2_MARKS };
s32 gV2InfoBounty = 0;
static u16 sV2InfoEntries[5 + 16 + 4];
static u16 sV2NemDyn[48];

// Rows for one trait mask, in trait order.
static s32 Menu_TraitRows(u16* out, s32 n, u16 mask) {
    s32 t;

    for (t = 0; t < 16; t++) {
        if (mask & (1 << t)) {
            out[n++] = ID_V2_BTRAIT_FIRST + t;
        }
    }
    return n;
}
static u16 sV2ShopEntries[3];
static u16 sV2ItemEntries[1 + 32];
static u16 sV2ServiceEntries[3 + 32];
static s32 sV2ItemCount = 0;
static s32 sV2ServiceCount = 0;
static u16 sV2BanEntries[EV_COUNT];
static u16 sV2CallEntries[EV_COUNT];
static s32 sV2EvCount = 0;
// V3: the curse sits with the events; one list of events, each with its own page.
static const u16 sEventsEntries[] = { ID_EV_CURSE_INFO, ID_EV_STATUS, ID_EV_MASTER,   ID_SUB_EV_LIST, ID_EV_TRIGGER,
                                      ID_EV_END,        ID_SUB_V2_MUTS, ID_V2_MUT,   ID_SUB_EV_REPORT };
static const u16 sEvFeatureEntries[] = { ID_EV_ROULETTE,  ID_EV_COMBOS,    ID_EV_CURSE,
                                         ID_EV_HOURS,     ID_EV_TIMERSTYLE,
                                         ID_EV_WARNSOUND, ID_EV_STREAKS,   ID_EV_MERCY };
static u16 sOverviewDyn[16];
static u16 sEvDetailDyn[4];
static u16 sLogEntries[10];
static const u16 sTingleEntries[] = { ID_TA_RATE, ID_TA_BOMBS };
static const u16 sStampedeEntries[] = { ID_GS_LANES, ID_GS_SPEED };
static const u16 sCurseTagEntries[] = { ID_CT_FUSE, ID_CT_ENEMIES };
static const u16 sEvSettingsEntries[] = { ID_SUB_EV_POOL,    ID_SUB_EV_CRITTERS,
                                         ID_SUB_EV_PHANTOM, ID_SUB_EV_TREASURE, ID_SUB_EV_HUNGER,   ID_SUB_EV_SEARCH,
                                         ID_SUB_EV_FATE,    ID_SUB_EV_REDLIGHT, ID_SUB_EV_ECHO,
                                         ID_SUB_EV_CHAMPION,
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
                                         ID_SUB_EV_TINGLE,
                                         ID_SUB_EV_CURSETAG,
};
static const u16 sMoonfallEntries[] = { ID_MF_METEOR_SIZE, ID_MF_SWOOP, ID_MF_MUSIC, ID_MF_QUAKE };
static const u16 sNemesisEntries[] = { ID_NEM_TYPE, ID_NEM_SIZE, ID_NEM_GLOW };
static const u16 sPhantomEntries[] = { ID_PH_SIZE, ID_PH_SOLDIERS, ID_PH_SHIMMER };
static const u16 sTreasureEntries[] = { ID_TH_TIME, ID_TH_ARROW };
static const u16 sHungerEntries[] = { ID_BH_HEAL, ID_BH_ENEMIES };
static const u16 sSearchEntries[] = { ID_SL_COUNT, ID_SL_SIZE, ID_SL_BACKUP };
static const u16 sFateEntries[] = { ID_TF_COUNT, ID_TF_SPEED, ID_TF_WALLS };
static const u16 sRedLightEntries[] = { ID_RL_REACT, ID_RL_GREEN, ID_RL_RED };
static const u16 sEchoEntries[] = { ID_EC_DELAY, ID_EC_CLOSER };
static const u16 sGlareEntries[] = { ID_MG_LENGTH, ID_MG_REACT };
static const u16 sKamikazeEntries[] = { ID_KK_SWARM, ID_KK_SPEED };
static const u16 sWolfPackEntries[] = { ID_WP_SIZE, ID_WP_WHITE };
static const u16 sChampionEntries[] = { ID_CD_WHO, ID_CD_HEALTH, ID_CD_FREEZE };
static const u16 sInfightEntries[] = { ID_IF_SIZE };
static const u16 sStillEntries[] = { ID_ST_COUNT, ID_ST_STILL };
static const u16 sImposterEntries[] = { ID_IM_CROWD, ID_IM_SHARE };
static const u16 sUprisingEntries[] = { ID_CU_COUNT, ID_CU_HITS };
static const u16 sProcessionEntries[] = { ID_PR_LENGTH };
static const u16 sPoltergeistEntries[] = { ID_PG_ROCKS };
static const u16 sBullethellEntries[] = { ID_BL_RING, ID_BL_FAN };
static const u16 sMirageEntries[] = { ID_MI_GROUPS, ID_MI_FAKES };
static const u16 sMirrorEntries[] = { ID_MD_COUNT };
static const u16 sSwarmEntries[] = { ID_SN_COUNT };
static const u16 sMajoraEntries[] = { ID_MJ_SIZE };
static const u16 sLeviathanEntries[] = { ID_LV_SIZE };

// Built at startup since they're long runs of consecutive IDs.
static u16 sEvListEntries[EV_COUNT + 7];
static s32 sEvListCount = 0;
static u16 sEvStartEntries[EV_COUNT];
static s32 sEvStartCount = 0;
static u16 sGiveItemEntries[GIVE_ITEM_COUNT];
static u16 sGiveMaskEntries[GIVE_MASK_COUNT];
static u16 sPoolEntries[3 + POOL_COUNT];
static s32 sPoolCount = 0;
static u16 sCritterEntries[1 + CRIT_COUNT];
static u16 sReportEntries[10 + EV_COUNT];
static s32 sReportCount = 0;

static void Menu_BuildLists(void) {
    s32 i;

    sEvListCount = 0;
    sEvStartCount = 0;
    sV2ShopEntries[0] = ID_V2_MARKS;
    sV2ShopEntries[1] = ID_SUB_V2_ITEMS;
    sV2ShopEntries[2] = ID_SUB_V2_SERVICES;
    sV2ItemCount = 0;
    sV2ItemEntries[sV2ItemCount++] = ID_V2_MARKS;
    for (i = 0; i < V2_ShopItemCount(); i++) {
        sV2ItemEntries[sV2ItemCount++] = ID_V2_SHOP_FIRST + i;
    }
    sV2ServiceCount = 0;
    sV2ServiceEntries[sV2ServiceCount++] = ID_V2_MARKS;
    sV2ServiceEntries[sV2ServiceCount++] = ID_SUB_V2_CALL; // V3: Call and Ban are Moon Services
    sV2ServiceEntries[sV2ServiceCount++] = ID_SUB_V2_BAN;
    for (i = 0; i < V2_ShopServiceCount(); i++) {
        if (!V2_ServiceHidden(i)) {
            sV2ServiceEntries[sV2ServiceCount++] = ID_V2_SHOP_FIRST + V2_ShopItemCount() + i;
        }
    }
    sV2EvCount = 0;
    for (i = 0; i < EV_COUNT; i++) {
        if (!Events_IsHidden(i)) {
            sV2BanEntries[sV2EvCount] = ID_V2_BAN_FIRST + i;
            sV2CallEntries[sV2EvCount] = ID_V2_CALL_FIRST + i;
            sV2EvCount++;
        }
    }
    sReportCount = 0;
    sReportEntries[sReportCount++] = ID_RR_SEEN;
    sReportEntries[sReportCount++] = ID_RR_SURVIVED;
    sReportEntries[sReportCount++] = ID_RR_DEATHS;
    sReportEntries[sReportCount++] = ID_RR_COMBOS;
    sReportEntries[sReportCount++] = ID_RR_STREAK;
    sReportEntries[sReportCount++] = ID_RR_BEST;
    for (i = 0; i < 10; i++) {
        sLogEntries[i] = ID_LOG_FIRST + i;
    }
    sReportEntries[sReportCount++] = ID_RR_KILLER;
    sReportEntries[sReportCount++] = ID_RR_KILLER_NAME;
    sReportEntries[sReportCount++] = ID_RR_RESET;
    // V3: the event settings sit at the top of Choose Events.
    sEvListEntries[sEvListCount++] = ID_EV_INTERVAL;
    sEvListEntries[sEvListCount++] = ID_EV_DURATION;
    sEvListEntries[sEvListCount++] = ID_SUB_EV_FEATURES;
    sEvListEntries[sEvListCount++] = ID_SUB_EV_POOL;
    sEvListEntries[sEvListCount++] = ID_SUB_EV_CRITTERS;
    sEvListEntries[sEvListCount++] = ID_EV_ALL_ON;
    sEvListEntries[sEvListCount++] = ID_EV_ALL_OFF;
    for (i = 0; i < EV_COUNT; i++) {
        if (Events_IsHidden(i)) {
            continue; // retired events
        }
        sEvListEntries[sEvListCount++] = ID_EV_ON_FIRST + i;
        sEvStartEntries[sEvStartCount++] = ID_EV_GO_FIRST + i;
        sReportEntries[sReportCount++] = ID_RR_EV_FIRST + i;
    }
    for (i = 0; i < GIVE_ITEM_COUNT; i++) {
        sGiveItemEntries[i] = ID_GIVEITEM_FIRST + i;
    }
    for (i = 0; i < GIVE_MASK_COUNT; i++) {
        sGiveMaskEntries[i] = ID_GIVEMASK_FIRST + i;
    }
    sPoolEntries[0] = ID_POOL_HORDE;
    sPoolEntries[1] = ID_POOL_ALL_ON;
    sPoolEntries[2] = ID_POOL_ALL_OFF;
    sPoolCount = 3;
    for (i = 0; i < POOL_COUNT; i++) {
        if (i != 33) { // V3: Big Poes are retired
            sPoolEntries[sPoolCount++] = ID_POOL_FIRST + i;
        }
    }
    sCritterEntries[0] = ID_CRIT_AMOUNT;
    for (i = 0; i < CRIT_COUNT; i++) {
        sCritterEntries[1 + i] = ID_CRIT_FIRST + i;
    }
}
static const u16 sPlayerEntries[] = { ID_GOD, ID_MAGIC, ID_RUPEES, ID_MOONJUMP, ID_SPEED, ID_LOWGRAV, ID_REFILL };
static const u16 sItemEntries[] = { ID_AMMO,       ID_SUB_GIVE_ITEM, ID_SUB_GIVE_MASK, ID_GIVE_ITEMS,
                                    ID_GIVE_MASKS, ID_GIVE_QUEST,    ID_MAX_UPGRADES,  ID_FAIRIES };
static const u16 sWorldEntries[] = { ID_TIMESPEED, ID_FREEZE_ENEMIES, ID_KILL_ENEMIES };

#ifdef GE_CHEATS
static const u16 sFunEntries[] = { ID_LINKSIZE, ID_BIGHEAD, ID_RAINBOWLINK, ID_BOMBRAIN, ID_RUPEERAIN };
#else
static const u16 sFunEntries[] = { ID_LINKSIZE, ID_BIGHEAD, ID_RAINBOWLINK };
#endif
static const u16 sTeleportEntries[] = { ID_SAVEPOS,       ID_LOADPOS,       ID_WARP_CLOCKTOWN, ID_WARP_MILKROAD,
                                       ID_WARP_SWAMP,    ID_WARP_WOODFALL, ID_WARP_MOUNTAIN,  ID_WARP_SNOWHEAD,
                                       ID_WARP_GREATBAY, ID_WARP_ZORACAPE, ID_WARP_IKANA,     ID_WARP_STONETOWER };
static const u16 sCarpenterEntries[] = { ID_C_LOOP, ID_C_RAINBOW, ID_C_GLOW, ID_C_SPEED, ID_C_REVERSE };
static const u16 sSettingsEntries[] = { ID_THEME,    ID_SOUNDS,    ID_V2_NEWS,   ID_V2_BANNERS, ID_V2_AUTOSAVE,
                                         ID_V2_MUT,   ID_V2_BOUNTY, ID_V2_NEM,    ID_V2_POUCH,   ID_V2_TRACKER,
                                         ID_V2_REMOTE, ID_ALL_OFF };

s32 V2_TakenBounty(s32 k);

static const u16* Menu_Entries(s32 page, s32* count) {
    switch (page) {
        case MENU_OVERVIEW: {
            s32 n = 0;
            s32 k;

            sOverviewDyn[n++] = ID_V2_MARKS;
            sOverviewDyn[n++] = ID_EV_CURSE_INFO;
            sOverviewDyn[n++] = ID_EV_STATUS;
            sOverviewDyn[n++] = ID_OV_NEM;
            for (k = 0; k < 3; k++) {
                if (V2_TakenBounty(k) >= 0) {
                    sOverviewDyn[n++] = ID_OV_BNT_FIRST + k;
                }
            }
            sOverviewDyn[n++] = ID_OV_CONTRACTS;
            sOverviewDyn[n++] = ID_OV_MUTS;
            sOverviewDyn[n++] = ID_V2_RANK;
            sOverviewDyn[n++] = ID_OV_FRIENDS;
            *count = n;
            return sOverviewDyn;
        }
        case MENU_EV_DETAIL:
            sEvDetailDyn[0] = ID_EVD_TIER;
            sEvDetailDyn[1] = ID_EVD_HOW;
            sEvDetailDyn[2] = ID_EV_ON_FIRST + gEvDetail;
            sEvDetailDyn[3] = ID_EV_GO_FIRST + gEvDetail;
            *count = 4;
            return sEvDetailDyn;
        case MENU_EVENTS:
            *count = ARRAY_COUNT(sEventsEntries);
            return sEventsEntries;
        case MENU_EV_LIST:
            *count = sEvListCount;
            return sEvListEntries;
        case MENU_EV_START:
            *count = sEvStartCount;
            return sEvStartEntries;
        case MENU_POOL:
            *count = sPoolCount;
            return sPoolEntries;
        case MENU_CRITTERS:
            *count = ARRAY_COUNT(sCritterEntries);
            return sCritterEntries;
        case MENU_MOONFALL:
            *count = ARRAY_COUNT(sMoonfallEntries);
            return sMoonfallEntries;
        case MENU_EV_SETTINGS:
            *count = ARRAY_COUNT(sEvSettingsEntries);
            return sEvSettingsEntries;
        case MENU_EV_FEATURES:
            *count = ARRAY_COUNT(sEvFeatureEntries);
            return sEvFeatureEntries;
        case MENU_EV_REPORT:
            *count = sReportCount;
            return sReportEntries;
        case MENU_EV_LOG:
            *count = ARRAY_COUNT(sLogEntries);
            return sLogEntries;
        case MENU_V2:
            *count = ARRAY_COUNT(sV2Entries);
            return sV2Entries;
        case MENU_V2_BOUNTY: {
            // WANTED and rare first (when posted), then the regulars, streak, contracts.
            static const u8 sOrder[V2_BOUNTY_SLOTS] = { 5, 3, 4, 0, 1, 2 };
            s32 n = 0;
            s32 k;

            for (k = 0; k < V2_BOUNTY_SLOTS; k++) {
                if (V2_BountyActive(sOrder[k])) {
                    sV2BountyDyn[n++] = ID_V2_BNT_FIRST + sOrder[k];
                }
            }
            sV2BountyDyn[n++] = ID_V2_RANK;
            sV2BountyDyn[n++] = ID_V2_STREAK;
            for (k = 0; k < 3; k++) {
                sV2BountyDyn[n++] = ID_V2_CON_FIRST + k;
            }
            *count = n;
            return sV2BountyDyn;
        }
        case MENU_V2_SHOP:
            *count = ARRAY_COUNT(sV2ShopEntries);
            return sV2ShopEntries;
        case MENU_V2_ITEMS:
            *count = sV2ItemCount;
            return sV2ItemEntries;
        case MENU_V2_SERVICES:
            *count = sV2ServiceCount;
            return sV2ServiceEntries;
        case MENU_V2_BAN:
            *count = sV2EvCount;
            return sV2BanEntries;
        case MENU_V2_CALL:
            *count = sV2EvCount;
            return sV2CallEntries;
        case MENU_V2_NEM: {
            // Name, type, level, one row per trait (hover to read it), place, moves, wins.
            s32 n = 0;

            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 0;
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 1;
            if (V2_NemCount() > 0) {
                u16 um = V2_NemUniqMask();
                s32 u;

                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 2;  // level
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 6;  // points / trait pick waiting
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 8;  // Health
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 9;  // Power
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 10; // Speed
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 11; // Tracking
                for (u = 0; u < 12; u++) {
                    if (um & (1 << u)) {
                        sV2NemDyn[n++] = ID_V2_NUNIQ_FIRST + u;
                    }
                }
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 4; // nature
            }
            n = Menu_TraitRows(sV2NemDyn, n, V2_NemTraitMask());
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 5;
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 7;
            sV2NemDyn[n++] = ID_V2_NEMDIFF;
            *count = n;
            return sV2NemDyn;
        }
        case MENU_V2_BINFO: {
            s32 n = 0;
            s32 k;

            for (k = 0; k < 5; k++) {
                sV2InfoEntries[n++] = ID_V2_BINFO_FIRST + k;
            }
            n = Menu_TraitRows(sV2InfoEntries, n, V2_BountyTraits(gV2InfoBounty));
            if (V2_BountyElite(gV2InfoBounty)) {
                sV2InfoEntries[n++] = ID_V2_BELITE;
            }
            if (V2_BountyBonus(gV2InfoBounty)) {
                sV2InfoEntries[n++] = ID_V2_BBONUS;
            }
            sV2InfoEntries[n++] = ID_V2_BACCEPT;
            if (V2_BountyUpgradable(gV2InfoBounty)) {
                sV2InfoEntries[n++] = ID_V2_BUPGRADE;
            }
            *count = n;
            return sV2InfoEntries;
        }
        case MENU_V2_MUTS:
            *count = ARRAY_COUNT(sV2MutEntries);
            return sV2MutEntries;
        case MENU_V2_LINK: {
            // The switch, the connection, then a row per friend (A changes what they may do).
            s32 n = 0;
            s32 k;
            s32 rows = Link_FriendRows();

            for (k = 0; k < (s32)ARRAY_COUNT(sV2LinkEntries); k++) {
                sV2LinkDyn[n++] = sV2LinkEntries[k];
            }
            for (k = 0; k < rows; k++) {
                sV2LinkDyn[n++] = ID_LNK_FR_FIRST + k;
            }
            *count = n;
            return sV2LinkDyn;
        }
        case MENU_V2_DRAFT:
            *count = ARRAY_COUNT(sV2DraftEntries);
            return sV2DraftEntries;
        case MENU_TINGLE:
            *count = ARRAY_COUNT(sTingleEntries);
            return sTingleEntries;
        case MENU_STAMPEDE:
            *count = ARRAY_COUNT(sStampedeEntries);
            return sStampedeEntries;
        case MENU_CURSETAG:
            *count = ARRAY_COUNT(sCurseTagEntries);
            return sCurseTagEntries;
        case MENU_NEMESIS:
            *count = ARRAY_COUNT(sNemesisEntries);
            return sNemesisEntries;
        case MENU_PHANTOM:
            *count = ARRAY_COUNT(sPhantomEntries);
            return sPhantomEntries;
        case MENU_TREASURE:
            *count = ARRAY_COUNT(sTreasureEntries);
            return sTreasureEntries;
        case MENU_HUNGER:
            *count = ARRAY_COUNT(sHungerEntries);
            return sHungerEntries;
        case MENU_SEARCH:
            *count = ARRAY_COUNT(sSearchEntries);
            return sSearchEntries;
        case MENU_FATE:
            *count = ARRAY_COUNT(sFateEntries);
            return sFateEntries;
        case MENU_REDLIGHT:
            *count = ARRAY_COUNT(sRedLightEntries);
            return sRedLightEntries;
        case MENU_ECHO:
            *count = ARRAY_COUNT(sEchoEntries);
            return sEchoEntries;
        case MENU_GLARE:
            *count = ARRAY_COUNT(sGlareEntries);
            return sGlareEntries;
        case MENU_KAMIKAZE:
            *count = ARRAY_COUNT(sKamikazeEntries);
            return sKamikazeEntries;
        case MENU_WOLFPACK:
            *count = ARRAY_COUNT(sWolfPackEntries);
            return sWolfPackEntries;
        case MENU_CHAMPION:
            *count = ARRAY_COUNT(sChampionEntries);
            return sChampionEntries;
        case MENU_INFIGHT:
            *count = ARRAY_COUNT(sInfightEntries);
            return sInfightEntries;
        case MENU_STILL:
            *count = ARRAY_COUNT(sStillEntries);
            return sStillEntries;
        case MENU_IMPOSTER:
            *count = ARRAY_COUNT(sImposterEntries);
            return sImposterEntries;
        case MENU_UPRISING:
            *count = ARRAY_COUNT(sUprisingEntries);
            return sUprisingEntries;
        case MENU_PROCESSION:
            *count = ARRAY_COUNT(sProcessionEntries);
            return sProcessionEntries;
        case MENU_POLTERGEIST:
            *count = ARRAY_COUNT(sPoltergeistEntries);
            return sPoltergeistEntries;
        case MENU_BULLETHELL:
            *count = ARRAY_COUNT(sBullethellEntries);
            return sBullethellEntries;
        case MENU_MIRAGE:
            *count = ARRAY_COUNT(sMirageEntries);
            return sMirageEntries;
        case MENU_MIRROR:
            *count = ARRAY_COUNT(sMirrorEntries);
            return sMirrorEntries;
        case MENU_SWARM:
            *count = ARRAY_COUNT(sSwarmEntries);
            return sSwarmEntries;
        case MENU_MAJORA:
            *count = ARRAY_COUNT(sMajoraEntries);
            return sMajoraEntries;
        case MENU_LEVIATHAN:
            *count = ARRAY_COUNT(sLeviathanEntries);
            return sLeviathanEntries;
        case MENU_PLAYER:
            *count = ARRAY_COUNT(sPlayerEntries);
            return sPlayerEntries;
        case MENU_ITEMS:
            *count = ARRAY_COUNT(sItemEntries);
            return sItemEntries;
        case MENU_WORLD:
            *count = ARRAY_COUNT(sWorldEntries);
            return sWorldEntries;
        case MENU_FUN:
            *count = ARRAY_COUNT(sFunEntries);
            return sFunEntries;
        case MENU_TELEPORT:
            *count = ARRAY_COUNT(sTeleportEntries);
            return sTeleportEntries;
        case MENU_CARPENTER:
            *count = ARRAY_COUNT(sCarpenterEntries);
            return sCarpenterEntries;
        case MENU_SETTINGS:
            *count = ARRAY_COUNT(sSettingsEntries);
            return sSettingsEntries;
        case MENU_GIVE_ITEM:
            *count = ARRAY_COUNT(sGiveItemEntries);
            return sGiveItemEntries;
        case MENU_GIVE_MASK:
            *count = ARRAY_COUNT(sGiveMaskEntries);
            return sGiveMaskEntries;
        default:
            *count = ARRAY_COUNT(sMainEntries);
            return sMainEntries;
    }
}

static const char* Menu_PageTitle(s32 page) {
    switch (page) {
        case MENU_OVERVIEW:
            return "Overview";
        case MENU_EV_DETAIL:
            return Events_ShortName(gEvDetail); // (full names run into the header)
        case MENU_EVENTS:
            return "Events";
        case MENU_EV_LIST:
            return "Choose Events";
        case MENU_EV_START:
            return "Start An Event";
        case MENU_POOL:
            return "Blood Moon Pool";
        case MENU_CRITTERS:
            return "Joke Critters";
        case MENU_MOONFALL:
            return "Moonfall Options";
        case MENU_EV_SETTINGS:
            return "Event Settings";
        case MENU_NEMESIS:
            return "Nemesis";
        case MENU_PHANTOM:
            return "Phantom Army";
        case MENU_TREASURE:
            return "Treasure Hunt";
        case MENU_HUNGER:
            return "Blood Hunger";
        case MENU_SEARCH:
            return "Searchlights";
        case MENU_FATE:
            return "Terrible Fate";
        case MENU_REDLIGHT:
            return "Red/Green Light";
        case MENU_ECHO:
            return "Echo";
        case MENU_GLARE:
            return "Moon's Glare";
        case MENU_KAMIKAZE:
            return "Kamikaze Keese";
        case MENU_WOLFPACK:
            return "Wolf Pack";
        case MENU_CHAMPION:
            return "Champion Duel";
        case MENU_INFIGHT:
            return "Infighting";
        case MENU_STILL:
            return "Time Moves w/ You";
        case MENU_IMPOSTER:
            return "Imposters";
        case MENU_UPRISING:
            return "Carpenter Uprising";
        case MENU_PROCESSION:
            return "Procession";
        case MENU_POLTERGEIST:
            return "Poltergeist";
        case MENU_BULLETHELL:
            return "Bullet Hell";
        case MENU_MIRAGE:
            return "Mirage";
        case MENU_MIRROR:
            return "Mirror Dance";
        case MENU_SWARM:
            return "Swarm Night";
        case MENU_MAJORA:
            return "Majora Looms";
        case MENU_LEVIATHAN:
            return "Sky Leviathan";
        case MENU_EV_FEATURES:
            return "Event Features";
        case MENU_EV_REPORT:
            return "Run Report";
        case MENU_EV_LOG:
            return "Event Log";
        case MENU_V2:
            return "Moon Marks";
        case MENU_V2_LINK:
            return "Friends";
        case MENU_V2_BOUNTY:
            return "Bounty Board";
        case MENU_V2_SHOP:
            return "Moon Shop";
        case MENU_V2_BAN:
            return "Ban an Event";
        case MENU_V2_CALL:
            return "Call an Event";
        case MENU_V2_NEM:
            return "Nemesis";
        case MENU_V2_MUTS:
            return "Your Picks";
        case MENU_V2_DRAFT:
            return "Moon Draft";
        case MENU_V2_BINFO:
            return "Bounty Details";
        case MENU_V2_ITEMS:
            return "Moon Pouch Items";
        case MENU_V2_SERVICES:
            return "Moon Services";
        case MENU_TINGLE:
            return "Tingle Air Raid";
        case MENU_STAMPEDE:
            return "Goron Stampede";
        case MENU_CURSETAG:
            return "Cursed Tag";
        case MENU_PLAYER:
            return "Player Mods";
        case MENU_ITEMS:
            return "Inventory Mods";
        case MENU_WORLD:
            return "World Mods";
        case MENU_FUN:
            return "Fun Mods";
        case MENU_TELEPORT:
            return "Teleports";
        case MENU_CARPENTER:
            return "Carpenter Mods";
        case MENU_SETTINGS:
            return "Settings";
        case MENU_GIVE_ITEM:
            return "Give Item";
        case MENU_GIVE_MASK:
            return "Give Mask";
        default:
            return "Cheats";
    }
}

// V3: the top-level pages are tabs (L / R or Z to switch).
#ifdef GE_CHEATS
static const u8 sTabs[] = { MENU_OVERVIEW, MENU_V2_BOUNTY, MENU_V2_SHOP, MENU_V2_NEM,
                            MENU_EVENTS,   MENU_V2_LINK,   MENU_SETTINGS, MENU_MAIN };
#else
static const u8 sTabs[] = { MENU_OVERVIEW, MENU_V2_BOUNTY, MENU_V2_SHOP, MENU_V2_NEM,
                            MENU_EVENTS,   MENU_V2_LINK,   MENU_SETTINGS };
#endif
static s32 sTab = 0;

static s32 Menu_IsTabRoot(s32 page) {
    s32 k;

    for (k = 0; k < (s32)ARRAY_COUNT(sTabs); k++) {
        if (sTabs[k] == page) {
            return true;
        }
    }
    return false;
}

static s32 Menu_Parent(s32 page) {
    if (Menu_IsTabRoot(page)) {
        return page;
    }
    switch (page) {
        case MENU_EV_DETAIL:
            return MENU_EV_LIST;
        case MENU_V2:
            return MENU_OVERVIEW;
        case MENU_CARPENTER:
            return MENU_SETTINGS;
        case MENU_GIVE_ITEM:
        case MENU_GIVE_MASK:
            return MENU_ITEMS;
        case MENU_EV_FEATURES:
        case MENU_POOL:
        case MENU_CRITTERS:
            return MENU_EV_LIST;
        case MENU_EV_LIST:
        case MENU_EV_START:
        case MENU_EV_SETTINGS:
        case MENU_EV_REPORT:
        case MENU_EV_LOG:
            return MENU_EVENTS;
        case MENU_V2_MUTS:
            return MENU_EVENTS;
        case MENU_V2_BAN:
        case MENU_V2_CALL:
            return MENU_V2_SERVICES;
        case MENU_V2_ITEMS:
        case MENU_V2_SERVICES:
            return MENU_V2_SHOP;
        case MENU_V2_DRAFT:
            return MENU_V2_DRAFT;
        case MENU_V2_BINFO:
            return MENU_V2_BOUNTY;
        case MENU_MOONFALL:
        case MENU_NEMESIS:
        case MENU_PHANTOM:
        case MENU_TREASURE:
        case MENU_HUNGER:
        case MENU_SEARCH:
        case MENU_FATE:
        case MENU_REDLIGHT:
        case MENU_ECHO:
        case MENU_GLARE:
        case MENU_KAMIKAZE:
        case MENU_WOLFPACK:
        case MENU_CHAMPION:
        case MENU_INFIGHT:
        case MENU_STILL:
        case MENU_IMPOSTER:
        case MENU_UPRISING:
        case MENU_PROCESSION:
        case MENU_POLTERGEIST:
        case MENU_BULLETHELL:
        case MENU_MIRAGE:
        case MENU_MIRROR:
        case MENU_SWARM:
        case MENU_MAJORA:
        case MENU_LEVIATHAN:
        case MENU_TINGLE:
        case MENU_STAMPEDE:
        case MENU_CURSETAG:
            return MENU_EV_SETTINGS;
        default:
            return MENU_MAIN;
    }
}

static s32 Menu_SubmenuTarget(s32 id) {
    switch (id) {
        case ID_SUB_EVENTS:
            return MENU_EVENTS;
        case ID_SUB_EV_LIST:
            return MENU_EV_LIST;
        case ID_SUB_EV_START:
            return MENU_EV_START;
        case ID_SUB_EV_POOL:
            return MENU_POOL;
        case ID_SUB_EV_CRITTERS:
            return MENU_CRITTERS;
        case ID_SUB_EV_MOONFALL:
            return MENU_MOONFALL;
        case ID_SUB_EV_SETTINGS:
            return MENU_EV_SETTINGS;
        case ID_SUB_EV_NEMESIS:
            return MENU_NEMESIS;
        case ID_SUB_EV_PHANTOM:
            return MENU_PHANTOM;
        case ID_SUB_EV_TREASURE:
            return MENU_TREASURE;
        case ID_SUB_EV_HUNGER:
            return MENU_HUNGER;
        case ID_SUB_EV_SEARCH:
            return MENU_SEARCH;
        case ID_SUB_EV_FATE:
            return MENU_FATE;
        case ID_SUB_EV_REDLIGHT:
            return MENU_REDLIGHT;
        case ID_SUB_EV_ECHO:
            return MENU_ECHO;
        case ID_SUB_EV_GLARE:
            return MENU_GLARE;
        case ID_SUB_EV_KAMIKAZE:
            return MENU_KAMIKAZE;
        case ID_SUB_EV_WOLFPACK:
            return MENU_WOLFPACK;
        case ID_SUB_EV_CHAMPION:
            return MENU_CHAMPION;
        case ID_SUB_EV_INFIGHT:
            return MENU_INFIGHT;
        case ID_SUB_EV_STILL:
            return MENU_STILL;
        case ID_SUB_EV_IMPOSTER:
            return MENU_IMPOSTER;
        case ID_SUB_EV_UPRISING:
            return MENU_UPRISING;
        case ID_SUB_EV_PROCESSION:
            return MENU_PROCESSION;
        case ID_SUB_EV_POLTERGEIST:
            return MENU_POLTERGEIST;
        case ID_SUB_EV_BULLETHELL:
            return MENU_BULLETHELL;
        case ID_SUB_EV_MIRAGE:
            return MENU_MIRAGE;
        case ID_SUB_EV_MIRROR:
            return MENU_MIRROR;
        case ID_SUB_EV_SWARM:
            return MENU_SWARM;
        case ID_SUB_EV_MAJORA:
            return MENU_MAJORA;
        case ID_SUB_EV_LEVIATHAN:
            return MENU_LEVIATHAN;
        case ID_SUB_EV_FEATURES:
            return MENU_EV_FEATURES;
        case ID_SUB_EV_REPORT:
            return MENU_EV_REPORT;
        case ID_SUB_EV_LOG:
            return MENU_EV_LOG;
        case ID_SUB_V2:
            return MENU_V2;
        case ID_SUB_V2_BOUNTY:
            return MENU_V2_BOUNTY;
        case ID_SUB_V2_SHOP:
            return MENU_V2_SHOP;
        case ID_SUB_V2_BAN:
            return MENU_V2_BAN;
        case ID_SUB_V2_CALL:
            return MENU_V2_CALL;
        case ID_SUB_V2_NEM:
            return MENU_V2_NEM;
        case ID_SUB_V2_MUTS:
            return MENU_V2_MUTS;
        case ID_SUB_V2_ITEMS:
            return MENU_V2_ITEMS;
        case ID_SUB_V2_SERVICES:
            return MENU_V2_SERVICES;
        case ID_SUB_V2_LINK:
            return MENU_V2_LINK;
        case ID_SUB_EV_TINGLE:
            return MENU_TINGLE;
        case ID_SUB_EV_STAMPEDE:
            return MENU_STAMPEDE;
        case ID_SUB_EV_CURSETAG:
            return MENU_CURSETAG;
        case ID_SUB_PLAYER:
            return MENU_PLAYER;
        case ID_SUB_ITEMS:
            return MENU_ITEMS;
        case ID_SUB_WORLD:
            return MENU_WORLD;
        case ID_SUB_FUN:
            return MENU_FUN;
        case ID_SUB_TELEPORT:
            return MENU_TELEPORT;
        case ID_SUB_CARPENTER:
            return MENU_CARPENTER;
        case ID_SUB_SETTINGS:
            return MENU_SETTINGS;
        case ID_SUB_GIVE_ITEM:
            return MENU_GIVE_ITEM;
        case ID_SUB_GIVE_MASK:
            return MENU_GIVE_MASK;
        default:
            return MENU_MAIN;
    }
}

static EntryType Entry_Type(s32 id) {
    if ((id >= ID_SUB_V2) && (id <= ID_SUB_V2_LINK)) {
        return ENTRY_SUBMENU;
    }
    if ((id == ID_V2_MUT) || (id == ID_V2_BOUNTY) || (id == ID_V2_NEM) || (id == ID_V2_POUCH) ||
        (id == ID_V2_TRACKER) || (id == ID_V2_REMOTE) || (id == ID_V2_LINK) || (id == ID_V2_AUTOSAVE) ||
        (id == ID_V2_NEWS) || (id == ID_V2_BANNERS)) {
        return ENTRY_TOGGLE;
    }
    if (id == ID_V2_NEMDIFF) {
        return ENTRY_CHOICE;
    }
    if ((id == ID_V2_BACCEPT) || (id == ID_V2_BUPGRADE) || ((id >= ID_LNK_FR_FIRST) && (id <= ID_LNK_FR_LAST)) ||
        (id == ID_LNK_APROOM) || (id == ID_LNK_EXPUNDO)) {
        return ENTRY_ACTION;
    }
    if ((id == ID_V2_NEMROW_FIRST) && (V2_NemCount() > 1)) {
        return ENTRY_ACTION; // (A: look at the next Nemesis)
    }
    if ((id == ID_V2_MARKS) || (id == ID_V2_STREAK) || (id == ID_V2_RANK) || (id == ID_V2_LINKSTATE) || ((id >= ID_V2_CON_FIRST) && (id < ID_V2_SHOP_FIRST)) ||
        ((id >= ID_V2_NEMROW_FIRST) && (id <= ID_V2_LAST))) {
        return ENTRY_INFO;
    }
    if (((id >= ID_RR_EV_FIRST) && (id <= ID_RR_EV_LAST)) || ((id >= ID_LOG_FIRST) && (id <= ID_LOG_LAST))) {
        return ENTRY_INFO;
    }
    if (((id >= ID_EV_ON_FIRST) && (id < ID_EV_ON_FIRST + EV_COUNT)) ||
        ((id >= ID_POOL_FIRST) && (id < ID_POOL_FIRST + POOL_COUNT)) ||
        ((id >= ID_CRIT_FIRST) && (id < ID_CRIT_FIRST + CRIT_COUNT))) {
        return ENTRY_TOGGLE;
    }

    switch (id) {
        case ID_SUB_EVENTS:
        case ID_SUB_EV_LIST:
        case ID_SUB_EV_START:
        case ID_SUB_EV_POOL:
        case ID_SUB_EV_CRITTERS:
        case ID_SUB_EV_MOONFALL:
        case ID_SUB_EV_SETTINGS:
        case ID_SUB_EV_NEMESIS:
        case ID_SUB_EV_PHANTOM:
        case ID_SUB_EV_TREASURE:
        case ID_SUB_EV_HUNGER:
        case ID_SUB_EV_SEARCH:
        case ID_SUB_EV_FATE:
        case ID_SUB_EV_REDLIGHT:
        case ID_SUB_EV_ECHO:
        case ID_SUB_EV_GLARE:
        case ID_SUB_EV_KAMIKAZE:
        case ID_SUB_EV_WOLFPACK:
        case ID_SUB_EV_CHAMPION:
        case ID_SUB_EV_INFIGHT:
        case ID_SUB_EV_STILL:
        case ID_SUB_EV_IMPOSTER:
        case ID_SUB_EV_UPRISING:
        case ID_SUB_EV_PROCESSION:
        case ID_SUB_EV_POLTERGEIST:
        case ID_SUB_EV_BULLETHELL:
        case ID_SUB_EV_MIRAGE:
        case ID_SUB_EV_MIRROR:
        case ID_SUB_EV_SWARM:
        case ID_SUB_EV_MAJORA:
        case ID_SUB_EV_LEVIATHAN:
        case ID_SUB_EV_FEATURES:
        case ID_SUB_EV_REPORT:
        case ID_SUB_EV_LOG:
        case ID_SUB_EV_TINGLE:
        case ID_SUB_EV_STAMPEDE:
        case ID_SUB_EV_CURSETAG:
        case ID_SUB_PLAYER:
        case ID_SUB_ITEMS:
        case ID_SUB_WORLD:
        case ID_SUB_FUN:
        case ID_SUB_TELEPORT:
        case ID_SUB_CARPENTER:
        case ID_SUB_SETTINGS:
        case ID_SUB_GIVE_ITEM:
        case ID_SUB_GIVE_MASK:
            return ENTRY_SUBMENU;

        case ID_GOD:
        case ID_MAGIC:
        case ID_RUPEES:
        case ID_MOONJUMP:
        case ID_SPEED:
        case ID_LOWGRAV:
        case ID_AMMO:
        case ID_FREEZE_ENEMIES:
        case ID_DISCO:
        case ID_BIGHEAD:
        case ID_RAINBOWLINK:
        case ID_BOMBRAIN:
        case ID_RUPEERAIN:
        case ID_C_LOOP:
        case ID_C_RAINBOW:
        case ID_C_GLOW:
        case ID_C_REVERSE:
        case ID_SOUNDS:
        case ID_EV_MASTER:
        case ID_MF_SWOOP:
        case ID_MF_MUSIC:
        case ID_MF_QUAKE:
        case ID_NEM_GLOW:
        case ID_TH_ARROW:
        case ID_TF_WALLS:
        case ID_EC_CLOSER:
        case ID_CD_FREEZE:
        case ID_EV_ROULETTE:
        case ID_EV_COMBOS:
        case ID_EV_CURSE:
        case ID_EV_PRESSURE:
        case ID_EV_SAFEZONES:
        case ID_EV_NEXTCLOCK:
        case ID_EV_WARNSOUND:
        case ID_EV_STREAKS:
        case ID_EV_WRATH:
        case ID_EV_MERCY:
        case ID_EV_WILD:
            return ENTRY_TOGGLE;

        case ID_TIMESPEED:
        case ID_LINKSIZE:
        case ID_C_SPEED:
        case ID_THEME:
        case ID_EV_INTERVAL:
        case ID_EV_DURATION:
        case ID_EV_INTENSITY:
        case ID_POOL_HORDE:
        case ID_CRIT_AMOUNT:
        case ID_MF_METEOR_SIZE:
        case ID_NEM_TYPE:
        case ID_NEM_SIZE:
        case ID_PH_SIZE:
        case ID_PH_SOLDIERS:
        case ID_PH_SHIMMER:
        case ID_TH_DIST:
        case ID_TH_TIME:
        case ID_BH_HEAL:
        case ID_BH_ENEMIES:
        case ID_SL_COUNT:
        case ID_SL_SIZE:
        case ID_SL_BACKUP:
        case ID_TF_COUNT:
        case ID_TF_SPEED:
        case ID_RL_REACT:
        case ID_RL_GREEN:
        case ID_RL_RED:
        case ID_EC_DELAY:
        case ID_MG_LENGTH:
        case ID_MG_REACT:
        case ID_KK_SWARM:
        case ID_KK_SPEED:
        case ID_WP_SIZE:
        case ID_WP_WHITE:
        case ID_CD_WHO:
        case ID_CD_HEALTH:
        case ID_IF_SIZE:
        case ID_ST_COUNT:
        case ID_ST_STILL:
        case ID_IM_CROWD:
        case ID_IM_SHARE:
        case ID_CU_COUNT:
        case ID_CU_HITS:
        case ID_PR_LENGTH:
        case ID_PG_ROCKS:
        case ID_BL_RING:
        case ID_BL_FAN:
        case ID_MI_GROUPS:
        case ID_MI_FAKES:
        case ID_MD_COUNT:
        case ID_SN_COUNT:
        case ID_MJ_SIZE:
        case ID_LV_SIZE:
        case ID_TA_RATE:
        case ID_TA_BOMBS:
        case ID_GS_LANES:
        case ID_GS_SPEED:
        case ID_CT_FUSE:
        case ID_CT_ENEMIES:
        case ID_EV_TIMERSTYLE:
        case ID_EV_HOURS:
            return ENTRY_CHOICE;

        case ID_EV_STATUS:
        case ID_EV_CURSE_INFO:
        case ID_RR_SEEN:
        case ID_RR_SURVIVED:
        case ID_RR_DEATHS:
        case ID_RR_KILLER:
        case ID_RR_KILLER_NAME:
        case ID_RR_COMBOS:
        case ID_RR_STREAK:
        case ID_RR_BEST:
        case ID_RR_WRATH:
            return ENTRY_INFO;

        default:
            return ENTRY_ACTION;
    }
}

static const char* Entry_Label(s32 id) {
    if ((id >= ID_SUB_V2) && (id <= ID_V2_LAST)) {
        const char* v2 = V2_Label(id);

        if (v2 != NULL) {
            return v2;
        }
    }
    if ((id >= ID_EV_ON_FIRST) && (id < ID_EV_ON_FIRST + EV_COUNT)) {
        return Events_Name(id - ID_EV_ON_FIRST);
    }
    if ((id >= ID_EV_GO_FIRST) && (id < ID_EV_GO_FIRST + EV_COUNT)) {
        return Events_StartLabel(id - ID_EV_GO_FIRST);
    }
    if ((id >= ID_POOL_FIRST) && (id < ID_POOL_FIRST + POOL_COUNT)) {
        return Pool_Name(id - ID_POOL_FIRST);
    }
    if ((id >= ID_CRIT_FIRST) && (id < ID_CRIT_FIRST + CRIT_COUNT)) {
        return Critter_Name(id - ID_CRIT_FIRST);
    }
    if ((id >= ID_GIVEITEM_FIRST) && (id < ID_GIVEITEM_FIRST + GIVE_ITEM_COUNT)) {
        return Give_ItemName(id - ID_GIVEITEM_FIRST);
    }
    if ((id >= ID_GIVEMASK_FIRST) && (id <= ID_GIVEMASK_LAST)) {
        return Give_MaskName(id - ID_GIVEMASK_FIRST);
    }
    if (Entry_Type(id) == ENTRY_INFO) {
        const char* label = Events_InfoLabel(id);

        if (label != NULL) {
            return label;
        }
    }
    if (id == ID_EV_ALL_ON) {
        return "Enable All";
    }
    if (id == ID_EV_ALL_OFF) {
        return "Disable All";
    }
    if (id == ID_SUB_GIVE_ITEM) {
        return "Give Item";
    }
    if (id == ID_SUB_GIVE_MASK) {
        return "Give Mask";
    }

    switch (id) {
        case ID_SUB_EVENTS:
            return "Global Events";
        case ID_SUB_EV_LIST:
            return "Choose Events";
        case ID_SUB_EV_START:
            return "Start An Event";
        case ID_SUB_EV_POOL:
            return "Blood Moon Pool";
        case ID_SUB_EV_CRITTERS:
            return "Joke Critters";
        case ID_SUB_EV_MOONFALL:
            return "Moonfall Options";
        case ID_EV_MASTER:
            return "Events Active";
        case ID_EV_STATUS:
            return "Status";
        case ID_EV_TRIGGER:
            return "Trigger Random Now";
        case ID_EV_END:
            return "End Current Event";
        case ID_EV_INTERVAL:
            return "Every";
        case ID_EV_DURATION:
            return "Lasts";
        case ID_EV_INTENSITY:
            return "Intensity";
        case ID_POOL_HORDE:
            return "Horde Size";
        case ID_POOL_ALL_ON:
            return "All Enemies On";
        case ID_POOL_ALL_OFF:
            return "All Enemies Off";
        case ID_CRIT_AMOUNT:
            return "Amount";
        case ID_MF_METEOR_SIZE:
            return "Meteor Size";
        case ID_MF_SWOOP:
            return "Moon Swoops In";
        case ID_MF_MUSIC:
            return "Final Hours Music";
        case ID_MF_QUAKE:
            return "Earthquake";
        case ID_SUB_EV_SETTINGS:
            return "Event Settings";
        case ID_SUB_EV_NEMESIS:
            return "Nemesis";
        case ID_SUB_EV_PHANTOM:
            return "Phantom Army";
        case ID_SUB_EV_TREASURE:
            return "Treasure Hunt";
        case ID_SUB_EV_HUNGER:
            return "Blood Hunger";
        case ID_SUB_EV_SEARCH:
            return "Searchlights";
        case ID_SUB_EV_FATE:
            return "Terrible Fate";
        case ID_SUB_EV_REDLIGHT:
            return "Red/Green Light";
        case ID_SUB_EV_ECHO:
            return "Echo";
        case ID_SUB_EV_GLARE:
            return "Moon's Glare";
        case ID_SUB_EV_KAMIKAZE:
            return "Kamikaze Keese";
        case ID_SUB_EV_WOLFPACK:
            return "Wolf Pack";
        case ID_SUB_EV_CHAMPION:
            return "Champion Duel";
        case ID_SUB_EV_INFIGHT:
            return "Infighting";
        case ID_SUB_EV_STILL:
            return "Time Moves w/ You";
        case ID_SUB_EV_IMPOSTER:
            return "Imposters";
        case ID_SUB_EV_UPRISING:
            return "Carpenter Uprising";
        case ID_SUB_EV_PROCESSION:
            return "Procession";
        case ID_SUB_EV_POLTERGEIST:
            return "Poltergeist";
        case ID_SUB_EV_BULLETHELL:
            return "Bullet Hell";
        case ID_SUB_EV_MIRAGE:
            return "Mirage";
        case ID_SUB_EV_MIRROR:
            return "Mirror Dance";
        case ID_SUB_EV_SWARM:
            return "Swarm Night";
        case ID_SUB_EV_MAJORA:
            return "Majora Looms";
        case ID_SUB_EV_LEVIATHAN:
            return "Sky Leviathan";
        case ID_ST_COUNT:
            return "Enemies";
        case ID_ST_STILL:
            return "When You Stop";
        case ID_IM_CROWD:
            return "Crowd Size";
        case ID_IM_SHARE:
            return "Imposters";
        case ID_CU_COUNT:
            return "Carpenters";
        case ID_CU_HITS:
            return "Hits to Stop";
        case ID_PR_LENGTH:
            return "Column Length";
        case ID_PG_ROCKS:
            return "Orbiting Rocks";
        case ID_BL_RING:
            return "Scrubs";
        case ID_BL_FAN:
            return "Nuts Per Volley";
        case ID_MI_GROUPS:
            return "Enemies";
        case ID_MI_FAKES:
            return "Fakes Each";
        case ID_MD_COUNT:
            return "Dancers";
        case ID_SN_COUNT:
            return "Swarm Size";
        case ID_MJ_SIZE:
            return "Mask Size";
        case ID_LV_SIZE:
            return "Size";
        case ID_SUB_EV_FEATURES:
            return "Event Features";
        case ID_SUB_EV_REPORT:
            return "Run Report";
        case ID_SUB_EV_TINGLE:
            return "Tingle Air Raid";
        case ID_SUB_EV_STAMPEDE:
            return "Goron Stampede";
        case ID_SUB_EV_CURSETAG:
            return "Cursed Tag";
        case ID_EV_ROULETTE:
            return "Event Roulette";
        case ID_EV_COMBOS:
            return "Combo Events";
        case ID_EV_CURSE:
            return "Curses In Draft";
        case ID_EV_PRESSURE:
            return "Moon Pressure";
        case ID_EV_SAFEZONES:
            return "Safe Zones";
        case ID_EV_TIMERSTYLE:
            return "Timer Style";
        case ID_EV_NEXTCLOCK:
            return "Next-Event Clock";
        case ID_EV_WARNSOUND:
            return "Warning Sounds";
        case ID_EV_STREAKS:
            return "Streak Cheers";
        case ID_EV_WRATH:
            return "Wrath Meter";
        case ID_EV_HOURS:
            return "Active Hours";
        case ID_EV_MERCY:
            return "Mercy Rule";
        case ID_EV_WILD:
            return "Wild Intensity";
        case ID_SUB_EV_LOG:
            return "Event Log";
        case ID_RR_RESET:
            return "Reset Report";
        case ID_TA_RATE:
            return "Drop Rate";
        case ID_TA_BOMBS:
            return "Bombs Per Drop";
        case ID_GS_LANES:
            return "Lanes At Once";
        case ID_GS_SPEED:
            return "Roll Speed";
        case ID_CT_FUSE:
            return "Fuse";
        case ID_CT_ENEMIES:
            return "Enemies Around";
        case ID_MG_LENGTH:
            return "Glare Length";
        case ID_MG_REACT:
            return "Reaction Time";
        case ID_KK_SWARM:
            return "Swarm Size";
        case ID_KK_SPEED:
            return "Dive Speed";
        case ID_WP_SIZE:
            return "Pack Size";
        case ID_WP_WHITE:
            return "White Wolfos";
        case ID_CD_WHO:
            return "Champion";
        case ID_CD_HEALTH:
            return "Health";
        case ID_CD_FREEZE:
            return "Others Hold Still";
        case ID_IF_SIZE:
            return "Group Size";
        case ID_NEM_TYPE:
            return "Enemy Type";
        case ID_NEM_SIZE:
            return "Size";
        case ID_NEM_GLOW:
            return "Red Glow";
        case ID_PH_SIZE:
            return "Army Size";
        case ID_PH_SOLDIERS:
            return "Soldiers";
        case ID_PH_SHIMMER:
            return "No-Lens Shimmer";
        case ID_TH_DIST:
            return "Distance";
        case ID_TH_TIME:
            return "Time Limit";
        case ID_TH_ARROW:
            return "Direction Arrow";
        case ID_BH_HEAL:
            return "Heal Per Kill";
        case ID_BH_ENEMIES:
            return "Max Enemies";
        case ID_SL_COUNT:
            return "Spotlights";
        case ID_SL_SIZE:
            return "Beam Size";
        case ID_SL_BACKUP:
            return "Backup Per Alarm";
        case ID_TF_COUNT:
            return "Salesmen";
        case ID_TF_SPEED:
            return "Creep Speed";
        case ID_TF_WALLS:
            return "Walls Hide Him";
        case ID_RL_REACT:
            return "Reaction Time";
        case ID_RL_GREEN:
            return "Green Light";
        case ID_RL_RED:
            return "Red Light";
        case ID_EC_DELAY:
            return "Delay";
        case ID_EC_CLOSER:
            return "Gets Closer";

        case ID_SUB_PLAYER:
            return "Player Mods";
        case ID_SUB_ITEMS:
            return "Inventory Mods";
        case ID_SUB_WORLD:
            return "World Mods";
        case ID_SUB_FUN:
            return "Fun Mods";
        case ID_SUB_TELEPORT:
            return "Teleports";
        case ID_SUB_CARPENTER:
            return "Carpenter Mods";
        case ID_SUB_SETTINGS:
            return "Settings";
        case ID_CLOSE:
            return "Close Menu";

        case ID_GOD:
            return "God Mode";
        case ID_MAGIC:
            return "Infinite Magic";
        case ID_RUPEES:
            return "Infinite Rupees";
        case ID_MOONJUMP:
            return "Moon Jump (Hold L)";
        case ID_SPEED:
            return "Super Speed";
        case ID_LOWGRAV:
            return "Low Gravity";
        case ID_REFILL:
            return "Refill Health+Magic";

        case ID_AMMO:
            return "Infinite Ammo";
        case ID_GIVE_ITEMS:
            return "Give All Items";
        case ID_GIVE_MASKS:
            return "Give All Masks";
        case ID_GIVE_QUEST:
            return "Give Songs+Remains";
        case ID_MAX_UPGRADES:
            return "Max Upgrades+Hearts";
        case ID_FAIRIES:
            return "Fill Bottles: Fairy";

        case ID_TIMESPEED:
            return "Time Speed";
        case ID_TIME_DAWN:
            return "Set Time: 6:30 AM";
        case ID_TIME_NOON:
            return "Set Time: Noon";
        case ID_TIME_DUSK:
            return "Set Time: 6:30 PM";
        case ID_TIME_MIDNIGHT:
            return "Set Time: Midnight";
        case ID_FREEZE_ENEMIES:
            return "Freeze Enemies";
        case ID_KILL_ENEMIES:
            return "Kill All Enemies";
        case ID_DISCO:
            return "Disco Lights";

        case ID_LINKSIZE:
            return "Link Size";
        case ID_BIGHEAD:
            return "Big Head Mode";
        case ID_RAINBOWLINK:
            return "Rainbow Link";
        case ID_BOMBRAIN:
            return "Bomb Rain";
        case ID_RUPEERAIN:
            return "Rupee Rain";

        case ID_SAVEPOS:
            return "Save Position";
        case ID_LOADPOS:
            return "Load Position";
        case ID_WARP_CLOCKTOWN:
            return "Warp: Clock Town";
        case ID_WARP_MILKROAD:
            return "Warp: Milk Road";
        case ID_WARP_SWAMP:
            return "Warp: Swamp";
        case ID_WARP_WOODFALL:
            return "Warp: Woodfall";
        case ID_WARP_MOUNTAIN:
            return "Warp: Mtn Village";
        case ID_WARP_SNOWHEAD:
            return "Warp: Snowhead";
        case ID_WARP_GREATBAY:
            return "Warp: Great Bay";
        case ID_WARP_ZORACAPE:
            return "Warp: Zora Cape";
        case ID_WARP_IKANA:
            return "Warp: Ikana Canyon";
        case ID_WARP_STONETOWER:
            return "Warp: Stone Tower";

        case ID_C_LOOP:
            return "Tower Loop";
        case ID_C_RAINBOW:
            return "Rainbow Colors";
        case ID_C_GLOW:
            return "Glow";
        case ID_C_SPEED:
            return "Walk Speed";
        case ID_C_REVERSE:
            return "Reverse Loop";

        case ID_THEME:
            return "Menu Color";
        case ID_SOUNDS:
            return "Menu Sounds";
        case ID_ALL_OFF:
#ifdef GE_CHEATS
            return "Turn Off All Mods";
#else
            return "Stop All Events";
#endif

        default:
            return "???";
    }
}

static s32 Entry_ChoiceCount(s32 id) {
    switch (id) {
        case ID_TIMESPEED:
            return TIMESPEED_MAX;
        case ID_LINKSIZE:
            return LINKSIZE_MAX;
        case ID_C_SPEED:
            return CSPEED_MAX;
        case ID_THEME:
            return THEME_MAX;
        case ID_EV_INTERVAL:
            return INTERVAL_MAX;
        case ID_EV_DURATION:
            return DURATION_MAX;
        case ID_EV_INTENSITY:
            return INTENSITY_MAX;
        case ID_POOL_HORDE:
            return HORDE_MAX;
        case ID_CRIT_AMOUNT:
            return CRITAMT_MAX;
        case ID_MF_METEOR_SIZE:
            return METEOR_MAX;
        case ID_NEM_TYPE:
            return NEMTYPE_MAX;
        case ID_PH_SOLDIERS:
            return SOLDIERS_MAX;
        case ID_NEM_SIZE:
        case ID_PH_SIZE:
        case ID_PH_SHIMMER:
        case ID_TH_DIST:
        case ID_BH_HEAL:
        case ID_BH_ENEMIES:
        case ID_SL_SIZE:
        case ID_TF_COUNT:
        case ID_TF_SPEED:
        case ID_RL_REACT:
        case ID_RL_GREEN:
        case ID_RL_RED:
        case ID_MG_LENGTH:
        case ID_MG_REACT:
        case ID_KK_SWARM:
        case ID_KK_SPEED:
        case ID_WP_SIZE:
        case ID_WP_WHITE:
        case ID_CD_HEALTH:
        case ID_IF_SIZE:
        case ID_ST_COUNT:
        case ID_ST_STILL:
        case ID_IM_CROWD:
        case ID_IM_SHARE:
        case ID_CU_COUNT:
        case ID_CU_HITS:
        case ID_PR_LENGTH:
        case ID_PG_ROCKS:
        case ID_BL_RING:
        case ID_BL_FAN:
        case ID_MI_GROUPS:
        case ID_MI_FAKES:
        case ID_MD_COUNT:
        case ID_SN_COUNT:
        case ID_MJ_SIZE:
        case ID_LV_SIZE:
        case ID_TA_RATE:
        case ID_TA_BOMBS:
        case ID_GS_LANES:
        case ID_GS_SPEED:
        case ID_CT_FUSE:
        case ID_CT_ENEMIES:
        case ID_EV_TIMERSTYLE:
        case ID_EV_HOURS:
        case ID_V2_NEMDIFF:
            return LVL_MAX;
        case ID_CD_WHO:
            return CHAMP_MAX;
        case ID_TH_TIME:
        case ID_SL_COUNT:
        case ID_SL_BACKUP:
        case ID_EC_DELAY:
            return STEP_MAX;
        default:
            return 2;
    }
}

static const char* Menu_Level3(s32 value, const char* a, const char* b, const char* c) {
    switch (value % LVL_MAX) {
        case LVL_LOW:
            return a;
        case LVL_MID:
            return b;
        default:
            return c;
    }
}

static const char* Menu_Step4(s32 value, const char* a, const char* b, const char* c, const char* d) {
    switch (value % STEP_MAX) {
        case STEP_1:
            return a;
        case STEP_2:
            return b;
        case STEP_3:
            return c;
        default:
            return d;
    }
}

static const char* Entry_ChoiceName(s32 id, s32 value) {
    switch (id) {
        case ID_TIMESPEED:
            return (value == TIMESPEED_FROZEN) ? "Frozen" : (value == TIMESPEED_FAST) ? "Fast" : "Normal";
        case ID_LINKSIZE:
            return (value == LINKSIZE_GIANT) ? "Giant" : (value == LINKSIZE_TINY) ? "Tiny" : "Normal";
        case ID_C_SPEED:
            return (value == CSPEED_1X) ? "1x" : (value == CSPEED_4X) ? "4x" : "2x";
        case ID_THEME:
            switch (value) {
                case THEME_RED:
                    return "Red";
                case THEME_BLUE:
                    return "Blue";
                case THEME_GREEN:
                    return "Green";
                case THEME_GOLD:
                    return "Gold";
                default:
                    return "Rainbow";
            }
        case ID_EV_INTERVAL: {
            static const char* sNames[INTERVAL_MAX] = { "1 min",  "2 min",  "3 min",  "4 min",  "5 min",
                                                        "6 min",  "7 min",  "8 min",  "9 min",  "10 min",
                                                        "15 min", "20 min", "25 min", "30 min" };

            return sNames[value % INTERVAL_MAX];
        }
        case ID_EV_DURATION: {
            static const char* sNames[DURATION_MAX] = { "15 sec", "30 sec", "1 min", "2 min",
                                                        "3 min",  "4 min",  "5 min" };

            return sNames[value % DURATION_MAX];
        }
        case ID_EV_INTENSITY: {
            static const char* sNames[INTENSITY_MAX] = { "Mild", "Normal", "Brutal" };

            return sNames[value % INTENSITY_MAX];
        }
        case ID_POOL_HORDE: {
            static const char* sNames[HORDE_MAX] = { "10", "20", "30", "40" };

            return sNames[value % HORDE_MAX];
        }
        case ID_CRIT_AMOUNT: {
            static const char* sNames[CRITAMT_MAX] = { "30", "60", "100" };

            return sNames[value % CRITAMT_MAX];
        }
        case ID_MF_METEOR_SIZE: {
            static const char* sNames[METEOR_MAX] = { "Small", "Medium", "Large" };

            return sNames[value % METEOR_MAX];
        }
        case ID_NEM_TYPE: {
            static const char* sNames[NEMTYPE_MAX] = { "Any", "Regular", "Mini-boss" };

            return sNames[value % NEMTYPE_MAX];
        }
        case ID_NEM_SIZE:
            return Menu_Level3(value, "Normal", "Big", "Huge");
        case ID_PH_SIZE:
            return Menu_Level3(value, "6", "10", "16");
        case ID_PH_SOLDIERS:
            return ((value % SOLDIERS_MAX) == SOLDIERS_STALCHILD) ? "Stalchildren" : "Mixed";
        case ID_PH_SHIMMER:
            return Menu_Level3(value, "Often", "Normal", "Rare");
        case ID_TH_DIST:
            return Menu_Level3(value, "Near", "Far", "Very Far");
        case ID_TH_TIME:
            return Menu_Step4(value, "30 sec", "45 sec", "60 sec", "90 sec");
        case ID_BH_HEAL:
            return Menu_Level3(value, "1 heart", "2 hearts", "3 hearts");
        case ID_BH_ENEMIES:
            return Menu_Level3(value, "4", "6", "8");
        case ID_SL_COUNT:
            return Menu_Step4(value, "2", "3", "4", "6");
        case ID_SL_SIZE:
            return Menu_Level3(value, "Small", "Medium", "Large");
        case ID_SL_BACKUP:
            return Menu_Step4(value, "1", "2", "3", "5");
        case ID_TF_COUNT:
            return Menu_Level3(value, "1", "2", "3");
        case ID_TF_SPEED:
            return Menu_Level3(value, "Slow", "Normal", "Fast");
        case ID_RL_REACT:
            return Menu_Level3(value, "Generous", "Normal", "Tight");
        case ID_RL_GREEN:
        case ID_RL_RED:
            return Menu_Level3(value, "Short", "Normal", "Long");
        case ID_EC_DELAY:
            return Menu_Step4(value, "2 sec", "3 sec", "4 sec", "5 sec");
        case ID_MG_LENGTH:
            return Menu_Level3(value, "Short", "Normal", "Long");
        case ID_MG_REACT:
            return Menu_Level3(value, "Generous", "Normal", "Tight");
        case ID_KK_SWARM:
            return Menu_Level3(value, "4", "7", "10");
        case ID_KK_SPEED:
            return Menu_Level3(value, "Slow", "Normal", "Fast");
        case ID_WP_SIZE:
            return Menu_Level3(value, "3", "5", "8");
        case ID_WP_WHITE:
            return Menu_Level3(value, "None", "Some", "All");
        case ID_CD_WHO: {
            static const char* sNames[CHAMP_MAX] = { "Any", "Iron Knuckle", "Dinolfos", "Garo Master" };

            return sNames[value % CHAMP_MAX];
        }
        case ID_CD_HEALTH:
            return Menu_Level3(value, "1x", "1.5x", "2x");
        case ID_IF_SIZE:
            return Menu_Level3(value, "2 vs 2", "3 vs 3", "5 vs 5");
        case ID_ST_COUNT:
            return Menu_Level3(value, "6", "10", "14");
        case ID_ST_STILL:
            return Menu_Level3(value, "Frozen", "Near-Frozen", "Crawl");
        case ID_IM_CROWD:
            return Menu_Level3(value, "6", "10", "14");
        case ID_IM_SHARE:
            return Menu_Level3(value, "1 in 4", "1 in 3", "1 in 2");
        case ID_CU_COUNT:
            return Menu_Level3(value, "4", "6", "9");
        case ID_CU_HITS:
            return Menu_Level3(value, "2", "3", "5");
        case ID_PR_LENGTH:
            return Menu_Level3(value, "8", "12", "16");
        case ID_PG_ROCKS:
            return Menu_Level3(value, "4", "6", "8");
        case ID_BL_RING:
            return Menu_Level3(value, "4", "6", "8");
        case ID_BL_FAN:
            return Menu_Level3(value, "3", "5", "7");
        case ID_MI_GROUPS:
            return Menu_Level3(value, "2", "3", "4");
        case ID_MI_FAKES:
            return Menu_Level3(value, "1", "2", "3");
        case ID_MD_COUNT:
            return Menu_Level3(value, "3", "5", "7");
        case ID_SN_COUNT:
            return Menu_Level3(value, "16", "24", "32");
        case ID_MJ_SIZE:
            return Menu_Level3(value, "Big", "Huge", "Colossal");
        case ID_LV_SIZE:
            return Menu_Level3(value, "Big", "Huge", "Colossal");
        case ID_TA_RATE:
        case ID_GS_SPEED:
            return Menu_Level3(value, "Slow", "Normal", "Fast");
        case ID_TA_BOMBS:
        case ID_GS_LANES:
            return Menu_Level3(value, "1", "2", "3");
        case ID_CT_FUSE:
            return Menu_Level3(value, "5 sec", "10 sec", "15 sec");
        case ID_CT_ENEMIES:
            return Menu_Level3(value, "3", "4", "6");
        case ID_EV_TIMERSTYLE:
            return Menu_Level3(value, "Full", "Compact", "Hidden");
        case ID_EV_HOURS:
            return Menu_Level3(value, "Any Time", "Day Only", "Night Only");
        case ID_V2_NEMDIFF:
            return Menu_Level3(value, "Easy", "Normal", "Hard");
        default:
            return "";
    }
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

#define VISIBLE_ROWS 13
#define TOAST_FRAMES 50

static s32 sPage = MENU_MAIN;
static s32 sCursor[MENU_COUNT];
static s32 sScroll[MENU_COUNT];

// Pages whose rows change (Nemesis traits) can shrink under the cursor.
static void Menu_ClampCursor(void) {
    s32 count;

    Menu_Entries(sPage, &count);
    if ((count > 0) && (sCursor[sPage] >= count)) {
        sCursor[sPage] = count - 1;
    }
    if (sScroll[sPage] > sCursor[sPage]) {
        sScroll[sPage] = sCursor[sPage];
    }
}
static s32 sInputBlockFrames = 0;
static s32 sHoldFrames = 0;
static s32 sOpenFrames = 0;
static const char* sToastText = NULL;
static s32 sToastTimer = 0;
static char sToastBuf[40];
static char sToastQueue[5][40];
static s32 sToastQueued = 0;

static void Toast_Copy(char* dst, const char* src) {
    s32 k;

    for (k = 0; (src[k] != '\0') && (k < 38); k++) {
        dst[k] = src[k];
    }
    dst[k] = '\0';
}

static s32 Toast_Same(const char* a, const char* b) {
    s32 k;

    for (k = 0; k < 38; k++) {
        if (a[k] != b[k]) {
            return false;
        }
        if (a[k] == '\0') {
            return true;
        }
    }
    return true;
}
static PlayState* sDrawPlay = NULL;
static s32 sDefaultsSet = false;

static void Menu_SetDefaults(void) {
    if (sDefaultsSet) {
        return;
    }
    sDefaultsSet = true;
    gOpt[ID_C_LOOP] = true;
    gOpt[ID_C_RAINBOW] = true;
    gOpt[ID_C_GLOW] = true;
    gOpt[ID_C_SPEED] = CSPEED_2X;
    gOpt[ID_SOUNDS] = true;
    gOpt[ID_THEME] = THEME_BLUE; // rainbow is still in Menu Settings
    Events_SetDefaults();
    Menu_BuildLists();
}

RECOMP_CALLBACK("*", recomp_on_init) void CarpenterMenu_OnInit(void) {
    Events_Init();
    Menu_SetDefaults();
}

void Menu_PlaySfx(u16 sfxId) {
    if (gOpt[ID_SOUNDS]) {
        Audio_PlaySfx(sfxId);
    }
}

// ---------------------------------------------------------------------------
// V3 pop-ups: up to 4 stacked at the bottom of the screen, 5 seconds each with
// a fade, coloured by where they came from. In the menu, feedback shows as a
// box in the middle instead.
// ---------------------------------------------------------------------------

#define NOTE_MAX 4
#define NOTE_FRAMES (5 * 20)
#define NOTE_WIDTH 36

typedef struct {
    char line[2][NOTE_WIDTH + 2];
    u8 lines;
    u8 kind;
    s16 timer;
} PopNote;

static PopNote sNotes[NOTE_MAX]; // [0] is the newest
static char sBannerTitle[40];
static char sBannerSub[2][NOTE_WIDTH + 2];
static s32 sBannerTimer = 0;

static s32 Note_Same(const PopNote* n, const char* text) {
    char joined[80];
    s32 k = 0;
    s32 i;

    for (i = 0; (n->line[0][i] != '\0') && (k < 78); i++) {
        joined[k++] = n->line[0][i];
    }
    if (n->lines > 1) {
        joined[k++] = ' ';
        for (i = 0; (n->line[1][i] != '\0') && (k < 78); i++) {
            joined[k++] = n->line[1][i];
        }
    }
    joined[k] = '\0';
    for (i = 0; (joined[i] != '\0') && (text[i] == joined[i]); i++) {
    }
    return (joined[i] == '\0') && (text[i] == '\0');
}

// Splits text over up to two lines at a space.
static s32 Note_Wrap(const char* text, char out[2][NOTE_WIDTH + 2]) {
    s32 len = Ui_StrLen(text);
    s32 cut = len;
    s32 i;
    s32 k;

    out[0][0] = out[1][0] = '\0';
    if (len > NOTE_WIDTH) {
        cut = NOTE_WIDTH;
        for (i = NOTE_WIDTH; i > NOTE_WIDTH / 2; i--) {
            if (text[i] == ' ') {
                cut = i;
                break;
            }
        }
    }
    for (k = 0; (k < cut) && (k < NOTE_WIDTH); k++) {
        out[0][k] = text[k];
    }
    out[0][k] = '\0';
    if (cut >= len) {
        return 1;
    }
    i = cut;
    while (text[i] == ' ') {
        i++;
    }
    for (k = 0; (text[i + k] != '\0') && (k < NOTE_WIDTH); k++) {
        out[1][k] = text[i + k];
    }
    out[1][k] = '\0';
    return 2;
}

void Menu_Notify(const char* text, s32 kind) {
    s32 i;

    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }
    if ((sNotes[0].timer > 0) && Note_Same(&sNotes[0], text)) {
        sNotes[0].timer = NOTE_FRAMES;
        return;
    }
    for (i = NOTE_MAX - 1; i > 0; i--) {
        sNotes[i] = sNotes[i - 1];
    }
    sNotes[0].lines = Note_Wrap(text, sNotes[0].line);
    sNotes[0].kind = kind;
    sNotes[0].timer = NOTE_FRAMES;
}

void Menu_ShowBanner(const char* title, const char* sub) {
    char joined[80];
    s32 n;
    s32 i;

    if (!gOpt[ID_V2_BANNERS]) {
        for (n = 0; (title[n] != '\0') && (n < 40); n++) {
            joined[n] = title[n];
        }
        joined[n++] = ' ';
        for (i = 0; (sub[i] != '\0') && (n < 78); i++) {
            joined[n++] = sub[i];
        }
        joined[n] = '\0';
        Menu_Notify(joined, NOTE_NEMESIS);
        return;
    }
    for (n = 0; (title[n] != '\0') && (n < 38); n++) {
        sBannerTitle[n] = title[n];
    }
    sBannerTitle[n] = '\0';
    Note_Wrap(sub, sBannerSub);
    sBannerTimer = 70;
}

void Menu_ShowToast(const char* text) {
    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }
    if (!gMenuOpen) {
        Menu_Notify(text, NOTE_PLAIN);
        return;
    }
    // In the menu: the feedback box replaces right away.
    Toast_Copy(sToastBuf, text);
    sToastText = sToastBuf;
    sToastTimer = TOAST_FRAMES;
}

static void Notes_Tick(void) {
    s32 i;

    if (sToastTimer > 0) {
        sToastTimer--;
    }
    if (gMenuOpen) {
        return;
    }
    for (i = 0; i < NOTE_MAX; i++) {
        if (sNotes[i].timer > 0) {
            sNotes[i].timer--;
        }
    }
    if (sBannerTimer > 0) {
        sBannerTimer--;
    }
}

static s32 Notes_Any(void) {
    s32 i;

    if (sBannerTimer > 0) {
        return true;
    }
    for (i = 0; i < NOTE_MAX; i++) {
        if (sNotes[i].timer > 0) {
            return true;
        }
    }
    return false;
}

static void Notes_Color(s32 kind, u8* r, u8* g, u8* b) {
    static const u8 sCol[6][3] = {
        { 240, 240, 240 }, { 200, 175, 255 }, { 255, 255, 170 }, { 255, 140, 140 }, { 255, 170, 110 }, { 190, 200, 255 },
    };
    s32 k = ((kind >= 0) && (kind < 6)) ? kind : 0;

    *r = sCol[k][0];
    *g = sCol[k][1];
    *b = sCol[k][2];
}

static void Notes_Draw(PlayState* play, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    GfxPrint printer;
    s32 y = 226;
    s32 i;
    s32 k;

    // Backings first, newest at the bottom.
    for (i = 0; i < NOTE_MAX; i++) {
        PopNote* n = &sNotes[i];
        s32 h = (n->lines > 1) ? 21 : 12;
        s32 w = 0;
        u8 alpha;

        if (n->timer <= 0) {
            continue;
        }
        alpha = (n->timer > 20) ? 150 : (u8)(n->timer * 7);
        for (k = 0; k < n->lines; k++) {
            w = MAX(w, Ui_StrLen(n->line[k]) * 8);
        }
        w += 12;
        gfx = Ui_DrawRect(gfx, (SCREEN_WIDTH - w) / 2, y - h + 10, (SCREEN_WIDTH + w) / 2, y + 12, 0, 0, 0, alpha);
        y -= h + 3;
    }
    if (sBannerTimer > 0) {
        u8 a = (sBannerTimer > 15) ? 170 : (u8)(sBannerTimer * 11);

        gfx = Ui_DrawRect(gfx, 0, 92, SCREEN_WIDTH, 128 + ((sBannerSub[1][0] != '\0') ? 10 : 0), 20, 0, 40, a);
    }
    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2); // text shadow
    y = 226;
    for (i = 0; i < NOTE_MAX; i++) {
        PopNote* n = &sNotes[i];
        s32 h = (n->lines > 1) ? 21 : 12;
        u8 r, g, b;
        u8 alpha;

        if (n->timer <= 0) {
            continue;
        }
        alpha = (n->timer > 20) ? 255 : (u8)(n->timer * 12);
        Notes_Color(n->kind, &r, &g, &b);
        GfxPrint_SetColor(&printer, r, g, b, alpha);
        if (n->lines > 1) {
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[0]) * 8) / 2, y - 9, n->line[0]);
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[1]) * 8) / 2, y + 1, n->line[1]);
        } else {
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[0]) * 8) / 2, y + 1, n->line[0]);
        }
        y -= h + 3;
    }
    if (sBannerTimer > 0) {
        u8 a = (sBannerTimer > 15) ? 255 : (u8)(sBannerTimer * 17);
        u8 r, g, b;

        HueToRgb((play->gameplayFrames * 6) % 360, &r, &g, &b);
        GfxPrint_SetColor(&printer, r, g, b, a);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sBannerTitle) * 8) / 2, 98, sBannerTitle);
        GfxPrint_SetColor(&printer, 230, 230, 255, a);
        for (k = 0; k < 2; k++) {
            if (sBannerSub[k][0] != '\0') {
                Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sBannerSub[k]) * 8) / 2, 112 + k * 10, sBannerSub[k]);
            }
        }
    }
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    *gfxP = gfx;
}

void Menu_OpenV2(PlayState* play, s32 which) {
    Menu_Open(play);
    if (which == 2) {
        sTab = 1; // Bounty Board
    } else if (which == 0) {
        sTab = 0;
    }
    sPage = (which == 1) ? MENU_V2_DRAFT : sTabs[sTab];
    sCursor[sPage] = 0;
    sScroll[sPage] = 0;
    Player_SetCsActionWithHaltedActors(play, NULL, PLAYER_CSACTION_WAIT);
}

void Menu_Open(PlayState* play) {
    Menu_SetDefaults();
    gMenuOpen = true;
    gMenuAtBoard = false;
    sPage = sTabs[sTab % ARRAY_COUNT(sTabs)]; // the tab you were on last
    sOpenFrames = 0;
    sHoldFrames = 0;
    Menu_PlaySfx(NA_SE_SY_WIN_OPEN);
}

static void Menu_Close(PlayState* play) {
    gMenuOpen = false;
    // Swallow input for a few frames so letting go of B doesn't swing the sword.
    sInputBlockFrames = 4;
    Menu_PlaySfx(NA_SE_SY_WIN_CLOSE);

    // Release Link from the silent talk (or the hold from opening it with the ocarina).
    if (CutsceneManager_GetCurrentCsId() == CS_ID_GLOBAL_TALK) {
        CutsceneManager_Stop(CS_ID_GLOBAL_TALK);
    }
    Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
}

// The mutator draft is a choice that has to be made: nothing forces it shut.
s32 Menu_IsDraft(void) {
    return gMenuOpen && (sPage == MENU_V2_DRAFT);
}

// Friend Link's Forced Ambush pulls Link out of the menu.
void Menu_ForceClose(PlayState* play) {
    if (gMenuOpen) {
        Menu_Close(play);
    }
}

// ---------------------------------------------------------------------------
// Remote access: playing Up Up Down Down (C buttons) on
// the ocarina opens the menu anywhere.
// ---------------------------------------------------------------------------

#define MENU_SONG_LEN 4

static const u8 sMenuSong[MENU_SONG_LEN] = {
    OCARINA_BTN_C_UP, OCARINA_BTN_C_UP, OCARINA_BTN_C_DOWN, OCARINA_BTN_C_DOWN,
};
static u8 sNoteBuf[MENU_SONG_LEN];
static s32 sNoteCount = 0;
static s32 sLastNotePos = 0;
static s32 sRemoteOpenTimer = 0; // counts down while waiting for the ocarina to be put away

static void Remote_ResetNotes(void) {
    sNoteCount = 0;
    sLastNotePos = 0;
}

static s32 Remote_SongPlayed(void) {
    s32 i;

    if (sNoteCount < MENU_SONG_LEN) {
        return false;
    }
    for (i = 0; i < MENU_SONG_LEN; i++) {
        if (sNoteBuf[(sNoteCount - MENU_SONG_LEN + i) % MENU_SONG_LEN] != sMenuSong[i]) {
            return false;
        }
    }
    return true;
}

static s32 Remote_CanOpenNow(PlayState* play) {
    Player* player = GET_PLAYER(play);

    return (player != NULL) && !gMenuOpen && (play->msgCtx.msgMode == MSGMODE_NONE) &&
           (CutsceneManager_GetCurrentCsId() == CS_ID_NONE) && !(player->stateFlags2 & PLAYER_STATE2_USING_OCARINA) &&
           (player->csAction == PLAYER_CSACTION_NONE) && !(player->stateFlags1 & PLAYER_STATE1_DEAD) &&
           (play->transitionTrigger == TRANS_TRIGGER_OFF) && (play->transitionMode == TRANS_MODE_OFF) &&
           !IS_PAUSED(&play->pauseCtx);
}

static void Remote_Update(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;
    s32 pos;

    // The song was played: open the menu once Link has put the ocarina away.
    if (sRemoteOpenTimer > 0) {
        sRemoteOpenTimer--;
        if (Remote_CanOpenNow(play)) {
            sRemoteOpenTimer = 0;
            Menu_Open(play);
            // Hold Link in place and pause enemies while the menu is up.
            Player_SetCsActionWithHaltedActors(play, NULL, PLAYER_CSACTION_WAIT);
        }
        return;
    }

    // Only listen while the ocarina is out for free playing.
    if ((msgCtx->msgMode != MSGMODE_OCARINA_PLAYING) || (msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY) ||
        (msgCtx->ocarinaStaff == NULL)) {
        Remote_ResetNotes();
        return;
    }

    // Each new note moves the staff position forward (it wraps after 8).
    pos = msgCtx->ocarinaStaff->pos;
    if ((pos != 0) && (pos != sLastNotePos)) {
        sNoteBuf[sNoteCount % MENU_SONG_LEN] = msgCtx->ocarinaStaff->buttonIndex;
        sNoteCount++;
        if (sNoteCount >= 2 * MENU_SONG_LEN) {
            sNoteCount -= MENU_SONG_LEN;
        }
    }
    sLastNotePos = pos;

    if (Remote_SongPlayed()) {
        Remote_ResetNotes();
        // End the ocarina the same way the game does after a special song.
        Message_CloseTextbox(play);
        msgCtx->ocarinaMode = OCARINA_MODE_END;
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
        AudioOcarina_SetOcarinaDisableTimer(0, 20);
        sRemoteOpenTimer = 4 * 20;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void Menu_MoveCursor(s32 delta) {
    s32 count;

    Menu_Entries(sPage, &count);
    sCursor[sPage] = (sCursor[sPage] + delta + count) % count;

    if (sCursor[sPage] < sScroll[sPage]) {
        sScroll[sPage] = sCursor[sPage];
    } else if (sCursor[sPage] >= sScroll[sPage] + VISIBLE_ROWS) {
        sScroll[sPage] = sCursor[sPage] - VISIBLE_ROWS + 1;
    }
    Menu_PlaySfx(NA_SE_SY_CURSOR);
}

static void Menu_ChangeValue(PlayState* play, s32 id, s32 delta) {
    EntryType type = Entry_Type(id);

    if (type == ENTRY_TOGGLE) {
        gOpt[id] = !gOpt[id];
    } else if (type == ENTRY_CHOICE) {
        s32 count = Entry_ChoiceCount(id);

        gOpt[id] = (gOpt[id] + delta + count) % count;
#ifdef GE_CHEATS
    } else if ((id == ID_EV_CURSE_INFO) && gOpt[ID_EV_CURSE]) {
        Events_CycleCurse(delta);
        Menu_PlaySfx(NA_SE_SY_DECIDE);
        return;
#endif
    } else {
        return;
    }

    Mods_OnOptionChanged(play, id);
    Carpenter_OnOptionChanged(play, id);
    Events_OnOptionChanged(play, id);
    Menu_PlaySfx(NA_SE_SY_DECIDE);
}

static void Menu_Select(PlayState* play) {
    s32 count;
    const u16* entries = Menu_Entries(sPage, &count);
    s32 id = entries[sCursor[sPage]];

    if ((id >= ID_V2_BNT_FIRST) && (id < ID_V2_CON_FIRST)) {
        // Bounty rows open their details (traits and what they do, accept/drop).
        gV2InfoBounty = id - ID_V2_BNT_FIRST;
        sPage = MENU_V2_BINFO;
        sCursor[sPage] = 0;
        sScroll[sPage] = 0;
        Menu_PlaySfx(NA_SE_SY_DECIDE);
        return;
    }
    switch (Entry_Type(id)) {
        case ENTRY_SUBMENU:
            sPage = Menu_SubmenuTarget(id);
            Menu_PlaySfx(NA_SE_SY_DECIDE);
            break;

        case ENTRY_INFO:
            break;

        case ENTRY_TOGGLE:
        case ENTRY_CHOICE:
            Menu_ChangeValue(play, id, 1);
            break;

        case ENTRY_ACTION:
            if (id == ID_CLOSE) {
                Menu_Close(play);
                return;
            }
            Menu_PlaySfx(NA_SE_SY_DECIDE);
            if (Events_RunAction(play, id)) {
                // An event was started: get Link back into the action.
                Menu_Close(play);
                return;
            }
            Mods_RunAction(play, id);
            // Warping or moving Link needs him out of the menu's talk state.
            if (Mods_WarpPending() || (id == ID_LOADPOS)) {
                Menu_Close(play);
            }
            break;
    }
}

static void Menu_HandleInput(PlayState* play, Input* input) {
    u16 press = input->press.button;
    u16 cur = input->cur.button;
    s32 stickY = input->cur.stick_y;
    s32 stickX = input->cur.stick_x;
    s32 dirY = 0;
    s32 dirX = 0;
    s32 count;
    const u16* entries;

    // Ignore the first couple of frames so the A press that opened the menu
    // doesn't also select something.
    if (sOpenFrames < 3) {
        sOpenFrames++;
        return;
    }
    Menu_ClampCursor();

    if (CHECK_BTN_ANY(cur, BTN_DUP) || (stickY > 45)) {
        dirY = -1;
    } else if (CHECK_BTN_ANY(cur, BTN_DDOWN) || (stickY < -45)) {
        dirY = 1;
    }
    if (CHECK_BTN_ANY(cur, BTN_DLEFT) || (stickX < -45)) {
        dirX = -1;
    } else if (CHECK_BTN_ANY(cur, BTN_DRIGHT) || (stickX > 45)) {
        dirX = 1;
    }

    // Held-direction repeat: move once, pause, then repeat quickly.
    if ((dirY != 0) || (dirX != 0)) {
        s32 fire = (sHoldFrames == 0) || ((sHoldFrames >= 8) && ((sHoldFrames % 3) == 0));

        sHoldFrames++;
        if (dirY != 0) {
            if (fire) {
                Menu_MoveCursor(dirY);
            }
        } else if (sHoldFrames == 1) {
            // Left/right changes a value once per press (no auto-repeat).
            // On the event list, Right opens the event's own page.
            entries = Menu_Entries(sPage, &count);
            if ((sPage == MENU_EV_LIST) && (dirX > 0) && (entries[sCursor[sPage]] >= ID_EV_ON_FIRST) &&
                (entries[sCursor[sPage]] < ID_EV_ON_FIRST + EV_COUNT)) {
                gEvDetail = entries[sCursor[sPage]] - ID_EV_ON_FIRST;
                sPage = MENU_EV_DETAIL;
                sCursor[sPage] = 0;
                sScroll[sPage] = 0;
                Menu_PlaySfx(NA_SE_SY_DECIDE);
            } else {
                Menu_ChangeValue(play, entries[sCursor[sPage]], dirX);
            }
        }
    } else {
        sHoldFrames = 0;
    }

    // Tabs: L / R (or Z) flip between the top-level pages.
    if ((sPage != MENU_V2_DRAFT) && CHECK_BTN_ANY(press, BTN_L | BTN_R | BTN_Z)) {
        s32 dir = CHECK_BTN_ANY(press, BTN_R) ? 1 : -1;

        sTab = (sTab + dir + ARRAY_COUNT(sTabs)) % ARRAY_COUNT(sTabs);
        sPage = sTabs[sTab];
        Menu_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }
    if (CHECK_BTN_ANY(press, BTN_A)) {
        Menu_Select(play);
    } else if (CHECK_BTN_ANY(press, BTN_B)) {
        if (sPage == MENU_V2_DRAFT) {
            // A mutator must be chosen.
            Menu_ShowToast("Pick one with A.");
        } else if (Menu_IsTabRoot(sPage)) {
            Menu_Close(play);
        } else {
            sPage = Menu_Parent(sPage);
            Menu_PlaySfx(NA_SE_SY_CANCEL);
        }
    }
}

static void Menu_ClearInput(Input* input) {
    input->cur.button = 0;
    input->cur.stick_x = 0;
    input->cur.stick_y = 0;
    input->press.button = 0;
    input->press.stick_x = 0;
    input->press.stick_y = 0;
    input->rel.button = 0;
    input->rel.stick_x = 0;
    input->rel.stick_y = 0;
}

// Runs every frame right before the game's own update.
RECOMP_CALLBACK("*", recomp_on_play_update) void CarpenterMenu_OnPlayUpdate(PlayState* play) {
    Input* input = CONTROLLER1(&play->state);

    Notes_Tick();

    if (!gMenuOpen) {
        Remote_Update(play);
    }

    if (gMenuOpen) {
        // Close automatically if the game moved on (loading zone, etc.).
        if ((play->transitionTrigger != TRANS_TRIGGER_OFF) || IS_PAUSED(&play->pauseCtx)) {
            gMenuOpen = false;
            Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
        } else {
            Menu_HandleInput(play, input);
            Menu_ClearInput(input);
        }
    } else if (sInputBlockFrames > 0) {
        sInputBlockFrames--;
        Menu_ClearInput(input);
    }

    Mods_OnPlayUpdate(play);
    Events_Update(play);
}

RECOMP_CALLBACK("*", recomp_after_play_init) void CarpenterMenu_OnPlayInit(PlayState* play) {
    gMenuOpen = false;
    sInputBlockFrames = 0;
    sRemoteOpenTimer = 0;
    Remote_ResetNotes();
    Carpenter_OnPlayInit(play);
    Mods_OnPlayInit(play);
    Events_OnPlayInit(play);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

#define GFXP_FLAG_SHADOW_BIT (1 << 2)

#define PANEL_X 6
#define PANEL_W 308
#define PANEL_TOP 8
#define HEADER_H 30
#define LIST_TOP (PANEL_TOP + HEADER_H + 6)
#define ROW_H 11
#define FOOTER_Y (LIST_TOP + VISIBLE_ROWS * ROW_H + 4)
#define PANEL_BOTTOM (FOOTER_Y + 33)

static void Menu_ThemeColor(PlayState* play, s32 hueOffset, u8* r, u8* g, u8* b) {
    switch (gOpt[ID_THEME]) {
        case THEME_RED:
            *r = 220, *g = 30, *b = 40;
            break;
        case THEME_BLUE:
            *r = 40, *g = 110, *b = 255;
            break;
        case THEME_GREEN:
            *r = 30, *g = 200, *b = 70;
            break;
        case THEME_GOLD:
            *r = 235, *g = 180, *b = 30;
            break;
        default:
            HueToRgb(BaseHue(play) * 2 + hueOffset, r, g, b);
            break;
    }
}

Gfx* Ui_DrawRect(Gfx* gfx, s32 x1, s32 y1, s32 x2, s32 y2, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_DISABLE | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillRectangle(gfx++, x1, y1, x2, y2);
    return gfx;
}

s32 Ui_StrLen(const char* s) {
    s32 n = 0;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void Ui_Print(GfxPrint* printer, s32 x, s32 y, const char* text) {
    GfxPrint_SetPosPx(printer, x, y);
    GfxPrint_PrintString(printer, text);
}

static void Menu_DrawPanel(PlayState* play, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    GfxPrint printer;
    s32 count;
    const u16* entries = Menu_Entries(sPage, &count);
    s32 scroll = sScroll[sPage];
    s32 cursor;

    Menu_ClampCursor();
    cursor = sCursor[sPage];
    s32 i;
    u8 r, g, b;
    char pageLine[32];
    char statusText[24];

    // Pin the panel to the left edge of the screen (also in widescreen), and
    // widen the drawing area to the whole screen so the part left of the
    // original 4:3 area isn't cut off (same approach the recomp's HUD uses).
    gEXSetRectAlign(gfx++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gEXSetScissorAlign(gfx++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, -SCREEN_WIDTH, 0, 0, 0, SCREEN_WIDTH,
                       SCREEN_HEIGHT);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Backdrop and borders.
    gfx = Ui_DrawRect(gfx, PANEL_X, PANEL_TOP, PANEL_X + PANEL_W, PANEL_BOTTOM, 0, 0, 0, 175);
    Menu_ThemeColor(play, 0, &r, &g, &b);
    gfx = Ui_DrawRect(gfx, PANEL_X, PANEL_TOP, PANEL_X + PANEL_W, PANEL_TOP + HEADER_H, r, g, b, 120);
    gfx = Ui_DrawRect(gfx, PANEL_X, PANEL_TOP + HEADER_H, PANEL_X + PANEL_W, PANEL_TOP + HEADER_H + 2, r, g, b,
                        255);
    gfx = Ui_DrawRect(gfx, PANEL_X, PANEL_TOP, PANEL_X + 2, PANEL_BOTTOM, r, g, b, 255);
    gfx = Ui_DrawRect(gfx, PANEL_X + PANEL_W - 2, PANEL_TOP, PANEL_X + PANEL_W, PANEL_BOTTOM, r, g, b, 255);
    gfx = Ui_DrawRect(gfx, PANEL_X, PANEL_BOTTOM - 2, PANEL_X + PANEL_W, PANEL_BOTTOM, r, g, b, 255);
    gfx = Ui_DrawRect(gfx, PANEL_X, FOOTER_Y - 2, PANEL_X + PANEL_W, FOOTER_Y - 1, r, g, b, 160);

    // Highlight bar behind the selected row.
    if ((cursor >= scroll) && (cursor < scroll + VISIBLE_ROWS)) {
        s32 rowY = LIST_TOP + (cursor - scroll) * ROW_H;

        Menu_ThemeColor(play, 40, &r, &g, &b);
        gfx = Ui_DrawRect(gfx, PANEL_X + 4, rowY - 2, PANEL_X + PANEL_W - 4, rowY + 9, r, g, b, 200);
    }

    // Scrollbar when the page is longer than the panel.
    if (count > VISIBLE_ROWS) {
        s32 trackTop = LIST_TOP - 1;
        s32 trackH = VISIBLE_ROWS * ROW_H;
        s32 thumbH = trackH * VISIBLE_ROWS / count;
        s32 thumbY = trackTop + (trackH - thumbH) * scroll / (count - VISIBLE_ROWS);

        gfx = Ui_DrawRect(gfx, PANEL_X + PANEL_W - 7, trackTop, PANEL_X + PANEL_W - 5, trackTop + trackH, 80, 80,
                            80, 200);
        gfx = Ui_DrawRect(gfx, PANEL_X + PANEL_W - 7, thumbY, PANEL_X + PANEL_W - 5, thumbY + thumbH, 255, 255, 255,
                            230);
    }

    // Nemesis bars: level (toward its next trait) and its 4 stats (20 fills
    // them, then they glow rainbow; ticks at the unique traits, 5 and 10).
    for (i = 0; (i < VISIBLE_ROWS) && (scroll + i < count); i++) {
        s32 id = entries[scroll + i];

        if ((id == ID_V2_NEMROW_FIRST + 2) || ((id >= ID_V2_NEMROW_FIRST + 8) && (id <= ID_V2_NEMROW_FIRST + 11))) {
            s32 val = V2_NemBar(id);
            s32 rowY = LIST_TOP + i * ROW_H;
            s32 bx = PANEL_X + 110;
            s32 bw = 120;
            s32 fill = bw * CLAMP(val, 0, 1000) / 1000;
            u8 cr, cg, cb;

            if (val < 0) {
                continue;
            }
            if (val >= 1000) {
                HueToRgb((play->gameplayFrames * 9 + i * 50) % 360, &cr, &cg, &cb);
            } else {
                Menu_ThemeColor(play, 0, &cr, &cg, &cb);
            }
            gfx = Ui_DrawRect(gfx, bx, rowY + 1, bx + bw, rowY + 7, 30, 30, 40, 220);
            gfx = Ui_DrawRect(gfx, bx, rowY + 1, bx + fill, rowY + 7, cr, cg, cb, 240);
            if (id != ID_V2_NEMROW_FIRST + 2) {
                gfx = Ui_DrawRect(gfx, bx + bw / 4, rowY, bx + bw / 4 + 1, rowY + 8, 220, 220, 220, 230);
                gfx = Ui_DrawRect(gfx, bx + bw / 2, rowY, bx + bw / 2 + 1, rowY + 8, 220, 220, 220, 230);
            }
        }
    }

    // Text.
    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT); // crisp letters: no bleed from the next glyph when upscaled
    printer.flags |= GFXP_FLAG_SHADOW_BIT;

    // Header: the tab (or page) name, your Moon Marks in gold, and the tab strip.
    {
        const char* title = Menu_PageTitle(sPage);
        s32 len = Ui_StrLen(title);
        s32 n = 0;
        s32 k;
        char marks[24];

        if (Menu_IsTabRoot(sPage)) {
            pageLine[n++] = '<';
            pageLine[n++] = ' ';
        }
        for (k = 0; (k < len) && (n < 26); k++) {
            pageLine[n++] = title[k];
        }
        if (Menu_IsTabRoot(sPage)) {
            pageLine[n++] = ' ';
            pageLine[n++] = '>';
        }
        pageLine[n] = '\0';
        if (gOpt[ID_THEME] == THEME_RAINBOW) {
            GfxPrint_SetColor(&printer, 255, 255, 255, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 5, GFXP_RAINBOW_ON "MOON MENU" GFXP_RAINBOW_OFF);
        } else {
            GfxPrint_SetColor(&printer, 220, 220, 255, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 5, "MOON MENU");
        }
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        Ui_Print(&printer, PANEL_X + (PANEL_W - n * 8) / 2, PANEL_TOP + 5, pageLine);
        n = Ui_StrLen(V2_Value(ID_V2_MARKS, marks, sizeof(marks), &r, &g, &b));
        k = n;
        n = n + 0;
        marks[k] = '\0';
        {
            char m2[32];
            s32 q = 0;
            s32 t;

            for (t = 0; marks[t] != '\0'; t++) {
                m2[q++] = marks[t];
            }
            m2[q++] = ' ';
            m2[q++] = 'M';
            m2[q] = '\0';
            GfxPrint_SetColor(&printer, 255, 215, 90, 255);
            Ui_Print(&printer, PANEL_X + PANEL_W - 10 - q * 8, PANEL_TOP + 5, m2);
        }
        // Tab strip: a dot per tab, the current one lit.
        if (sPage != MENU_V2_DRAFT) {
            s32 x0 = PANEL_X + (PANEL_W - (s32)ARRAY_COUNT(sTabs) * 12) / 2;

            for (k = 0; k < (s32)ARRAY_COUNT(sTabs); k++) {
                s32 on = (sTabs[k] == sTabs[sTab]);

                GfxPrint_SetColor(&printer, on ? 255 : 120, on ? 230 : 120, on ? 120 : 140, 255);
                Ui_Print(&printer, x0 + k * 12, PANEL_TOP + 17, on ? "o" : ".");
            }
            GfxPrint_SetColor(&printer, 150, 150, 180, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 17, "L");
            Ui_Print(&printer, PANEL_X + PANEL_W - 18, PANEL_TOP + 17, "R");
        }
    }

    // Rows
    for (i = 0; (i < VISIBLE_ROWS) && (scroll + i < count); i++) {
        s32 index = scroll + i;
        s32 id = entries[index];
        s32 rowY = LIST_TOP + i * ROW_H;
        s32 selected = (index == cursor);
        EntryType type = Entry_Type(id);
        const char* value = NULL;
        u8 vr = 255, vg = 255, vb = 255;

        if (selected) {
            GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        } else {
            GfxPrint_SetColor(&printer, 190, 190, 190, 255);
        }
        Ui_Print(&printer, PANEL_X + 8, rowY, Entry_Label(id));

        switch (type) {
            case ENTRY_TOGGLE:
                if (gOpt[id]) {
                    value = "ON";
                    vr = 60, vg = 255, vb = 90;
                } else {
                    value = "OFF";
                    vr = 255, vg = 70, vb = 70;
                }
                break;
            case ENTRY_CHOICE:
                value = Entry_ChoiceName(id, gOpt[id]);
                vr = 255, vg = 220, vb = 90;
                break;
            case ENTRY_ACTION:
                if ((id >= ID_SUB_V2) && (id <= ID_V2_LAST)) {
                    value = V2_Value(id, statusText, sizeof(statusText), &vr, &vg, &vb);
                }
                break;
            case ENTRY_SUBMENU:
                value = ">>";
                vr = 150, vg = 200, vb = 255;
                if (id == ID_SUB_EV_LIST) {
                    // How many events are on.
                    s32 total;
                    s32 on = Events_EnabledCount(&total);
                    s32 k = 0;

                    if (on >= 10) {
                        statusText[k++] = '0' + (on / 10) % 10;
                    }
                    statusText[k++] = '0' + (on % 10);
                    statusText[k++] = '/';
                    if (total >= 10) {
                        statusText[k++] = '0' + (total / 10) % 10;
                    }
                    statusText[k++] = '0' + (total % 10);
                    statusText[k++] = ' ';
                    statusText[k++] = '>';
                    statusText[k++] = '>';
                    statusText[k] = '\0';
                    value = statusText;
                }
                break;
            case ENTRY_INFO:
                if ((id >= ID_SUB_V2) && (id <= ID_V2_LAST)) {
                    value = V2_Value(id, statusText, sizeof(statusText), &vr, &vg, &vb);
                } else {
                    value = Events_InfoText(id, statusText, sizeof(statusText), &vr, &vg, &vb);
                }
                break;
            default:
                break;
        }

        if (value != NULL) {
            s32 len = Ui_StrLen(value);

            GfxPrint_SetColor(&printer, vr, vg, vb, 255);
            Ui_Print(&printer, PANEL_X + PANEL_W - 12 - len * 8, rowY, value);
        }
    }

    // Footer: what the highlighted row means, over up to three lines.
    GfxPrint_SetColor(&printer, 170, 170, 170, 255);
    {
        const char* desc = (count > 0) ? Events_InfoDesc(entries[cursor]) : NULL;

        if (desc != NULL) {
            char lines[3][40];
            s32 line = 0;
            s32 len = 0;
            s32 k = 0;

            lines[0][0] = lines[1][0] = lines[2][0] = '\0';
            while ((desc[k] != '\0') && (line < 3)) {
                s32 wordLen = 0;

                while ((desc[k + wordLen] != '\0') && (desc[k + wordLen] != ' ')) {
                    wordLen++;
                }
                if ((len > 0) && (len + 1 + wordLen > 36)) {
                    line++;
                    len = 0;
                    if (line >= 3) {
                        break;
                    }
                }
                if (len > 0) {
                    lines[line][len++] = ' ';
                }
                while ((wordLen > 0) && (len < 36)) {
                    lines[line][len++] = desc[k++];
                    wordLen--;
                }
                k += wordLen;
                lines[line][len] = '\0';
                while (desc[k] == ' ') {
                    k++;
                }
            }
            GfxPrint_SetColor(&printer, 210, 170, 255, 255);
            for (k = 0; k < 3; k++) {
                Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 2 + k * 10, lines[k]);
            }
        } else {
            Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 2, "A: Select   B: Back");
            Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 12, "L / R / Z: switch tabs");
        }
        GfxPrint_SetColor(&printer, 120, 120, 150, 255);
        Ui_Print(&printer, PANEL_X + PANEL_W - 8 - (s32)(sizeof(GE_VERSION) - 1) * 8, FOOTER_Y + 22, GE_VERSION);
    }

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    // Back to normal for anything drawn after us.
    gEXSetRectAlign(gfx++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gEXSetScissorAlign(gfx++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    *gfxP = gfx;
}

static void Menu_DrawToast(PlayState* play, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    GfxPrint printer;
    s32 len = Ui_StrLen(sToastText);
    s32 w = len * 8 + 16;
    s32 x = (SCREEN_WIDTH - w) / 2;
    s32 y = 226;
    u8 alpha = (sToastTimer > 10) ? 255 : (u8)(sToastTimer * 25);
    u8 r, g, b;

    Menu_ThemeColor(play, 0, &r, &g, &b);
    gfx = Ui_DrawRect(gfx, x, y - 4, x + w, y + 12, 0, 0, 0, (u8)(alpha * 190 / 255));
    gfx = Ui_DrawRect(gfx, x, y + 11, x + w, y + 12, r, g, b, alpha);

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT); // crisp letters: no bleed from the next glyph when upscaled
    printer.flags |= GFXP_FLAG_SHADOW_BIT;
    GfxPrint_SetColor(&printer, 255, 255, 255, alpha);
    Ui_Print(&printer, x + 8, y, sToastText);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    *gfxP = gfx;
}

RECOMP_HOOK("Play_PostWorldDraw") void CarpenterMenu_BeforePostWorldDraw(PlayState* play) {
    sDrawPlay = play;
}

RECOMP_HOOK_RETURN("Play_PostWorldDraw") void CarpenterMenu_AfterPostWorldDraw(void) {
    PlayState* play = sDrawPlay;
    Gfx* gfx;
    s32 drawEvents;

    if (play == NULL) {
        return;
    }
    drawEvents = !gMenuOpen && Events_WantsDraw(play);
    if (!gMenuOpen && !Notes_Any() && !drawEvents) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    gfx = OVERLAY_DISP;

    if (drawEvents) {
        Events_Draw(play, &gfx);
    }
    if (gMenuOpen) {
        Menu_DrawPanel(play, &gfx);
    }
    if (gMenuOpen && (sToastTimer > 0) && (sToastText != NULL)) {
        Menu_DrawToast(play, &gfx);
    }
    if (!gMenuOpen && Notes_Any()) {
        Notes_Draw(play, &gfx);
    }

    OVERLAY_DISP = gfx;
    CLOSE_DISPS(play->state.gfxCtx);
}
