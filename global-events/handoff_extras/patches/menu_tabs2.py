def sub(s,a,b,c=1):
    assert a in s, a[:80]
    return s.replace(a,b,c)
P='/root/RainbowCarpenter/src/menu.c'
s=open(P).read()
s=sub(s,"""static const char* Menu_PageTitle(s32 page) {
    switch (page) {
        case MENU_EVENTS:
            return "Global Events";""","""static const char* Menu_PageTitle(s32 page) {
    switch (page) {
        case MENU_OVERVIEW:
            return "Overview";
        case MENU_EV_DETAIL:
            return Events_Name(gEvDetail);
        case MENU_EVENTS:
            return "Events";""")
s=sub(s,"""        case MENU_V2_LINK:
            return "Friend Link";
        case MENU_V2_BOUNTY:
            return "Bounty Board";""","""        case MENU_V2_LINK:
            return "Friends";
        case MENU_V2_BOUNTY:
            return "Bounty Board";""")
s=sub(s,"""        default:
            return "Main Menu";
    }
}""","""        default:
            return "Cheats";
    }
}""")
# tabs + parent
s=sub(s,"""static s32 Menu_Parent(s32 page) {
    switch (page) {""","""// V3: the top-level pages are tabs (L / R or Z to switch).
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
            return MENU_SETTINGS;""")
s=sub(s,"""        case MENU_EV_LIST:
        case MENU_EV_START:
        case MENU_EV_SETTINGS:
        case MENU_EV_FEATURES:
        case MENU_EV_REPORT:
        case MENU_EV_LOG:
            return MENU_EVENTS;
        case MENU_V2_BOUNTY:
        case MENU_V2_SHOP:
            return MENU_V2;""","""        case MENU_EV_LIST:
        case MENU_EV_START:
        case MENU_EV_SETTINGS:
        case MENU_EV_FEATURES:
        case MENU_EV_REPORT:
        case MENU_EV_LOG:
        case MENU_POOL:
        case MENU_CRITTERS:
            return MENU_EVENTS;""")
s=sub(s,"""        case MENU_POOL:
        case MENU_CRITTERS:
        case MENU_MOONFALL:""","""        case MENU_MOONFALL:""")
# open
s=sub(s,"""void Menu_OpenV2(PlayState* play, s32 which) {
    Menu_Open(play);
    sPage = (which == 1) ? MENU_V2_DRAFT : (which == 2) ? MENU_V2_BOUNTY : MENU_V2;""","""void Menu_OpenV2(PlayState* play, s32 which) {
    Menu_Open(play);
    if (which == 2) {
        sTab = 1; // Bounty Board
    } else if (which == 0) {
        sTab = 0;
    }
    sPage = (which == 1) ? MENU_V2_DRAFT : sTabs[sTab];""")
s=sub(s,"""    gMenuOpen = true;
    gMenuAtBoard = false;
    sPage = MENU_MAIN;""","""    gMenuOpen = true;
    gMenuAtBoard = false;
    sPage = sTabs[sTab % ARRAY_COUNT(sTabs)]; // the tab you were on last""")
# input: tabs, event detail, back
s=sub(s,"""    if (CHECK_BTN_ANY(press, BTN_A)) {
        Menu_Select(play);
    } else if (CHECK_BTN_ANY(press, BTN_B)) {
        if (sPage == MENU_V2_DRAFT) {
            // A mutator must be chosen.
            Menu_ShowToast("Pick a mutator with A.");
        } else if (sPage == MENU_MAIN) {
            Menu_Close(play);
        } else {
            sPage = Menu_Parent(sPage);
            Menu_PlaySfx(NA_SE_SY_CANCEL);
        }
    }""","""    // Tabs: L / R (or Z) flip between the top-level pages.
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
            Menu_ShowToast("Pick a mutator with A.");
        } else if (Menu_IsTabRoot(sPage)) {
            Menu_Close(play);
        } else {
            sPage = Menu_Parent(sPage);
            Menu_PlaySfx(NA_SE_SY_CANCEL);
        }
    }""")
s=sub(s,"""        } else if (sHoldFrames == 1) {
            // Left/right changes a value once per press (no auto-repeat).
            entries = Menu_Entries(sPage, &count);
            Menu_ChangeValue(play, entries[sCursor[sPage]], dirX);
        }""","""        } else if (sHoldFrames == 1) {
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
        }""")
open(P,'w').write(s)
