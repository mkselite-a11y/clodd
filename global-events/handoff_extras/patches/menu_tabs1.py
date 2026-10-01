def sub(s,a,b,c=1):
    assert a in s, a[:80]
    return s.replace(a,b,c)
P='/root/RainbowCarpenter/src/menu.c'
s=open(P).read()
s=sub(s,"    MENU_GIVE_MASK,\n    MENU_COUNT","    MENU_GIVE_MASK,\n    MENU_OVERVIEW,  // V3\n    MENU_EV_DETAIL, // V3\n    MENU_COUNT")
s=sub(s,"""static const u16 sMainEntries[] = { ID_SUB_EVENTS,   ID_SUB_V2,    ID_SUB_V2_NEM, ID_SUB_V2_LINK,
                                    ID_SUB_SETTINGS, ID_SUB_PLAYER, ID_SUB_ITEMS,  ID_SUB_WORLD,
                                    ID_SUB_TELEPORT, ID_CLOSE };""","""static const u16 sMainEntries[] = { ID_SUB_PLAYER, ID_SUB_ITEMS, ID_SUB_WORLD, ID_SUB_TELEPORT, ID_SUB_FUN, ID_CLOSE };""")
s=sub(s,"""static const u16 sEventsEntries[] = { ID_EV_MASTER,       ID_EV_STATUS,   ID_EV_CURSE_INFO,  ID_EV_TRIGGER,
                                      ID_EV_END,          ID_EV_INTERVAL, ID_EV_DURATION,    ID_EV_INTENSITY,
                                      ID_SUB_V2_MUTS,     ID_V2_MUT,      ID_SUB_EV_FEATURES, ID_SUB_EV_LIST,
                                      ID_SUB_EV_START,    ID_SUB_EV_SETTINGS, ID_SUB_EV_REPORT };
static const u16 sEvFeatureEntries[] = { ID_EV_ROULETTE,  ID_EV_COMBOS,    ID_EV_CURSE,    ID_EV_PRESSURE,
                                         ID_EV_HOURS,     ID_EV_TIMERSTYLE,
                                         ID_EV_WARNSOUND, ID_EV_STREAKS,   ID_EV_MERCY,
                                         ID_EV_WILD };""","""// V3: the curse sits with the events; one list of events, each with its own page.
static const u16 sEventsEntries[] = { ID_EV_CURSE_INFO, ID_EV_STATUS,     ID_EV_MASTER,       ID_SUB_EV_LIST,
                                      ID_EV_TRIGGER,    ID_EV_END,        ID_EV_INTERVAL,     ID_EV_DURATION,
                                      ID_SUB_V2_MUTS,   ID_V2_MUT,        ID_SUB_EV_POOL,     ID_SUB_EV_CRITTERS,
                                      ID_SUB_EV_FEATURES, ID_SUB_EV_REPORT };
static const u16 sEvFeatureEntries[] = { ID_EV_ROULETTE,  ID_EV_COMBOS,    ID_EV_CURSE,
                                         ID_EV_HOURS,     ID_EV_TIMERSTYLE,
                                         ID_EV_WARNSOUND, ID_EV_STREAKS,   ID_EV_MERCY };
static u16 sOverviewDyn[16];
static u16 sEvDetailDyn[4];""")
s=sub(s,"static const u16 sSettingsEntries[] = { ID_THEME, ID_SOUNDS, ID_ALL_OFF };","""static const u16 sSettingsEntries[] = { ID_THEME,    ID_SOUNDS,    ID_V2_NEWS,   ID_V2_BANNERS, ID_V2_AUTOSAVE,
                                         ID_V2_MUT,   ID_V2_BOUNTY, ID_V2_NEM,    ID_V2_POUCH,   ID_V2_TRACKER,
                                         ID_V2_REMOTE, ID_SUB_CARPENTER, ID_ALL_OFF };""")
s=sub(s,"""static const u16* Menu_Entries(s32 page, s32* count) {
    switch (page) {""","""s32 V2_TakenBounty(s32 k);

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
            return sEvDetailDyn;""")
s=sub(s,"""            sV2NemDyn[n++] = sV2NemEntries[0];
            sV2NemDyn[n++] = sV2NemEntries[1];
            sV2NemDyn[n++] = sV2NemEntries[2];
            n = Menu_TraitRows(sV2NemDyn, n, V2_NemTraitMask());
            sV2NemDyn[n++] = sV2NemEntries[5];
            sV2NemDyn[n++] = sV2NemEntries[6];
            sV2NemDyn[n++] = sV2NemEntries[7];
            sV2NemDyn[n++] = ID_V2_NEM;
            sV2NemDyn[n++] = ID_V2_NEMDIFF;""","""            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 0;
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 1;
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 2;
            if (V2_NemStatValue(0) >= 0) {
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 4; // nature
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 8; // stats with bars
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 9;
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 10;
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 11;
                sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 6; // free points
            }
            n = Menu_TraitRows(sV2NemDyn, n, V2_NemTraitMask());
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 5;
            sV2NemDyn[n++] = ID_V2_NEMROW_FIRST + 7;
            sV2NemDyn[n++] = ID_V2_NEMDIFF;""")
s=sub(s,"static u16 sV2NemDyn[8 + 16];","static u16 sV2NemDyn[16 + 16];")
open(P,'w').write(s)
