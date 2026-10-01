// ===========================================================================
// Global Events 2.0: Mutator Deck, Bounty Board + Moon Marks, Nemesis,
// Moon Pouch and the save code. Included into events.c after ev_v11.inc.
// Nothing here gives game items, sets progress flags or touches checks.
// ===========================================================================

static Gfx* Ev_BeginLeftAligned(Gfx* gfx);
static Gfx* Ev_EndAligned(Gfx* gfx);
static s32 V2_ZoneNow(PlayState* play);

#define V2_ON(id) (gOpt[id] != 0)
#define V2_Banner(title, sub) Menu_ShowBanner(title, sub)
static void World_CowardFlees(PlayState* play, Actor* actor, TagData* t);

// ---------------------------------------------------------------------------
// Zones of Termina (bounties and the Nemesis travel between them)
// ---------------------------------------------------------------------------

// V3: the 27 outdoor areas (Moonfall Mayhem's map) plus every indoor area,
// each indoor one hanging off the outdoor area its door is in. Creatures
// prefer the big outdoor areas and only dip indoors now and then.
typedef struct {
    const char* name;
    s16 scenes[3];
    s8 parent; // -1: outdoor
    s8 links[10];
} ZoneDef;

#define ZL(...) { __VA_ARGS__, -1 }
static const ZoneDef sZones[] = {
    /*  0 */ { "South Clock Town", { SCENE_CLOCKTOWER, -1, -1 }, -1, ZL(1, 2, 3, 4, 5) },
    /*  1 */ { "East Clock Town", { SCENE_TOWN, -1, -1 }, -1, ZL(0, 3, 5) },
    /*  2 */ { "West Clock Town", { SCENE_ICHIBA, -1, -1 }, -1, ZL(0, 5) },
    /*  3 */ { "North Clock Town", { SCENE_BACKTOWN, -1, -1 }, -1, ZL(0, 1, 5) },
    /*  4 */ { "Laundry Pool", { SCENE_ALLEY, -1, -1 }, -1, ZL(0) },
    /*  5 */ { "Termina Field", { SCENE_00KEIKOKU, -1, -1 }, -1, ZL(0, 1, 2, 3, 6, 11, 17, 19, 23) },
    /*  6 */ { "Road to the Swamp", { SCENE_24KEMONOMITI, -1, -1 }, -1, ZL(5, 7) },
    /*  7 */ { "Southern Swamp", { SCENE_20SICHITAI, SCENE_20SICHITAI2, -1 }, -1, ZL(6, 8, 9, 10) },
    /*  8 */ { "Deku Palace", { SCENE_22DEKUCITY, -1, -1 }, -1, ZL(7) },
    /*  9 */ { "Woodfall", { SCENE_21MITURINMAE, -1, -1 }, -1, ZL(7) },
    /* 10 */ { "Woods of Mystery", { SCENE_26SARUNOMORI, -1, -1 }, -1, ZL(7) },
    /* 11 */ { "Path to the Mountain", { SCENE_13HUBUKINOMITI, -1, -1 }, -1, ZL(5, 12) },
    /* 12 */ { "Mountain Village", { SCENE_10YUKIYAMANOMURA, SCENE_10YUKIYAMANOMURA2, -1 }, -1, ZL(11, 13, 15) },
    /* 13 */ { "Twin Islands", { SCENE_17SETUGEN, SCENE_17SETUGEN2, -1 }, -1, ZL(12, 14) },
    /* 14 */ { "Goron Village", { SCENE_11GORONNOSATO, SCENE_11GORONNOSATO2, -1 }, -1, ZL(13) },
    /* 15 */ { "Path to Snowhead", { SCENE_14YUKIDAMANOMITI, -1, -1 }, -1, ZL(12, 16) },
    /* 16 */ { "Snowhead", { SCENE_12HAKUGINMAE, -1, -1 }, -1, ZL(15) },
    /* 17 */ { "Milk Road", { SCENE_ROMANYMAE, -1, -1 }, -1, ZL(5, 18) },
    /* 18 */ { "Romani Ranch", { SCENE_F01, -1, -1 }, -1, ZL(17) },
    /* 19 */ { "Great Bay Coast", { SCENE_30GYOSON, -1, -1 }, -1, ZL(5, 20, 21) },
    /* 20 */ { "Zora Cape", { SCENE_31MISAKI, -1, -1 }, -1, ZL(19, 22) },
    /* 21 */ { "Pirates' Fortress", { SCENE_TORIDE, -1, -1 }, -1, ZL(19) },
    /* 22 */ { "Zora Hall", { SCENE_33ZORACITY, -1, -1 }, -1, ZL(20) },
    /* 23 */ { "Road to Ikana", { SCENE_IKANAMAE, -1, -1 }, -1, ZL(5, 24, 25) },
    /* 24 */ { "Ikana Graveyard", { SCENE_BOTI, -1, -1 }, -1, ZL(23) },
    /* 25 */ { "Ikana Canyon", { SCENE_IKANA, -1, -1 }, -1, ZL(23, 26) },
    /* 26 */ { "Stone Tower", { SCENE_F40, SCENE_F41, -1 }, -1, ZL(25) },
    // Indoors (linked only to their outdoor area)
    /* 27 */ { "Clock Tower", { SCENE_INSIDETOWER, -1, -1 }, 0, ZL(-1) },
    /* 28 */ { "Stock Pot Inn", { SCENE_YADOYA, -1, -1 }, 1, ZL(-1) },
    /* 29 */ { "Milk Bar", { SCENE_MILK_BAR, -1, -1 }, 1, ZL(-1) },
    /* 30 */ { "Mayor's Residence", { SCENE_SONCHONOIE, -1, -1 }, 1, ZL(-1) },
    /* 31 */ { "Honey & Darling's", { SCENE_BOWLING, -1, -1 }, 1, ZL(-1) },
    /* 32 */ { "Treasure Chest Shop", { SCENE_TAKARAYA, -1, -1 }, 1, ZL(-1) },
    /* 33 */ { "Town Shooting Gallery", { SCENE_SYATEKI_MIZU, -1, -1 }, 1, ZL(-1) },
    /* 34 */ { "Curiosity Shop", { SCENE_AYASHIISHOP, -1, -1 }, 2, ZL(-1) },
    /* 35 */ { "Trading Post", { SCENE_8ITEMSHOP, -1, -1 }, 2, ZL(-1) },
    /* 36 */ { "Bomb Shop", { SCENE_BOMYA, -1, -1 }, 2, ZL(-1) },
    /* 37 */ { "Swordsman's School", { SCENE_DOUJOU, -1, -1 }, 2, ZL(-1) },
    /* 38 */ { "Post Office", { SCENE_POSTHOUSE, -1, -1 }, 2, ZL(-1) },
    /* 39 */ { "Lottery Shop", { SCENE_TAKARAKUJI, -1, -1 }, 2, ZL(-1) },
    /* 40 */ { "Deku Playground", { SCENE_DEKUTES, -1, -1 }, 3, ZL(-1) },
    /* 41 */ { "Astral Observatory", { SCENE_TENMON_DAI, -1, -1 }, 5, ZL(-1) },
    /* 42 */ { "A Grotto", { SCENE_KAKUSIANA, -1, -1 }, 5, ZL(-1) },
    /* 43 */ { "Fairy Fountain", { SCENE_YOUSEI_IZUMI, -1, -1 }, 5, ZL(-1) },
    /* 44 */ { "Swamp Shooting Gallery", { SCENE_SYATEKI_MORI, -1, -1 }, 6, ZL(-1) },
    /* 45 */ { "Potion Shop", { SCENE_WITCH_SHOP, -1, -1 }, 7, ZL(-1) },
    /* 46 */ { "Tourist Center", { SCENE_MAP_SHOP, -1, -1 }, 7, ZL(-1) },
    /* 47 */ { "Swamp Spider House", { SCENE_KINSTA1, -1, -1 }, 7, ZL(-1) },
    /* 48 */ { "Deku King's Chamber", { SCENE_DEKU_KING, -1, -1 }, 8, ZL(-1) },
    /* 49 */ { "Woodfall Temple", { SCENE_MITURIN, -1, -1 }, 9, ZL(-1) },
    /* 50 */ { "Blacksmith", { SCENE_KAJIYA, -1, -1 }, 12, ZL(-1) },
    /* 51 */ { "Goron Graveyard", { SCENE_GORON_HAKA, -1, -1 }, 12, ZL(-1) },
    /* 52 */ { "Goron Racetrack", { SCENE_GORONRACE, -1, -1 }, 13, ZL(-1) },
    /* 53 */ { "Goron Shrine", { SCENE_16GORON_HOUSE, -1, -1 }, 14, ZL(-1) },
    /* 54 */ { "Goron Shop", { SCENE_GORONSHOP, -1, -1 }, 14, ZL(-1) },
    /* 55 */ { "Snowhead Temple", { SCENE_HAKUGIN, -1, -1 }, 16, ZL(-1) },
    /* 56 */ { "Gorman Track", { SCENE_KOEPONARACE, -1, -1 }, 17, ZL(-1) },
    /* 57 */ { "Ranch House", { SCENE_OMOYA, -1, -1 }, 18, ZL(-1) },
    /* 58 */ { "Doggy Racetrack", { SCENE_F01_B, -1, -1 }, 18, ZL(-1) },
    /* 59 */ { "Cucco Shack", { SCENE_F01C, -1, -1 }, 18, ZL(-1) },
    /* 60 */ { "Marine Lab", { SCENE_LABO, -1, -1 }, 19, ZL(-1) },
    /* 61 */ { "Fisherman's Hut", { SCENE_FISHERMAN, -1, -1 }, 19, ZL(-1) },
    /* 62 */ { "Oceanside Spider House", { SCENE_KINDAN2, -1, -1 }, 19, ZL(-1) },
    /* 63 */ { "Pinnacle Rock", { SCENE_SINKAI, -1, -1 }, 19, ZL(-1) },
    /* 64 */ { "Waterfall Rapids", { SCENE_35TAKI, -1, -1 }, 20, ZL(-1) },
    /* 65 */ { "Great Bay Temple", { SCENE_SEA, -1, -1 }, 20, ZL(-1) },
    /* 66 */ { "Pirates' Hideout", { SCENE_PIRATE, SCENE_KAIZOKU, -1 }, 21, ZL(-1) },
    /* 67 */ { "Zora Band Rooms", { SCENE_BANDROOM, -1, -1 }, 22, ZL(-1) },
    /* 68 */ { "Dampe's House", { SCENE_DANPEI, -1, -1 }, 24, ZL(-1) },
    /* 69 */ { "Beneath the Graveyard", { SCENE_HAKASHITA, -1, -1 }, 24, ZL(-1) },
    /* 70 */ { "Ikana Castle", { SCENE_CASTLE, -1, -1 }, 25, ZL(-1) },
    /* 71 */ { "Beneath the Well", { SCENE_REDEAD, -1, -1 }, 25, ZL(-1) },
    /* 72 */ { "Music Box House", { SCENE_MUSICHOUSE, -1, -1 }, 25, ZL(-1) },
    /* 73 */ { "Sakon's Hideout", { SCENE_SECOM, -1, -1 }, 25, ZL(-1) },
    /* 74 */ { "Secret Shrine", { SCENE_RANDOM, -1, -1 }, 25, ZL(-1) },
    /* 75 */ { "Poe Hut", { SCENE_TOUGITES, -1, -1 }, 25, ZL(-1) },
    /* 76 */ { "Stone Tower Temple", { SCENE_INISIE_N, SCENE_INISIE_R, -1 }, 26, ZL(-1) },
};
#undef ZL
#define ZONE_COUNT ((s32)ARRAY_COUNT(sZones))
#define ZONE_OUTDOOR 27
_Static_assert(ARRAY_COUNT(sZones) < 127, "zones fit an s8 and 7 save bits");

static const char* sZoneNames[ARRAY_COUNT(sZones)];
static u8 sZoneBlocked[ARRAY_COUNT(sZones)]; // the game blocks your sword there: creatures stay out

static void Zone_InitNames(void) {
    s32 i;

    for (i = 0; i < ZONE_COUNT; i++) {
        sZoneNames[i] = sZones[i].name;
    }
}

static s32 V2_ZoneOfScene(s32 scene) {
    s32 i;
    s32 k;

    for (i = 0; i < ZONE_COUNT; i++) {
        for (k = 0; k < 3; k++) {
            if (sZones[i].scenes[k] == scene) {
                return i;
            }
        }
    }
    return -1;
}

static s32 V2_ZoneNow(PlayState* play) {
    return V2_ZoneOfScene(play->sceneId);
}

static s32 Zone_IsIndoor(s32 z) {
    return (z >= 0) && (z < ZONE_COUNT) && (sZones[z].parent >= 0);
}

// The outdoor area a zone belongs to (itself if outdoors).
static s32 Zone_Outdoor(s32 z) {
    return Zone_IsIndoor(z) ? sZones[z].parent : z;
}

// Neighbours: outdoor links plus the indoor areas behind this area's doors.
static s32 Zone_Neighbors(s32 z, s32* out) {
    s32 n = 0;
    s32 k;

    if ((z < 0) || (z >= ZONE_COUNT)) {
        return 0;
    }
    if (Zone_IsIndoor(z)) {
        out[n++] = sZones[z].parent;
        return n;
    }
    for (k = 0; (k < 9) && (sZones[z].links[k] >= 0); k++) {
        out[n++] = sZones[z].links[k];
    }
    for (k = ZONE_OUTDOOR; k < ZONE_COUNT; k++) {
        if (sZones[k].parent == z) {
            out[n++] = k;
        }
    }
    return n;
}

static s32 V2_Adjacent(s32 a, s32 b) {
    s32 nb[ARRAY_COUNT(sZones)];
    s32 n = Zone_Neighbors(a, nb);
    s32 k;

    for (k = 0; k < n; k++) {
        if (nb[k] == b) {
            return true;
        }
    }
    return false;
}

// A random neighbour. Outdoors it mostly picks another outdoor area: an
// indoor one only about 1 time in 6. Indoors it always heads back out.
static s32 V2_Neighbor(s32 zone) {
    s32 nb[ARRAY_COUNT(sZones)];
    s32 n = Zone_Neighbors(zone, nb);
    s32 outdoor[12];
    s32 indoor[ARRAY_COUNT(sZones)];
    s32 no = 0;
    s32 ni = 0;
    s32 k;

    for (k = 0; k < n; k++) {
        if (sZoneBlocked[nb[k]]) {
            continue;
        }
        if (Zone_IsIndoor(nb[k])) {
            indoor[ni++] = nb[k];
        } else if (no < 12) {
            outdoor[no++] = nb[k];
        }
    }
    if ((ni > 0) && ((no == 0) || ((s32)(Rand_ZeroOne() * 6) == 0))) {
        return indoor[(s32)(Rand_ZeroOne() * ni) % ni];
    }
    if (no > 0) {
        return outdoor[(s32)(Rand_ZeroOne() * no) % no];
    }
    return zone;
}

static s32 V2_LinkCount(s32 zone) {
    s32 nb[ARRAY_COUNT(sZones)];

    return Zone_Neighbors(zone, nb);
}

static s32 V2_Rand(s32 n) {
    return (n > 0) ? ((s32)(Rand_ZeroOne() * n) % n) : 0;
}

// ---------------------------------------------------------------------------
// Traits (bounty modifiers and Nemesis traits)
// ---------------------------------------------------------------------------

// 16 at most: traits are a u16 mask and the save code stores each in 4 bits.
// V3: Swift, Giant, Brutal and Leech gave way to Coward, Moonblessed,
// Mark-Hungry and Frostbrand (same slots, so saved traits map across).
// Slot 13 (Second Wind) is retired.
typedef enum {
    TR_ARMORED, TR_COWARD, TR_REGEN, TR_SUMMONER, TR_FIRE, TR_BLINKER,
    TR_MOONBLESSED, TR_VENGEFUL, TR_MARKHUNGRY, TR_SPLITTER, TR_ENRAGED, TR_THORNED,
    TR_FROSTBRAND, TR_RETIRED, TR_THIEF, TR_CLOAKED, TR_COUNT
} TraitId;

static const char* sTraitNames[TR_COUNT] = {
    "Armored", "Coward", "Regenerating", "Summoner", "Fire Aura", "Blinker",
    "Moonblessed", "Vengeful", "Mark-Hungry", "Splitter", "Enraged", "Thorned",
    "Frostbrand", "(retired)", "Thief", "Cloaked",
};

static const char* sTraitDescs[TR_COUNT] = {
    "Takes half damage from every hit.",
    "At 30% health it flees, then returns healed.",
    "Slowly heals itself over time.",
    "Calls in extra enemies every 12 seconds.",
    "Burns you if you get too close.",
    "Teleports near you if you keep your distance.",
    "Can't be hurt while an event is running.",
    "Drops a bomb from the sky when it dies.",
    "Hits harder the more Moon Marks you carry.",
    "Splits into two copies when slain.",
    "Faster and hits harder below half health.",
    "Stings you back when you hit it with a sword.",
    "Its hits chill you: your stick goes half as far.",
    "",
    "Steals 3 Moon Marks per hit. Pays back double.",
    "Keeps fading out of sight. Lens reveals it.",
};

// A random trait that's in use.
static s32 Trait_Random(void) {
    s32 tr;

    do {
        tr = (s32)(Rand_ZeroOne() * TR_COUNT) % TR_COUNT;
    } while (tr == TR_RETIRED);
    return tr;
}
_Static_assert(TR_COUNT <= 16, "traits must fit in a u16");

// Enemies that can be bounties or Nemeses (indices into sPool).
static const u8 sHuntPool[] = { 3, 4, 5, 7, 9, 13, 19, 22, 24, 28, 30, 31, 32, 8, 10 }; // V3: no ReDeads (you can only knock them down), no Big Poes

#define TAGF_V2_GUARD (1 << 0) // Nemesis bodyguard
#define TAGF_V2_ELITE (1 << 1) // WANTED bounty: Meteor Caller
#define TAGF_V2_DYING (1 << 2) // a Big Poe of ours on its way out
#define TAGF_V2_ELITEFOE (1 << 3) // Elite Foes mutator: got a random trait
#define TAGF_V2_SEEN (1 << 4)
#define TAGF_V2_TOUGH (1 << 5)
#define TAGF_V2_SPLIT (1 << 6) // a split copy: doesn't split again
#define TAGF_V2_REVIVED (1 << 7) // Second Wind used

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------

static const char* sMutNames[MUT_COUNT] = {
    "Thin Skin",     "Tough Crowd",   "Vengeful Dead", "Reinforcements", "Elite Foes",     "Heart Lock",
    "Open Wounds",   "(retired)",     "Mana Tax",     "Ammo Tax",       "Butterfingers",  "Rupee Rot",
    "Cold Water",    "Restless Earth", "Dark Nights",  "Emboldened",     "Hunted",         "Moon Tears",
    "Ambushers",     "Keese Escort",  "Weak Recovery", "Bleeding",
};

static const char* sMutDescs[MUT_COUNT] = {
    "You take 50% more damage.",
    "Enemies have 50% more health.",
    "Slain enemies leave a falling bomb behind.",
    "Kills may call in a replacement enemy.",
    "1 in 4 enemies has a random trait.",
    "You can't heal within 2 hearts of your max.",
    "After a hit, you can't heal for 10 seconds.",
    "",
    "Magic costs double.",
    "Arrows, bombs, nuts and sticks go twice as fast.",
    "Every hit you take costs 5 rupees.",
    "You lose a rupee every 20 seconds.",
    "Swimming drains health (not as Zora).",
    "Stand still 4 seconds and a boulder falls on you.",
    "Nights are much darker.",
    "Your Nemesis gains a level.",
    "Your Nemesis turns up far more often.",
    "Boulders fall on you now and then.",
    "New areas may greet you with enemies.",
    "A few Keese follow you everywhere.",
    "All healing is halved.",
    "Every hit takes two more hearts a moment later.",
};

static u8 sMutOrder[6];
static s32 sMutCount = 0;
static u8 sDraftDone = 0; // bit per draft slot this cycle
static s32 sOfferA = -1;
static s32 sOfferB = -1;
static s32 sDraftSlot = -1;

// V3: retired mutators (they overlapped other things) can't be drafted and do nothing.
static s32 Mut_Retired(s32 m) {
    return (m == MUT_RETIRED) || (m == MUT_VENGEFUL) || (m == MUT_RUPEEROT) || (m == MUT_TEARS) ||
           (m == MUT_WEAKHEAL) || (m == MUT_BLEEDING);
}

static s32 Mut_Has(s32 m) {
    return !Mut_Retired(m) && ((sMutMask & (1u << m)) != 0);
}

static void Mut_Add(s32 m) {
    if ((m < 0) || (m >= MUT_COUNT) || Mut_Retired(m) || Mut_Has(m) || (sMutCount >= 6)) {
        return;
    }
    sMutMask |= (1u << m);
    sMutOrder[sMutCount++] = m;
}

static s32 Mut_RandomNew(s32 except) {
    s32 opts[MUT_COUNT];
    s32 n = 0;
    s32 i;

    for (i = 0; i < MUT_COUNT; i++) {
        if (!Mut_Has(i) && (i != except) && !Mut_Retired(i)) {
            opts[n++] = i;
        }
    }
    return (n > 0) ? opts[V2_Rand(n)] : -1;
}

static void Mut_ClearAll(void) {
    sMutMask = 0;
    sMutCount = 0;
    sDraftDone = 0;
    sDraftSlot = -1;
}

// Draft slot for the current time: Day 1, Night 1, Day 2, Night 2, Day 3, Night 3.
static s32 Mut_SlotNow(void) {
    s32 day = gSaveContext.save.day;
    s32 slot;

    if ((day < 1) || (day > 3)) {
        return -1;
    }
    slot = (day - 1) * 2 + (Ev_IsNight() ? 1 : 0);
    return CLAMP(slot, 0, 5);
}

// ---------------------------------------------------------------------------
// Moon Marks, Moon Pouch, contracts, bounties and the Nemesis: state
// ---------------------------------------------------------------------------

static s32 sMarks = 0;
static s32 sBountiesDone = 0;
static s32 sNemSlain = 0;

typedef enum {
    PI_PORTAL, PI_GIGA, PI_GRAVITY, PI_HOURGLASS, PI_TONIC, PI_SECONDWIND, PI_SHIELD, PI_BRAND, PI_COMPASS,
    PI_SMOKE, PI_STILL, PI_DASH, PI_COUNT
} PouchItem;

static const char* sPouchNames[PI_COUNT] = {
    "Portal Stones", "Giga Blade",  "Gravity Bomb", "Moon Hourglass",  "Lunar Tonic",     "Second Wind",
    "Moon Shield",   "Nemesis Brand", "Nemesis Compass", "Smoke Veil", "Stillwater Charm", "Moon Dash",
};
static const char* sPouchDescs[PI_COUNT] = {
    "Place two, then step in one to hop to the other.",
    "Arm it: your next sword hits do triple damage.",
    "Pulls enemies together, then blows them up.",
    "Freezes every enemy for 8 seconds.",
    "Heals 4 hearts.",
    "Automatic: a killing blow leaves you at 4 hearts.",
    "Raise it: it absorbs your next hits.",
    "The next roaming enemy you hurt becomes your Nemesis.",
    "60 seconds: shows where your Nemesis is.",
    "Enemies lose track of you for 10 seconds.",
    "Ends the event that's running. No reward.",
    "Blink forward along the ground, through enemies.",
};
// V3 prices: about three times what they were. Work for the good stuff.
static const u8 sPouchPrice[PI_COUNT] = { 80, 70, 60, 100, 40, 250, 80, 120, 30, 55, 90, 45 };
static const u8 sPouchAmount[PI_COUNT] = { 1, 10, 1, 1, 1, 1, 3, 1, 1, 1, 1, 3 };
static const u8 sPouchMax[PI_COUNT] = { 9, 99, 9, 9, 9, 1, 30, 1, 9, 9, 9, 30 };
static s32 sBrandArmed = false;
static s32 sLComboUsed = false; // L was used for a pouch combo during this hold
static Actor* sMenuSpot = NULL;

static u8 sPouch[PI_COUNT];
static s32 sPouchSel = 0;
static s32 sGigaArmed = false;
static s32 sShieldUp = false;
static s32 sPactTimer = 0;
static s32 sCompassTimer = 0;
static s32 sHourglassTimer = 0;
static s32 sPouchFlash = 0;
static s32 sVeilTimer = 0;   // Smoke Veil
static s32 sDecoyTimer = 0;  // Decoy
static Actor* sDecoy = NULL;
#define DRAFT_REROLL_PRICE 30

// Portals
static Vec3f sPortal[2];
static s8 sPortalRoom[2];
static s32 sPortalCount = 0;
static s32 sPortalLock = false; // standing on a portal after a hop
static s32 sPortalRoomWait = 0; // a hop into another room is loading
// Recall Feather history
#define RECALL_LEN 40
static Vec3f sRecall[RECALL_LEN];
static s32 sRecallCount = 0;
static s32 sRecallHead = 0;
static s8 sRecallRoom = -1;
// Gravity bomb
static Vec3f sGravPos;
static s32 sGravTimer = 0;

// Contracts
typedef enum {
    CT_KILLS, CT_EVENT_KILLS, CT_SURVIVE, CT_COMBO, CT_QUIZ, CT_BLOODMOON, CT_NOHIT, CT_BOUNTY, CT_NEMHITS, CT_BRUTAL,
    CT_PREY, CT_GUARD, CT_BONUS,
    CT_TYPES
} ContractType;

static const char* sContractNames[CT_TYPES] = {
    "Kill enemies", "Kill during events", "Survive events", "Survive a combo", "Perfect Pop Quiz",
    "Blood Moon kills", "No-hit event", "Claim a bounty", "Hit your Nemesis", "Survive a Brutal",
    "Slay moonlit prey", "Defeat guards", "Earn a bounty bonus",
};
static const char* sContractDescs[CT_TYPES] = {
    "Defeat enemies anywhere.",
    "Defeat enemies while an event runs.",
    "Get through events alive.",
    "Get through a combo event alive.",
    "Answer every Pop Quiz question right.",
    "Defeat enemies during a Blood Moon.",
    "Survive an event without a hit.",
    "Defeat any posted bounty.",
    "Land hits on your Nemesis.",
    "Survive a Brutal-tier event.",
    "Win a Moonlit Prey event.",
    "Defeat your Nemesis's bodyguards.",
    "Meet a bounty's bonus objective.",
};
static const u8 sContractGoal[CT_TYPES] = { 15, 8, 3, 1, 1, 10, 1, 1, 5, 1, 1, 2, 1 };
static const u8 sContractPay[CT_TYPES] = { 20, 25, 20, 30, 35, 25, 35, 20, 25, 30, 25, 30, 25 };

typedef struct {
    s8 type;
    u8 progress;
} Contract;
static Contract sContracts[3];
static s32 sHitDuringEvent = false;

// V3: where a creature physically is inside the area you're in.
typedef struct {
    Vec3f home;   // its spot in this area (far from you, away from exits)
    Vec3f goal;   // where it's walking right now
    Vec3f exitPos; // the way it's leaving by
    u8 hasHome;
    u8 leaving;   // walking out toward an exit
    u8 arriving;  // comes in through an entrance
    u8 phase;     // Nemesis: 0 shadowing, 1 hunting you down
    s16 timer;
    s16 wander;
} WorldBody;

enum { MOTIVE_ROAM, MOTIVE_MIGRATE, MOTIVE_NEST, MOTIVE_HUNT, MOTIVE_SKITTISH, MOTIVE_COUNT };
static const char* sMotiveNames[MOTIVE_COUNT] = { "Roaming", "Migrating", "Nesting", "Hunting you", "Skittish" };

// Bounties
typedef struct {
    s8 pool;
    u8 stars;
    u16 traits;
    s8 zone;
    s32 migrate;
    Actor* actor;
    u8 spawnedVisit;
    u8 tries;
    u8 accepted; // taken at the Bounty Board; only accepted bounties show up
    u8 kind;
    u8 bonus;     // optional objective for +50%
    u8 hitSince;  // you took damage since it appeared
    s32 aliveTimer; // frames since it appeared
    // V3
    u8 motive;
    s8 target;    // Migrating: where it's headed
    u8 nameIdx;
    u8 hpPct;     // wounds carried between meetings
    WorldBody body;
} Bounty;
static const char* sBountyNames[24] = { "Rotfang", "Mudgut", "Bristle", "Old Scratch", "Nettle", "Cinder",
                                        "Hollow", "Grimbly", "Snarl", "Mossback", "Widow", "Pale Tom",
                                        "Ruckus", "Gnash", "Ember", "Crooked Kit", "Sallow", "Brine",
                                        "Thistle", "Grub", "Lantern", "Soot", "Marrow", "Tallow" };
enum { BONUS_NONE, BONUS_QUICK, BONUS_UNTOUCHED, BONUS_NIGHT, BONUS_DAY, BONUS_COUNT };
static const char* sBonusNames[BONUS_COUNT] = { "", "Quick Kill", "Untouched", "Night Hunt", "Day Hunt" };
static const char* sBonusDescs[BONUS_COUNT] = {
    "",
    "Slay it within 45s of it showing up: +50%.",
    "Take no damage once it shows up: +50%.",
    "Slay it at night: +50%.",
    "Slay it during the day: +50%.",
};
#define QUICK_FRAMES (45 * FPS)

// Hunter Rank: lifetime bounties claimed (kept in the save code).
static const char* sRankNames[6] = { "Novice", "Tracker", "Hunter", "Slayer", "Legend", "Moonbane" };
static const u8 sRankAt[6] = { 0, 3, 8, 15, 25, 40 };
static s32 sLastSlotSeen = -1; // for dawn: board refresh
// Moon Services state (not in the save code; lasts until used or the cycle ends)
static s32 sWardCharges = 0;   // skip mutator drafts
static s32 sMercyCharges = 0;  // cancel events as they start
static s32 sLuckyCharges = 0;  // double the next bounty payout
static s32 sFriendShields = 0; // Friend Shield: blocks your friend's next harmful action
static s32 sHunterSight = false;
static s32 sNemBanished = false;
static s32 sNemLure = false;   // Nemesis moves to the next area you enter
static s32 sBountyBait = false; // taken bounties move to the next area you enter
enum { BK_NORMAL, BK_WANTED, BK_RARE, BK_FRIEND };
#define SLOT_WANTED 3
#define SLOT_RARE 4
#define SLOT_FRIEND 5
static Bounty sBounties[V2_BOUNTY_SLOTS];
static s32 sHuntStreak = 0;   // bounties claimed this cycle
static s32 sStreakPaid = false;
static s32 sWantedPosted = false;
static Actor* sGuards[2];
static Actor* sNemOrphan = NULL; // a Nemesis replaced by a save code, still out in the world
static const u8 sRarePool[] = { 19, 30, 31, 32 }; // Like Like, Garo Master, Iron Knuckle, Dinolfos
static s32 sV2Ready = false;

// Nemesis
static const char* sNemA[24] = { "Vor", "Grask", "Mal", "Zeth", "Kru", "Dra", "Ur",  "Skar", "Mor", "Thal", "Gol", "Rhe",
                                 "Bal", "Nex",   "Ost", "Karn", "Vel", "Dur", "Ash", "Ryn",  "Hal", "Zor",  "Brak", "Fen" };
// Whole names (nameA 24 and up): no ending is added.
#define NEM_FULL_FIRST 24
static const char* sNemFull[] = { "Froggy", "Jack", "Dom", "Alex", "Aaron" };
static const char* sNemB[16] = { "gash", "ok", "ith", "an", "ul", "ax", "eth", "or",
                                 "im",   "ak", "us",  "en", "oth", "ar", "ix", "ek" };

typedef struct {
    s8 pool; // -1: no Nemesis
    u8 nameA;
    u8 nameB;
    u8 level;
    u8 traitCount;
    u8 traitOrder[6];
    s8 zone;
    u8 wins;
    s32 migrate;
    Actor* actor;
    u8 spawnedVisit;
    u8 engaged; // it hurt you this visit
    u8 tries;
    // V3
    u8 style;    // 0: charges straight at you, 1: shadows you first (fixed for its life)
    u8 scarred;  // survived being slain once: tougher
    u8 hpPct;    // wounds carried between meetings
    u16 dormant; // seconds before it hunts again (scarred return)
    WorldBody body;
} Nemesis;
static s32 sNemStolen = 0; // Thief: Marks it took from you (paid back double when slain)
static Nemesis sNem = { .pool = -1 };

// Nemesis skill points: 1 per level. Your friend spends them; unspent ones
// spend themselves after 2 minutes (or right away with Friend Link off).
enum { NS_SPEED, NS_POWER, NS_HEALTH, NS_TRACK, NS_COUNT };
#define NEM_STAT_MAX 50 // V3: very high; each point past 10 counts a quarter
static const char* sNemStatNames[NS_COUNT] = { "Speed", "Power", "Health", "Tracking" };
static u8 sNemStat[NS_COUNT];
static s32 sNemPointTimer = 0;
static u8 sNemInherit = 0;   // levels a purged Nemesis passes to the next one
static s8 sPlayerZone = -1;  // last outdoor area you were in (the Nemesis heads there)

static s32 Nem_PointsFree(void) {
    s32 used = 0;
    s32 k;

    if (sNem.pool < 0) {
        return 0;
    }
    for (k = 0; k < NS_COUNT; k++) {
        used += sNemStat[k];
    }
    return MAX(MIN(sNem.level, NS_COUNT * NEM_STAT_MAX) - used, 0);
}

static s32 Nem_SpendPoint(s32 stat) {
    if ((stat < 0) || (stat >= NS_COUNT) || (Nem_PointsFree() <= 0) || (sNemStat[stat] >= NEM_STAT_MAX)) {
        return false;
    }
    sNemStat[stat]++;
    return true;
}

// Picks the lowest stat (ties at random), so an unattended Nemesis grows evenly.
static s32 Nem_AutoStat(void) {
    s32 best = -1;
    s32 k;
    s32 start = (s32)(Rand_ZeroOne() * NS_COUNT) % NS_COUNT;

    for (k = 0; k < NS_COUNT; k++) {
        s32 s = (start + k) % NS_COUNT;

        if ((sNemStat[s] < NEM_STAT_MAX) && ((best < 0) || (sNemStat[s] < sNemStat[best]))) {
            best = s;
        }
    }
    return best;
}

static void Nem_ResetStats(void) {
    bzero(sNemStat, sizeof(sNemStat));
    sNemPointTimer = 0;
}

// How long it stays put before moving one area closer to you.
// A stat's effect: full value up to 10 (critical mass), a quarter per point after.
static f32 Nem_StatEff(s32 st) {
    s32 v = sNemStat[st];

    return (v <= 10) ? (f32)v : 10.0f + (v - 10) * 0.25f;
}

static s32 Nem_Crit(s32 st) {
    return (sNem.pool >= 0) && (sNemStat[st] >= 10);
}

static s32 Nem_CritHealth(void) {
    return Nem_Crit(NS_HEALTH);
}

static s32 Nem_MigrateFrames(s32 hunted) {
    s32 secs = (s32)((hunted ? 90 : 180) - 12.0f * Nem_StatEff(NS_TRACK));

    return MAX(secs, 30) * FPS;
}

// Next area on the shortest path from one area to another.
static s32 V2_StepToward(s32 from, s32 to) {
    s8 dist[ARRAY_COUNT(sZones)];
    s8 queue[ARRAY_COUNT(sZones)];
    s32 nb[ARRAY_COUNT(sZones)];
    s32 head = 0;
    s32 tail = 0;
    s32 k;
    s32 n;
    s32 best = from;

    if ((from < 0) || (to < 0) || (from == to) || (from >= ZONE_COUNT) || (to >= ZONE_COUNT)) {
        return from;
    }
    for (k = 0; k < ZONE_COUNT; k++) {
        dist[k] = -1;
    }
    dist[to] = 0;
    queue[tail++] = to;
    while (head < tail) {
        s32 z = queue[head++];

        n = Zone_Neighbors(z, nb);
        for (k = 0; k < n; k++) {
            if (dist[nb[k]] < 0) {
                dist[nb[k]] = dist[z] + 1;
                queue[tail++] = nb[k];
            }
        }
    }
    n = Zone_Neighbors(from, nb);
    for (k = 0; k < n; k++) {
        if ((dist[nb[k]] >= 0) && ((best == from) || (dist[nb[k]] < dist[best]))) {
            best = nb[k];
        }
    }
    return best;
}
static Actor* sNemCandidate = NULL; // took you below 2 hearts
static s32 sNemCandidatePool = -1;
static s32 sNemCrash = false;
static s32 sNemRestSlot = -1;     // slain during this dawn/dusk: the moon waits for the next
static s32 sNemEscaped = false;   // it hurt you and you left it behind
static s32 sPendingPromote = -1;  // enemy type that knocked you low and lived

static s32 sV2PrevHp = -1;
static s32 sWoundTimer = 0; // Open Wounds: no healing until this runs out
static s32 sAreaTimer = 0;
static s32 sAmbushDone = false;
static s32 sStillFrames = 0;
static s32 sTearsTimer = 40 * FPS;
static s32 sRotTimer = 20 * FPS;
static s32 sColdTimer = 0;
static s32 sEscortTimer = 0;
static s32 sBleedTimer = 0;
static s32 sBleedAmount = 0;
static s32 sPrevMagic = -1;
static u8 sPrevAmmo[5];
static s32 sAmmoReady = false;
static s32 sThornTimer = 0;
static s32 sNightDark = false;
static char sV2Toast[48];

static void V2_Toast(const char* a, s32 num, const char* b) {
    s32 n = Ev_Append(sV2Toast, 0, a, sizeof(sV2Toast));

    if (num >= 0) {
        n = Ev_AppendNum(sV2Toast, n, num, sizeof(sV2Toast));
    }
    Ev_Append(sV2Toast, n, b, sizeof(sV2Toast));
    Menu_ShowToast(sV2Toast);
}

// Difficulty tiers (set with the player): 0 easy/mild, 1 medium, 2 hard, 3 brutal.
static s32 Pool_Tier(s32 pool) {
    switch (pool) {
        case 8:  // Chuchu
        case 9:  // Tektite
        case 10: // Blue Bubble
        case 5:  // Stalchild
        case 22: // Hiploop
            return 0;
        case 28: // Peahat
        case 19: // Like Like
        case 13: // Dodongo
        case 30: // Garo Master
        case 33: // Big Poe
        case 32: // Dinolfos
            return 2;
        case 31: // Iron Knuckle
            return 3;
        default: // Wolfos, White Wolfos, Poe, Snapper, ReDead...
            return 1;
    }
}

static s32 Ev_Tier(s32 ev) {
    switch (ev) {
        case EV_JOKE:
        case EV_LEVIATHAN:
        case EV_LOWGRAV:
        case EV_RUPOOR_RAIN:
        case EV_STILL:
        case EV_MIRROR:
        case EV_ECHO:
        case EV_INFIGHT:
        case EV_TREASURE:
        case EV_PREY:
        case EV_PROCESSION:
        case EV_QUIZ:
            return 0;
        case EV_MOONFALL:
        case EV_PHANTOM:
        case EV_HUNGER:
        case EV_BULLETHELL:
        case EV_MAJORA:
            return 2;
        case EV_BLOOD_MOON:
        case EV_NEMESIS:
        case EV_CHAMPION:
            return 3;
        default:
            return V3_IsEvent(ev) ? V3_Tier(ev) : 1;
    }
}

static void V2_GiveMarks(s32 amount, const char* why) {
    s32 n;

    sMarks = MIN(sMarks + amount, 60000);
    n = Ev_Append(sV2Toast, 0, why, sizeof(sV2Toast));
    n = Ev_AppendNum(sV2Toast, n, amount, sizeof(sV2Toast));
    Ev_Append(sV2Toast, n, " Moon Marks", sizeof(sV2Toast));
    Menu_Notify(sV2Toast, NOTE_MARKS);
    Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
}

// Earned Marks: each active mutator adds 10% (keeping them is worth something).
static void V2_Earn(s32 amount, const char* why) {
    s32 muts = gOpt[ID_V2_MUT] ? sMutCount : 0;

    V2_GiveMarks(amount + amount * muts / 10, why);
}

// ---------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------

static void Contract_New(s32 slot) {
    s32 tries;

    for (tries = 0; tries < 20; tries++) {
        s32 type = V2_Rand(CT_TYPES);
        s32 k;
        s32 dup = false;

        for (k = 0; k < 3; k++) {
            if ((k != slot) && (sContracts[k].type == type)) {
                dup = true;
            }
        }
        if (!dup) {
            sContracts[slot].type = type;
            sContracts[slot].progress = 0;
            return;
        }
    }
    sContracts[slot].type = CT_KILLS;
    sContracts[slot].progress = 0;
}

static void Contract_Add(s32 type, s32 amount) {
    s32 i;

    if (!V2_ON(ID_V2_BOUNTY)) {
        return;
    }
    for (i = 0; i < 3; i++) {
        if (sContracts[i].type == type) {
            sContracts[i].progress = MIN(sContracts[i].progress + amount, 255);
            if (sContracts[i].progress >= sContractGoal[type]) {
                V2_Earn(sContractPay[type], "Contract done! +");
                Contract_New(i);
            }
        }
    }
}

// Called from the event code.
static void V2_OnEventStart(void) {
    sHitDuringEvent = false;
    sQuizMistakes = 0;
}

static s32 sCallPending = false;  // Call an Event (or the menu) is starting one
static s32 sFreeRollPending = false; // menu "Trigger event": its roulette starts it a bit later
static s32 sEventWasCalled = false; // the running event was bought (counts for contracts too)

static s32 V3_Failed(void);

// V3 payouts: by the event's tier (Fierce Moon / Empower count a tier up),
// +50% without a hit, half if you failed its challenge, +25% if a friend sent it.
static void V2_OnEventSurvived(s32 ev, s32 combo) {
    static const u8 sTierMarks[5] = { 1, 3, 6, 10, 14 };
    s32 bump = (Intensity() >= INTENSITY_BRUTAL) ? 1 : 0;

    if (V2_ON(ID_V2_BOUNTY) && !sEventWasCalled) {
        s32 pay = sTierMarks[Ev_Tier(ev) + bump] + ((combo >= 0) ? sTierMarks[Ev_Tier(combo) + bump] : 0);

        if (V3_Failed()) {
            pay = (pay + 1) / 2;
        } else if (!sHitDuringEvent) {
            pay += (pay + 1) / 2;
        }
        if (sEventByFriend) {
            pay += (pay + 3) / 4;
        }
        V2_Earn(pay, sHitDuringEvent ? "Survived! +" : "Survived untouched! +");
    }
    Contract_Add(CT_SURVIVE, 1);
    if (combo >= 0) {
        Contract_Add(CT_COMBO, 1);
    }
    if (!sHitDuringEvent) {
        Contract_Add(CT_NOHIT, 1);
    }
    if (Ev_Tier(ev) >= 3) {
        Contract_Add(CT_BRUTAL, 1); // survived a Brutal-tier event
    }
    if ((ev == EV_QUIZ) && (sQuizMistakes == 0)) {
        Contract_Add(CT_QUIZ, 1);
    }
}

// ---------------------------------------------------------------------------
// Spawning our persistent enemies (bounties, Nemesis, mutator extras)
// ---------------------------------------------------------------------------

static Vec3f sEntryPos;
static s32 sSpawnWait = 0; // bounty/Nemesis delay after entering an area

// A spot well away from where you came into the area, so targets aren't
// waiting at the door. Falls back to anything if the area is small.
static s32 V2_FarSpot(PlayState* play, s32 tries, Vec3f* out) {
    Player* player = GET_PLAYER(play);
    f32 needed = (tries < 20) ? 650.0f : (tries < 40) ? 400.0f : 0.0f;
    s32 k;

    for (k = 0; k < 4; k++) {
        if (Ev_FindSpot(play, 350.0f, 900.0f, player->actor.shape.rot.y, 0x7FFF, true, out) &&
            (Ev_DistXZ(out, &sEntryPos) >= needed)) {
            return true;
        }
    }
    return false;
}

static Actor* V2_SpawnEnemy(PlayState* play, s32 pool, Vec3f* at, f32 minD, f32 maxD, u8 tag) {
    const SpawnDef* def = &sPool[pool];
    Player* player = GET_PLAYER(play);
    u32 savedClear = play->actorCtx.sceneFlags.clearedRoom;
    Vec3f spot;
    Actor* actor;

    if (player == NULL) {
        return NULL;
    }
    if (at != NULL) {
        spot = *at;
    } else if (!Ev_FindSpot(play, minD, maxD, player->actor.shape.rot.y, 0x7FFF, true, &spot)) {
        return NULL;
    }
    spot.y += def->yOffset;
    play->actorCtx.sceneFlags.clearedRoom = 0; // a cleared room still lets these in
    actor = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, Math_Vec3f_Yaw(&spot, &player->actor.world.pos),
                     def->rotZ, def->params, tag);
    play->actorCtx.sceneFlags.clearedRoom = savedClear;
    if (actor != NULL) {
        Ev_AfterSpawn(play, actor);
    }
    return actor;
}

static void V2_ApplyTraits(Actor* actor, u16 traits, f32 hpMult) {
    TagData* t = Tag_Get(actor);
    s32 hp;

    if (t == NULL) {
        return;
    }
    t->traits = traits;
    hp = (s32)(MAX(actor->colChkInfo.health, 1) * hpMult);
    actor->colChkInfo.health = CLAMP(hp, 1, 250);
    // Health critical mass: a Nemesis keeps the health the game has no room for in reserve.
    t->reserve = ((t->tag == TAG_NEMV2) && Nem_CritHealth() && (hp > 250)) ? (u16)MIN(hp - 250, 60000) : 0;
    t->maxHp = actor->colChkInfo.health;
    t->hp2 = actor->colChkInfo.health;
    t->v2flags |= TAGF_V2_SEEN | TAGF_V2_TOUGH;
}

static s32 V2_IsLive(PlayState* play, Actor* target) {
    s32 c;

    if (target == NULL) {
        return false;
    }
    for (c = 0; c < ACTORCAT_MAX; c++) {
        Actor* actor = play->actorCtx.actorLists[c].first;

        while (actor != NULL) {
            if (actor == target) {
                return (actor->update != NULL) && (actor->colChkInfo.health > 0);
            }
            actor = actor->next;
        }
    }
    return false;
}

static Actor* sLastHitter = NULL;
static s32 sLastHitterTimer = 0;

// Closest living enemy that could be a Nemesis.
static Actor* V2_NearestEnemy(PlayState* play, f32 range) {
    Actor* best = NULL;
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        if ((actor->update != NULL) && (actor->colChkInfo.health > 0) && (actor->xzDistToPlayer < range) &&
            ((best == NULL) || (actor->xzDistToPlayer < best->xzDistToPlayer))) {
            best = actor;
        }
        actor = actor->next;
    }
    return best;
}

// The enemy-pool entry an actor matches. The exact variant (same params) wins,
// so a White Wolfos stays white and a Fire Keese stays on fire.
static s32 V2_PoolOfActor(Actor* actor) {
    s32 i;
    s32 loose = -1;

    for (i = 0; i < POOL_FIRST_BOSS; i++) {
        if ((sPool[i].actorId != actor->id) || (i == 18)) {
            continue;
        }
        if (sPool[i].params == actor->params) {
            return i;
        }
        if (loose < 0) {
            loose = i;
        }
    }
    return loose;
}

// ---------------------------------------------------------------------------
// Nemesis
// ---------------------------------------------------------------------------

#define NEM_LEVEL_MAX 200

// V3: every enemy type can reach level 200, and nothing holds it back to your level.
static s32 Nem_TypeCap(s32 pool) {
    return NEM_LEVEL_MAX;
}

static s32 Nem_Cap(void) {
    return NEM_LEVEL_MAX;
}

static s32 Nem_EffLevel(void) {
    s32 lvl = sNem.level + ((gOpt[ID_V2_MUT] && Mut_Has(MUT_EMBOLDENED)) ? 1 : 0);

    return CLAMP(lvl, 1, NEM_LEVEL_MAX);
}

// Weak enemy types get sturdier as a Nemesis, so a Chuchu Nemesis still matters.
// +20% health per level up to 10, +10% per level after that; scarred ones +50%.
static f32 Nem_HpMult(void) {
    static const f32 sTierHp[4] = { 1.5f, 1.2f, 1.0f, 1.0f };
    s32 lvl = Nem_EffLevel();
    f32 lvlMult = 1.0f + 0.2f * MIN(lvl, 10) + 0.1f * MAX(lvl - 10, 0);

    return lvlMult * (1.0f + 0.15f * Nem_StatEff(NS_HEALTH)) * sTierHp[Pool_Tier(sNem.pool)] *
           (sNem.scarred ? 1.5f : 1.0f);
}

static u16 Nem_Traits(void);
u16 V2_NemTraitMask(void) {
    return (sNem.pool >= 0) ? Nem_Traits() : 0;
}

static u16 Nem_Traits(void) {
    s32 active = MIN(sNem.traitCount, (Nem_EffLevel() + 1) / 2);
    u16 mask = 0;
    s32 i;

    for (i = 0; i < active; i++) {
        mask |= (1 << sNem.traitOrder[i]);
    }
    return mask | sNemBonusTraits; // your friend's gifts are always on
}

// A Nemesis already out in the world picks up new traits right away.
static void Nem_RefreshLive(PlayState* play) {
    TagData* t;

    if ((play == NULL) || (sNem.actor == NULL) || !V2_IsLive(play, sNem.actor)) {
        return;
    }
    t = Tag_Get(sNem.actor);
    if (t != NULL) {
        t->traits = Nem_Traits();
    }
}

static void Nem_AddTrait(void) {
    s32 tries;

    if (sNem.traitCount >= 6) {
        return;
    }
    for (tries = 0; tries < 30; tries++) {
        s32 tr = Trait_Random();
        s32 i;
        s32 dup = false;

        for (i = 0; i < sNem.traitCount; i++) {
            if (sNem.traitOrder[i] == tr) {
                dup = true;
            }
        }
        if (!dup) {
            sNem.traitOrder[sNem.traitCount++] = tr;
            return;
        }
    }
}

static void Nem_Name(char* out, s32 size) {
    s32 n;

    if (sNemCustom[0] != '\0') {
        Ev_Append(out, 0, sNemCustom, size); // your friend named it
        return;
    }
    if ((sNem.nameA >= NEM_FULL_FIRST) && (sNem.nameA < NEM_FULL_FIRST + (s32)ARRAY_COUNT(sNemFull))) {
        Ev_Append(out, 0, sNemFull[sNem.nameA - NEM_FULL_FIRST], size);
        return;
    }
    n = Ev_Append(out, 0, sNemA[sNem.nameA % 24], size);
    Ev_Append(out, n, sNemB[sNem.nameB % 16], size);
}

// A new or slain Nemesis forgets what your friend did to the last one.
static void Nem_ClearFriendStuff(void) {
    sNemCustom[0] = '\0';
    sNemBonusTraits = 0;
}

static void Nem_Promote(PlayState* play, s32 pool, Actor* actor) {
    char name[24];

    if (!V2_ON(ID_V2_NEM) || (sNem.pool >= 0) || (pool < 0) || (pool >= POOL_FIRST_BOSS) || (pool == 18)) {
        return;
    }
    Nem_ClearFriendStuff();
    Nem_ResetStats();
    sNem.pool = pool;
    // 1 in 32 get a whole name from the list.
    sNem.nameA = (V2_Rand(32) == 0) ? NEM_FULL_FIRST + V2_Rand(ARRAY_COUNT(sNemFull)) : V2_Rand(24);
    sNem.nameB = V2_Rand(16);
    sNem.level = CLAMP(sNemInherit, 1, Nem_TypeCap(pool)); // a purged Nemesis passes its levels on
    sNem.traitCount = 0;
    {
        s32 k;

        for (k = 0; k < sNem.level; k++) {
            Nem_AddTrait(); // fresh, random traits
        }
    }
    sNemInherit = 0;
    sNem.zone = (play != NULL) ? V2_ZoneNow(play) : 1;
    if (sNem.zone < 0) {
        sNem.zone = 1;
    }
    sNem.wins = 0;
    sNem.migrate = Nem_MigrateFrames(false) * 2; // gives you a head start
    sNem.actor = NULL;
    sNem.spawnedVisit = true; // it lets you go this time
    sNem.engaged = false;
    sNem.style = V2_Rand(2);
    sNem.scarred = 0;
    sNem.hpPct = 100;
    sNem.dormant = 0;
    sNemStolen = 0;
    Nem_Name(name, sizeof(name));
    V2_Toast("A Nemesis rises: ", -1, name);
    Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
    // The enemy itself becomes the Nemesis right away, if it's still here.
    if ((actor != NULL) && (play != NULL) && V2_IsLive(play, actor)) {
        TagData* t = Tag_Get(actor);

        if ((t != NULL) && (t->tag == TAG_NONE)) {
            t->tag = TAG_NEMV2;
            t->v2flags &= ~TAGF_V2_ELITEFOE; // it's the Nemesis now, not an elite
            V2_ApplyTraits(actor, Nem_Traits(), Nem_HpMult());
            sNem.actor = actor;
        }
    }
}

static void Nem_LevelUp(const char* why) {
    if (sNem.pool < 0) {
        return;
    }
    if (sNem.level >= Nem_TypeCap(sNem.pool)) {
        V2_Toast("Your Nemesis is at its peak.", -1, "");
        return;
    }
    sNem.level++;
    Nem_AddTrait();
    Nem_RefreshLive(gPlayState_v2);
    V2_Toast(why, -1, "");
}

static void Nem_Slain(PlayState* play) {
    s32 pay = 30 + 8 * Nem_EffLevel() + 5 * MIN(sNem.traitCount, 6) + 2 * sNemStolen;
    char name[24];

    sNemStolen = 0;
    sNemSlain++;
    Nem_Name(name, sizeof(name));
    // V3: one in four crawls away and comes back later, scarred and tougher.
    if (!sNem.scarred && (Rand_ZeroOne() < 0.25f)) {
        V2_Earn(pay, "Nemesis beaten! +");
        sNem.scarred = 1;
        sNem.dormant = 600;
        sNem.hpPct = 100;
        sNem.actor = NULL;
        sNem.zone = V2_Neighbor(V2_Neighbor(Zone_Outdoor(sNem.zone)));
        V2_Banner(name, "crawled away. It will return, scarred.");
        return;
    }
    sNem.pool = -1;
    sNem.actor = NULL;
    Nem_ClearFriendStuff();
    Nem_ResetStats();
    sNemRestSlot = Mut_SlotNow();
    V2_Earn(pay, "Nemesis slain! +");
    V2_Banner("NEMESIS DEFEATED", name);
}

// near: right by Link (Friend Link), otherwise well away from where he came in.
static Actor* Nem_SpawnEx(PlayState* play, s32 near) {
    Actor* actor;
    f32 diff = (gOpt[ID_V2_NEMDIFF] % 3 == 0) ? 0.8f : (gOpt[ID_V2_NEMDIFF] % 3 == 2) ? 1.3f : 1.0f;

    if (near) {
        actor = V2_SpawnEnemy(play, sNem.pool, NULL, 220.0f, 420.0f, TAG_NEMV2);
        if (actor == NULL) {
            actor = V2_SpawnEnemy(play, sNem.pool, NULL, 120.0f, 700.0f, TAG_NEMV2);
        }
        if (actor == NULL) {
            return NULL;
        }
    } else {
        Vec3f far;

        actor = V2_FarSpot(play, sNem.tries, &far) ? V2_SpawnEnemy(play, sNem.pool, &far, 0, 0, TAG_NEMV2) : NULL;
    }
    if (actor == NULL) {
        if (++sNem.tries > 60) {
            sNem.spawnedVisit = true; // no room here; try another visit
        }
        return NULL;
    }
    V2_ApplyTraits(actor, Nem_Traits(), Nem_HpMult() * diff);
    sNem.actor = actor;
    sNem.spawnedVisit = true;
    sNem.engaged = false;
    // Bodyguards: one at level 5, two at level 8.
    {
        static const u8 sEscorts[] = { 3, 5, 7, 9 }; // Wolfos, Stalchild, Poe, Tektite
        s32 lvl = Nem_EffLevel();
        s32 count = (lvl >= 8) ? 2 : (lvl >= 5) ? 1 : 0;
        s32 k;

        sGuards[0] = sGuards[1] = NULL;
        for (k = 0; k < count; k++) {
            CollisionPoly* poly;
            s32 bgId;
            Vec3f at = actor->world.pos;
            Vec3f probe;
            f32 floorY;

            at.x += Math_SinS(actor->shape.rot.y + ((k == 0) ? 0x4000 : -0x4000)) * 90.0f;
            at.z += Math_CosS(actor->shape.rot.y + ((k == 0) ? 0x4000 : -0x4000)) * 90.0f;
            probe = at;
            probe.y += 150.0f;
            floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
            if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - actor->world.pos.y) > 200.0f)) {
                continue;
            }
            at.y = floorY;
            sGuards[k] = V2_SpawnEnemy(play, sEscorts[V2_Rand(ARRAY_COUNT(sEscorts))], &at, 0, 0, TAG_V2);
            if (sGuards[k] != NULL) {
                TagData* gt = Tag_Get(sGuards[k]);

                V2_ApplyTraits(sGuards[k], 1 << TR_ARMORED, 1.25f);
                if (gt != NULL) {
                    gt->v2flags |= TAGF_V2_GUARD;
                }
                Ev_Poof(play, &sGuards[k]->world.pos, 1);
            }
        }
    }
    Ev_Poof(play, &actor->world.pos, 1);
    Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
    return actor;
}

static void Nem_Spawn(PlayState* play) {
    Nem_SpawnEx(play, false);
}

// ---------------------------------------------------------------------------
// Bounties
// ---------------------------------------------------------------------------

// 4 to 14 minutes, at least 90 seconds apart from the other bounties' moves.
// How long until a bounty considers moving again, by personality.
static s32 Bounty_MoveTime(s32 i) {
    static const u8 sLo[MOTIVE_COUNT] = { 40, 30, 60, 20, 30 };
    static const u8 sHi[MOTIVE_COUNT] = { 110, 70, 150, 50, 80 };
    s32 m = sBounties[i].motive % MOTIVE_COUNT;

    return (sLo[m] + V2_Rand(sHi[m] - sLo[m] + 1)) * FPS;
}

u16 V2_BountyTraits(s32 i) {
    return ((i >= 0) && (i < V2_BOUNTY_SLOTS)) ? sBounties[i].traits : 0;
}

s32 V2_BountyActive(s32 i) {
    return (i >= 0) && (i < V2_BOUNTY_SLOTS) && (sBounties[i].pool >= 0);
}

s32 V2_BountyElite(s32 i) {
    return V2_BountyActive(i) && (sBounties[i].kind == BK_WANTED);
}

static s32 Rank_Of(s32 done) {
    s32 r = 0;
    s32 k;

    for (k = 0; k < 6; k++) {
        if (done >= sRankAt[k]) {
            r = k;
        }
    }
    return r;
}

// Base pay by stars and kind, plus 5% per Hunter Rank.
static s32 Bounty_Pay(Bounty* b) {
    static const u8 sStarPay[5] = { 10, 18, 28, 40, 55 };
    static const u8 sTierPct[4] = { 100, 120, 150, 200 }; // tougher enemies pay more
    s32 traits = 0;
    s32 k;
    s32 pay;

    for (k = 0; k < TR_COUNT; k++) {
        traits += (b->traits >> k) & 1;
    }
    pay = sStarPay[CLAMP(b->stars, 1, 5) - 1] * sTierPct[Pool_Tier(b->pool)] / 100 + 5 * traits;
    pay = (b->kind == BK_WANTED) ? pay * 2 : (b->kind == BK_RARE) ? pay * 2 : pay;
    return pay * (100 + 5 * Rank_Of(sBountiesDone)) / 100;
}

static s32 Bounty_BonusMet(Bounty* b) {
    switch (b->bonus) {
        case BONUS_QUICK: return b->aliveTimer <= QUICK_FRAMES;
        case BONUS_UNTOUCHED: return !b->hitSince;
        case BONUS_NIGHT: return Ev_IsNight();
        case BONUS_DAY: return !Ev_IsNight();
        default: return false;
    }
}

s32 V2_BountyBonus(s32 i) {
    return V2_BountyActive(i) && (sBounties[i].bonus != BONUS_NONE);
}

static void Bounty_Fill(s32 i, s32 pool, s32 stars, s32 kind) {
    Bounty* b = &sBounties[i];
    s32 k;

    b->pool = pool;
    b->stars = stars;
    b->kind = kind;
    b->traits = 0;
    // One distinct trait per star.
    for (k = 0; k < b->stars;) {
        u16 bit = 1 << Trait_Random();

        if (!(b->traits & bit)) {
            b->traits |= bit;
            k++;
        }
    }
    b->zone = V2_Rand(ZONE_OUTDOOR); // V3: any outdoor area, Clock Town too
    b->migrate = Bounty_MoveTime(i);
    b->nameIdx = V2_Rand(24);
    b->hpPct = 100;
    bzero(&b->body, sizeof(b->body));
    {
        // Personalities: roaming, migrating, nesting, hunting you (1 in 5), skittish.
        f32 r = Rand_ZeroOne();

        b->motive = (r < 0.20f) ? MOTIVE_HUNT : (r < 0.45f) ? MOTIVE_ROAM : (r < 0.65f) ? MOTIVE_MIGRATE
                  : (r < 0.88f) ? MOTIVE_NEST : MOTIVE_SKITTISH;
        b->target = V2_Rand(ZONE_OUTDOOR);
    }
    b->actor = NULL;
    b->spawnedVisit = false;
    b->tries = 0;
    b->accepted = false;
    // Specials always have a bonus objective; regulars most of the time.
    b->bonus = ((kind != BK_NORMAL) || (Rand_ZeroOne() < 0.6f)) ? 1 + V2_Rand(BONUS_COUNT - 1) : BONUS_NONE;
    b->hitSince = false;
    b->aliveTimer = 0;
}

// Regular slots get a fresh bounty; the WANTED and rare slots go empty.
static void Bounty_New(s32 i) {
    if (i >= 3) {
        sBounties[i].pool = -1;
        sBounties[i].actor = NULL;
        sBounties[i].accepted = false;
        return;
    }
    Bounty_Fill(i, sHuntPool[V2_Rand(ARRAY_COUNT(sHuntPool))], 1 + V2_Rand(5), BK_NORMAL);
}

static void V2_InitOnce(void) {
    s32 i;

    if (sV2Ready) {
        return;
    }
    sV2Ready = true;
    Zone_InitNames();
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        Bounty_New(i);
    }
    for (i = 0; i < 3; i++) {
        Contract_New(i);
    }
}

// ---------------------------------------------------------------------------
// Damage and health hooks
// ---------------------------------------------------------------------------

static Actor* sV2LastDamaged = NULL;

RECOMP_HOOK("Actor_ApplyDamage") void V2_BeforeApplyDamage(Actor* actor) {
    TagData* t = Tag_Get(actor);
    Player* player;
    s32 dmg;

    if ((actor == NULL) || (actor->category == ACTORCAT_PLAYER) || (gPlayState_v2 == NULL)) {
        return;
    }
    player = GET_PLAYER(gPlayState_v2);
    dmg = actor->colChkInfo.damage;
    if (dmg == 0) {
        return;
    }
    if (sGigaArmed && (player != NULL) && (player->meleeWeaponState != 0) && (sPouch[PI_GIGA] > 0)) {
        dmg *= 3;
        if (--sPouch[PI_GIGA] == 0) {
            sGigaArmed = false;
        }
    }
    if (sPactTimer > 0) {
        dmg *= 2;
    }
    if ((t != NULL) && (t->traits & (1 << TR_ARMORED))) {
        dmg = (dmg + 1) / 2;
    }
    if ((t != NULL) && (t->traits & (1 << TR_MOONBLESSED)) && (sActive >= 0)) {
        // Moonblessed: nothing hurts it while an event runs.
        actor->colChkInfo.damage = 0;
        Actor_SetColorFilter(actor, COLORFILTER_COLORFLAG_BLUE, 255, COLORFILTER_BUFFLAG_OPA, 8);
        return;
    }
    if ((t != NULL) && (t->reserve > 0) && (dmg >= actor->colChkInfo.health)) {
        // Health critical mass: topped up from its reserve.
        s32 add = MIN(t->reserve, 250 - actor->colChkInfo.health);

        actor->colChkInfo.health += add;
        t->reserve -= add;
    }
    if ((t != NULL) && (t->traits & (1 << TR_THORNED)) && (player != NULL) && (player->meleeWeaponState != 0)) {
        sThornTimer = 1; // stings you back next frame
    }
    if ((t != NULL) && (t->tag == TAG_NEMV2)) {
        Contract_Add(CT_NEMHITS, 1);
    }
    // Nemesis Brand: the next roaming enemy you hurt is crowned.
    if (sBrandArmed && (t != NULL) && (t->tag == TAG_NONE) && (actor->category == ACTORCAT_ENEMY) &&
        (sNem.pool < 0) && (sPouch[PI_BRAND] > 0)) {
        s32 pool = V2_PoolOfActor(actor);
        s32 k;

        for (k = 0; k < (s32)ARRAY_COUNT(sHuntPool); k++) {
            if (sHuntPool[k] == pool) {
                sBrandArmed = false;
                sPouch[PI_BRAND]--;
                Nem_Promote(gPlayState_v2, pool, actor);
                if (sNem.actor == actor) {
                    actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
                    sNem.body.phase = 1;
                    sNem.zone = V2_ZoneNow(gPlayState_v2);
                }
                break;
            }
        }
    }
    actor->colChkInfo.damage = (u8)CLAMP(dmg, 1, 255);
    sV2LastDamaged = actor;
}


// Brutal enemies near Link make his losses bigger.
static s32 V2_BrutalNear(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* actor;

    if (player == NULL) {
        return false;
    }
    actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (actor != NULL) {
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (actor->xzDistToPlayer < 180.0f) &&
            ((t->traits & (1 << TR_ENRAGED)) && (actor->colChkInfo.health * 2 < t->maxHp))) {
            return true;
        }
        actor = actor->next;
    }
    return false;
}

// Runs after every Health_ChangeBy (after Glass Moon).
static void V2_AfterHealthChange(s32 before) {
    s32 hp = gSaveContext.save.saveInfo.playerData.health;
    s32 delta = hp - before;
    PlayState* play = gPlayState_v2;

    if (delta < 0) {
        s32 loss = -delta;
        s32 extra = 0;

        if (sShieldUp && (sPouch[PI_SHIELD] > 0) && !gEchoOneShot) {
            gSaveContext.save.saveInfo.playerData.health = before; // blocked
            if (--sPouch[PI_SHIELD] == 0) {
                sShieldUp = false;
            }
            sPouchFlash = 10;
            Audio_PlaySfx(NA_SE_IT_SHIELD_REFLECT_SW);
            return;
        }
        if (sActive >= 0) {
            sHitDuringEvent = true;
        }
        {
            s32 k;

            for (k = 0; k < V2_BOUNTY_SLOTS; k++) {
                if (sBounties[k].actor != NULL) {
                    sBounties[k].hitSince = true; // Untouched bonus lost
                }
            }
        }
        if (gOpt[ID_V2_MUT] && Mut_Has(MUT_THINSKIN)) {
            extra += loss / 2;
        }
        // Nemesis Power: +25% damage per point when it's the one that hit you.
        if ((play != NULL) && (sNem.actor != NULL) && (sLastHitter == sNem.actor) && V2_IsLive(play, sNem.actor)) {
            extra += (s32)(loss * Nem_StatEff(NS_POWER) / 4.0f);
        }
        if (sPactTimer > 0) {
            extra += loss;
        }
        if ((play != NULL) && V2_BrutalNear(play)) {
            extra += loss / 2;
        }
        if (extra > 0) {
            gSaveContext.save.saveInfo.playerData.health = MAX(hp - extra, 0);
        }
        if (play != NULL) {
            // Leech and Thief only act when they're the one that hit you.
            Actor* hitter = ((sLastHitter != NULL) && V2_IsLive(play, sLastHitter)) ? sLastHitter : NULL;
            TagData* ht = (hitter != NULL) ? Tag_Get(hitter) : NULL;

            if ((hitter != NULL) && (hitter == sNem.actor)) {
                sNem.engaged = true; // it hurt you: leaving it behind now counts as getting away
            }
            if (ht != NULL) {
                ht->linkHits = MIN(ht->linkHits + 1, 250);
            }
            if ((ht != NULL) && (ht->traits & (1 << TR_FROSTBRAND))) {
                gChillTimer = 4 * FPS; // chilled
                Actor_SetColorFilter(&GET_PLAYER(play)->actor, COLORFILTER_COLORFLAG_BLUE, 255, COLORFILTER_BUFFLAG_OPA, 20);
            }
            if ((ht != NULL) && (ht->traits & (1 << TR_THIEF)) && (sMarks > 0)) {
                s32 take = MIN(3, sMarks);

                sMarks -= take;
                ht->stolen = MIN(ht->stolen + take, 250);
                if (hitter == sNem.actor) {
                    sNemStolen = MIN(sNemStolen + take, 60000);
                }
                V2_Toast("Stolen: -", take, " Moon Marks");
            }
            if ((ht != NULL) && (ht->traits & (1 << TR_MARKHUNGRY))) {
                s32 pts = MIN(sMarks / 50, 3);

                if (pts > 0) {
                    gSaveContext.save.saveInfo.playerData.health =
                        MAX(gSaveContext.save.saveInfo.playerData.health - loss * pts / 4, 0);
                }
            }
        }
        if (gOpt[ID_V2_MUT] && Mut_Has(MUT_BUTTERFINGERS)) {
            s32 wallet = gSaveContext.save.saveInfo.playerData.rupees + gSaveContext.rupeeAccumulator;

            if (wallet > 0) {
                Rupees_ChangeBy(-MIN(5, wallet));
            }
        }
        if (gOpt[ID_V2_MUT] && Mut_Has(MUT_BLEEDING)) {
            sBleedTimer = 2 * FPS;
            sBleedAmount = MIN(sBleedAmount + 0x20, 0x40); // two hearts, a moment later
        }
    }
    // Second Wind: a killing blow leaves you at 4 hearts instead.
    if ((before > 0) && (gSaveContext.save.saveInfo.playerData.health <= 0) && (sPouch[PI_SECONDWIND] > 0)) {
        sPouch[PI_SECONDWIND]--;
        gSaveContext.save.saveInfo.playerData.health = MIN(0x40, gSaveContext.save.saveInfo.playerData.healthCapacity);
        Menu_Notify("Second Wind! You got back up.", NOTE_MARKS);
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    }
}

// Everything that reacts to Link's health changing (Glass Moon, mutators,
// Moon Shield, Blood Pact...). Called from the damage hook, or, if another
// mod (like the randomizer) replaced the game's damage function so the hook
// never runs, from the per-frame check in V2_Update.
static void Health_React(s32 before) {
    s32 hp = gSaveContext.save.saveInfo.playerData.health;
    s32 loss = before - hp;

    if ((loss > 0) && (sActive >= 0) && Curse_Is(CURSE_GLASS)) {
        gSaveContext.save.saveInfo.playerData.health = MAX(hp - loss, 0);
    }
    V2_AfterHealthChange(before);
}

// ---------------------------------------------------------------------------
// Enemy scan: deaths, first sightings (Tough Crowd)
// ---------------------------------------------------------------------------

static void V2_OnEnemyDeath(PlayState* play, Actor* actor, TagData* t) {
    s32 i;

    Contract_Add(CT_KILLS, 1);
    if (sActive >= 0) {
        Contract_Add(CT_EVENT_KILLS, 1);
        if (Ev_IsOn(EV_BLOOD_MOON)) {
            Contract_Add(CT_BLOODMOON, 1);
        }
    }
    if (t->tag == TAG_BOUNTY) {
        for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
            if ((sBounties[i].pool >= 0) && (sBounties[i].actor == actor)) {
                s32 kind = sBounties[i].kind;
                s32 pay = Bounty_Pay(&sBounties[i]);
                s32 bonus = Bounty_BonusMet(&sBounties[i]);
                s32 rankBefore = Rank_Of(sBountiesDone);

                if (bonus) {
                    pay += pay / 2;
                }
                if (sLuckyCharges > 0) {
                    sLuckyCharges--;
                    pay *= 2;
                }
                sBountiesDone++;
                sHuntStreak++;
                V2_Earn(pay, bonus                ? "Bounty + bonus! +"
                                  : (kind == BK_FRIEND) ? "Friend bounty claimed! +"
                                  : (kind == BK_WANTED) ? "WANTED claimed! +"
                                  : (kind == BK_RARE)   ? "Rare bounty claimed! +"
                                                        : "Bounty claimed! +");
                if (Rank_Of(sBountiesDone) > rankBefore) {
                    V2_Toast("Hunter Rank up: ", -1, sRankNames[Rank_Of(sBountiesDone)]);
                    Audio_PlaySfx(NA_SE_SY_GET_ITEM);
                }
                Contract_Add(CT_BOUNTY, 1);
                if (bonus) {
                    Contract_Add(CT_BONUS, 1);
                }
                Bounty_New(i);
                // Hunt streak: three in one cycle pays a bonus and posts a rare bounty.
                if ((sHuntStreak >= 3) && !sStreakPaid) {
                    sStreakPaid = true;
                    V2_Earn(25, "Hunt streak bonus! +");
                    if (sBounties[SLOT_RARE].pool < 0) {
                        Bounty_Fill(SLOT_RARE, sRarePool[V2_Rand(ARRAY_COUNT(sRarePool))], 4, BK_RARE);
                        Menu_ShowToast("A rare bounty was posted!");
                    }
                }
            }
        }
    }
    if ((t->tag == TAG_NEMV2) && (actor == sNem.actor)) {
        Nem_Slain(play);
    }
    if (t->v2flags & TAGF_V2_GUARD) {
        Contract_Add(CT_GUARD, 1);
    }
    if (actor == sNemCandidate) {
        sNemCandidate = NULL;
    }
    // Death traits and mutators.
    if (t->stolen > 0) {
        V2_GiveMarks(t->stolen * 2, "Stolen Marks back x2! +");
        t->stolen = 0;
    }
    if (t->traits & (1 << TR_VENGEFUL)) {
        Vec3f from = actor->world.pos;

        from.y = Ev_SkyHeight(play, &actor->world.pos, 250.0f);
        Drop_Launch(play, &from, &actor->world.pos, 30);
    }
    if ((t->traits & (1 << TR_SPLITTER)) && !(t->v2flags & TAGF_V2_SPLIT)) {
        s32 pool = V2_PoolOfActor(actor);
        s32 k;

        for (k = 0; (k < 2) && (pool >= 0); k++) {
            Vec3f at = actor->world.pos;
            Actor* copy;

            at.x += (k == 0) ? 40.0f : -40.0f;
            copy = V2_SpawnEnemy(play, pool, &at, 0, 0, TAG_V2);
            if (copy != NULL) {
                TagData* ct = Tag_Get(copy);

                if (ct != NULL) {
                    ct->v2flags |= TAGF_V2_SPLIT;
                }
            }
        }
    }
    if (gOpt[ID_V2_MUT] && Mut_Has(MUT_REINFORCE) && (Rand_ZeroOne() < 0.25f) && (t->tag != TAG_V2)) {
        s32 pool = V2_PoolOfActor(actor);

        if (pool >= 0) {
            V2_SpawnEnemy(play, pool, NULL, 250.0f, 500.0f, TAG_V2);
        }
    }
}

static void V2_ScanEnemies(PlayState* play) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        Actor* next = actor->next;
        TagData* t = Tag_Get(actor);

        if ((actor->update != NULL) && (t != NULL) && (t->tag != TAG_KEEP)) {
            u8 hp = actor->colChkInfo.health;

            if (!(t->v2flags & TAGF_V2_TOUGH)) {
                t->v2flags |= TAGF_V2_TOUGH;
                if (gOpt[ID_V2_MUT] && Mut_Has(MUT_TOUGH) && (hp > 0) && (hp <= 60)) {
                    actor->colChkInfo.health = hp = (u8)MIN(hp + (hp + 1) / 2, 250);
                }
                t->maxHp = hp;
                // Elite Foes: some ordinary enemies get a trait (only ones that work on anything).
                if (gOpt[ID_V2_MUT] && Mut_Has(MUT_ELITE) && (t->traits == 0) && (t->tag != TAG_NEMV2) &&
                    (hp > 0) && (hp <= 60) && (Rand_ZeroOne() < 0.25f)) {
                    static const u8 sEliteTraits[] = { TR_ARMORED, TR_COWARD,  TR_REGEN,   TR_FIRE,
                                                       TR_VENGEFUL, TR_ENRAGED, TR_THORNED, TR_FROSTBRAND };

                    V2_ApplyTraits(actor, 1 << sEliteTraits[V2_Rand(ARRAY_COUNT(sEliteTraits))], 1.0f);
                    t->v2flags |= TAGF_V2_ELITEFOE;
                    hp = actor->colChkInfo.health;
                }
            }
            if ((t->v2flags & TAGF_V2_SEEN) && (t->hp2 > 0) && (hp == 0)) {
                V2_OnEnemyDeath(play, actor, t);
            }
            t->hp2 = hp;
            t->v2flags |= TAGF_V2_SEEN;
        }
        actor = next;
    }
}

// ---------------------------------------------------------------------------
// Per-actor traits (bounties, Nemesis), after each actor update
// ---------------------------------------------------------------------------

static void V2_AfterActorUpdate(PlayState* play, Actor* actor) {
    TagData* t = Tag_Get(actor);
    Player* player = GET_PLAYER(play);
    u16 tr;
    s32 enraged;
    s32 airborne;

    if ((t == NULL) || (player == NULL) || (actor->update == NULL) ||
        ((t->traits == 0) && (t->tag != TAG_NEMV2)) ||
        ((t->tag != TAG_BOUNTY) && (t->tag != TAG_NEMV2) && !(t->v2flags & TAGF_V2_ELITEFOE)) ||
        (actor->colChkInfo.health == 0)) {
        return;
    }
    tr = t->traits;
    enraged = (tr & (1 << TR_ENRAGED)) && (actor->colChkInfo.health * 2 < t->maxHp);
    t->v2timer++;
    // Coward: at 30% health it vanishes, and comes back healed later.
    if ((tr & (1 << TR_COWARD)) && !(t->v3flags & TAGF3_FLED) && (actor->colChkInfo.health * 10 <= t->maxHp * 3) &&
        ((t->tag == TAG_NEMV2) || (t->tag == TAG_BOUNTY))) {
        t->v3flags |= TAGF3_FLED;
        World_CowardFlees(play, actor, t);
        return;
    }
    // Nemesis critical mass powers.
    if ((t->tag == TAG_NEMV2) && (actor == sNem.actor)) {
        if (t->v3timer > 0) {
            t->v3timer--;
        }
        // Speed: lunges at you from mid range.
        if (Nem_Crit(NS_SPEED)) {
            if ((t->v3timer == 0) && (actor->xzDistToPlayer > 150.0f) && (actor->xzDistToPlayer < 450.0f)) {
                t->v3timer = 3 * FPS;
                Actor_PlaySfx(actor, NA_SE_EN_WIZ_LAUGH);
            }
            if (t->v3timer > 3 * FPS - 6) {
                Ev_StepToward(play, actor, &player->actor.world.pos, 35.0f);
            }
        }
        // Power: its blows go through your shield.
        if (Nem_Crit(NS_POWER) && (player->stateFlags1 & PLAYER_STATE1_400000) && (actor->xzDistToPlayer < 90.0f) &&
            ((t->v2timer % 30) == 0)) {
            func_800B8D50(play, actor, 6.0f, actor->yawTowardsPlayer, 4.0f, 0x10);
            Audio_PlaySfx(NA_SE_IT_SHIELD_REFLECT_SW);
        }
        // Health: slowly regenerates.
        if (Nem_Crit(NS_HEALTH) && ((t->v2timer % FPS) == 0) && (actor->colChkInfo.health < t->maxHp)) {
            actor->colChkInfo.health++;
        }
    }
    // Stuck-jump watchdog: a walking Nemesis that has been "in the air" right above
    // the floor for 3 seconds is put back down; after 6 it pops up somewhere else.
    if ((t->tag == TAG_NEMV2) && (actor == sNem.actor) && (actor->gravity < 0.0f)) {
        static s32 sNemAir = 0;

        if (actor->bgCheckFlags & BGCHECKFLAG_GROUND) {
            sNemAir = 0;
        } else if (++sNemAir == 3 * FPS) {
            Vec3f probe = actor->world.pos;
            CollisionPoly* poly;
            s32 bgId;
            f32 floorY;

            probe.y += 60.0f;
            floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
            if ((floorY > BGCHECK_Y_MIN) && (actor->world.pos.y - floorY < 80.0f)) {
                actor->world.pos.y = floorY;
                actor->velocity.y = -4.0f;
            }
        } else if (sNemAir >= 6 * FPS) {
            Vec3f spot;

            sNemAir = 0;
            if (Ev_FindSpot(play, 200.0f, 400.0f, player->actor.shape.rot.y, 0x7FFF, true, &spot)) {
                Ev_Poof(play, &actor->world.pos, 1);
                actor->world.pos = spot;
                actor->prevPos = spot;
                actor->velocity.y = 0.0f;
                Ev_Poof(play, &spot, 1);
            }
        }
    }
    airborne = (actor->gravity < 0.0f) && !(actor->bgCheckFlags & BGCHECKFLAG_GROUND);
    // Nemesis Speed: +12% movement per point (on the ground only).
    if ((t->tag == TAG_NEMV2) && (actor == sNem.actor) && (sNemStat[NS_SPEED] > 0) && !airborne) {
        f32 k = MIN(0.12f * Nem_StatEff(NS_SPEED), 1.5f);

        actor->world.pos.x += (actor->world.pos.x - actor->prevPos.x) * k;
        actor->world.pos.z += (actor->world.pos.z - actor->prevPos.z) * k;
    }

    if (enraged && !airborne) {
        actor->world.pos.x += (actor->world.pos.x - actor->prevPos.x) * 0.6f;
        actor->world.pos.z += (actor->world.pos.z - actor->prevPos.z) * 0.6f;
    }
    if ((tr & (1 << TR_REGEN)) && ((t->v2timer % (4 * FPS)) == 0) && (actor->colChkInfo.health < t->maxHp)) {
        actor->colChkInfo.health++;
    }
    if ((tr & (1 << TR_SUMMONER)) && ((t->v2timer % (12 * FPS)) == 0) && (Ev_CountTagAll(play, TAG_V2) < 6)) {
        static const u8 sMinions[] = { 0, 5, 8 };

        V2_SpawnEnemy(play, sMinions[V2_Rand(3)], NULL, 150.0f, 350.0f, TAG_V2);
    }
    if ((tr & (1 << TR_FIRE)) && (actor->xzDistToPlayer < 75.0f) && (fabsf(actor->playerHeightRel) < 80.0f) &&
        ((t->v2timer % 30) == 0)) {
        func_800B8D50(play, actor, 4.0f, actor->yawTowardsPlayer, 3.0f, 0x08);
    }
    if ((tr & (1 << TR_FIRE)) && ((t->v2timer % 4) == 0)) {
        Vec3f p = actor->world.pos;

        p.x += Rand_CenteredFloat(40.0f);
        p.z += Rand_CenteredFloat(40.0f);
        func_800B3030(play, &p, &gZeroVec3f, &gZeroVec3f, 50, 0, 2);
    }
    if ((tr & (1 << TR_BLINKER)) && ((t->v2timer % (8 * FPS)) == 0) && (actor->xzDistToPlayer > 320.0f)) {
        Vec3f spot;

        if (Ev_FindSpot(play, 150.0f, 250.0f, player->actor.shape.rot.y, 0x7FFF, false, &spot)) {
            Ev_Poof(play, &actor->world.pos, 1);
            spot.y += sPool[V2_PoolOfActor(actor) >= 0 ? V2_PoolOfActor(actor) : 0].yOffset;
            actor->world.pos = spot;
            actor->prevPos = spot;
            Ev_Poof(play, &spot, 1);
        }
    }
    if ((t->v2flags & TAGF_V2_ELITE) && ((t->v2timer % (10 * FPS)) == 0) && (actor->xzDistToPlayer < 900.0f)) {
        // Meteor Caller: a falling bomb near you (the ground marker shows where).
        Vec3f to = player->actor.world.pos;
        Vec3f from;

        to.x += Rand_CenteredFloat(160.0f);
        to.z += Rand_CenteredFloat(160.0f);
        from = to;
        from.y = Ev_SkyHeight(play, &to, 250.0f);
        Drop_Launch(play, &from, &to, 30);
    }
    if (tr & (1 << TR_CLOAKED)) {
        // 4 seconds seen, 3 seconds only visible through the Lens of Truth.
        if ((t->v2timer % (7 * FPS)) < (4 * FPS)) {
            actor->flags &= ~ACTOR_FLAG_REACT_TO_LENS;
        } else {
            actor->flags |= ACTOR_FLAG_REACT_TO_LENS;
        }
    }
}

// ---------------------------------------------------------------------------
// Moon Pouch
// ---------------------------------------------------------------------------

static void Pouch_Teleport(PlayState* play, Vec3f* to) {
    Player* player = GET_PLAYER(play);

    Ev_Poof(play, &player->actor.world.pos, 3);
    player->actor.world.pos = *to;
    player->actor.prevPos = *to;
    player->actor.velocity.y = 0.0f;
    Ev_Poof(play, to, 3);
    Audio_PlaySfx(NA_SE_EV_CHINCLE_SPELL_EFFECT);
}

static void Pouch_EnemyBomb(PlayState* play, Vec3f* pos) {
    Actor* actor =
        Ev_Spawn(play, ACTOR_EN_BOM, 0, pos, BOMB_EXPLOSIVE_TYPE_BOMB, 0, 0, BOMB_TYPE_BODY, TAG_ENEMYBOMB);

    if (actor != NULL) {
        ((EnBom*)actor)->timer = 1;
    }
}

static s32 Pouch_OwnedCount(void) {
    s32 k;
    s32 m = 0;

    for (k = 0; k < PI_COUNT; k++) {
        m += (sPouch[k] > 0);
    }
    return m;
}

static void Pouch_Use(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 item = sPouchSel;
    Actor* actor;

    if ((player == NULL) || (sPouch[item] == 0)) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return;
    }
    switch (item) {
        case PI_PORTAL:
            if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            // V3: the two stones can be in different rooms of the same area.
            if (sPortalCount >= 2) {
                sPortalCount = 0;
            }
            sPortal[sPortalCount] = player->actor.world.pos;
            sPortalRoom[sPortalCount] = play->roomCtx.curRoom.num;
            sPortalCount++;
            sPortalLock = true;
            if (sPortalCount == 2) {
                sPouch[PI_PORTAL]--;
                Menu_ShowToast("Portals linked!");
            } else {
                Menu_ShowToast("Blue portal placed. Place the second one.");
            }
            Audio_PlaySfx(NA_SE_EV_CHINCLE_SPELL_EFFECT);
            return;
        case PI_GIGA:
            sGigaArmed = !sGigaArmed;
            Menu_ShowToast(sGigaArmed ? "Giga Blade armed" : "Giga Blade lowered");
            break;
        case PI_GRAVITY:
            sGravPos = player->actor.world.pos;
            sGravPos.x += Math_SinS(player->actor.shape.rot.y) * 100.0f;
            sGravPos.z += Math_CosS(player->actor.shape.rot.y) * 100.0f;
            sGravTimer = 30;
            sPouch[item]--;
            break;
        case PI_HOURGLASS:
            sHourglassTimer = 8 * FPS;
            sPouch[item]--;
            Menu_ShowToast("Time stands still for them");
            break;
        case PI_TONIC:
            if (gSaveContext.save.saveInfo.playerData.health >= gSaveContext.save.saveInfo.playerData.healthCapacity) {
                Menu_ShowToast("You're at full health.");
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            Health_ChangeBy(play, 0x40);
            sPouch[item]--;
            Menu_ShowToast("Lunar Tonic: +4 hearts");
            break;
        case PI_SECONDWIND:
            Menu_ShowToast("Second Wind works by itself.");
            return;
        case PI_SHIELD:
            sShieldUp = !sShieldUp;
            Menu_ShowToast(sShieldUp ? "Moon Shield up" : "Moon Shield down");
            break;
        case PI_BRAND:
            if (sNem.pool >= 0) {
                Menu_ShowToast("You already have a Nemesis.");
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            sBrandArmed = !sBrandArmed;
            Menu_ShowToast(sBrandArmed ? "Brand ready: hurt a roaming enemy" : "Brand put away");
            break;
        case PI_SMOKE:
            sVeilTimer = 10 * FPS;
            sPouch[item]--;
            Ev_Poof(play, &player->actor.world.pos, 3);
            Menu_ShowToast("Smoke Veil: they lost you");
            break;
        case PI_STILL:
            if (sActive < 0) {
                Menu_ShowToast("No event is running.");
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            sPouch[item]--;
            Menu_ShowToast("Stillwater: the moon goes quiet.");
            Ev_Abort(play);
            break;
        case PI_DASH: {
            // Only along solid ground and never through walls, so it can't skip gaps or clip out of bounds.
            Vec3f from = player->actor.world.pos;
            Vec3f to = from;
            f32 reach = 0.0f;
            f32 step;

            if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || (player->stateFlags1 & PLAYER_STATE1_8000000)) {
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            f32 lastY = from.y;
            Vec3f prev = from;

            // V3: each step is checked from the one before (slopes are fine), and it reaches farther.
            for (step = 30.0f; step <= 300.0f; step += 30.0f) {
                Vec3f a = prev;
                Vec3f b;
                Vec3f hit;
                Vec3f probe;
                CollisionPoly* poly;
                s32 bgId;
                f32 floorY;

                a.y = lastY + 30.0f;
                b = from;
                b.y = lastY + 30.0f;
                b.x += Math_SinS(player->actor.shape.rot.y) * step;
                b.z += Math_CosS(player->actor.shape.rot.y) * step;
                if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, true, true, &bgId)) {
                    break; // a wall
                }
                probe = b;
                probe.y += 40.0f;
                floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - lastY) > 25.0f)) {
                    break; // a drop or a ledge
                }
                lastY = floorY;
                to = b;
                to.y = floorY;
                prev = to;
                reach = step;
            }
            if (reach < 60.0f) {
                Menu_ShowToast("No room to dash");
                Audio_PlaySfx(NA_SE_SY_ERROR);
                return;
            }
            Pouch_Teleport(play, &to);
            sPouch[item]--;
            return;
        }
        case PI_COMPASS:
            if (sNem.pool < 0) {
                Menu_ShowToast("You have no Nemesis");
                return;
            }
            sCompassTimer = 60 * FPS;
            sPouch[item]--;
            break;
    }
    sPouchFlash = 10;
    Audio_PlaySfx(NA_SE_SY_DECIDE);
}

static void Pouch_Update(PlayState* play, s32 playing) {
    Player* player = GET_PLAYER(play);
    Input* input = &play->state.input[0];

    if (!V2_ON(ID_V2_POUCH)) {
        // Turned off: nothing from the pouch keeps running.
        sPactTimer = sCompassTimer = sHourglassTimer = sVeilTimer = 0;
        if ((sDecoy != NULL) && V2_IsLive(play, sDecoy)) {
            Ev_KillOne(play, sDecoy);
        }
        sDecoy = NULL;
        sDecoyTimer = 0;
        return;
    }
    if (player == NULL) {
        return;
    }
    if (sPactTimer > 0) {
        sPactTimer--;
    }
    if (sCompassTimer > 0) {
        sCompassTimer--;
    }
    if (sHourglassTimer > 0) {
        sHourglassTimer--;
    }
    if ((sVeilTimer > 0) && playing) {
        sVeilTimer--;
    }
    if (sDecoy != NULL) {
        if (!V2_IsLive(play, sDecoy)) {
            sDecoy = NULL;
            sDecoyTimer = 0;
        } else if (playing && (--sDecoyTimer <= 0)) {
            Ev_Poof(play, &sDecoy->world.pos, 2);
            Ev_KillOne(play, sDecoy);
            sDecoy = NULL;
        }
    }
    if (sPouchFlash > 0) {
        sPouchFlash--;
    }
    if (sPouch[sPouchSel] == 0) {
        s32 k;

        for (k = 1; k < PI_COUNT; k++) {
            if (sPouch[(sPouchSel + k) % PI_COUNT] > 0) {
                sPouchSel = (sPouchSel + k) % PI_COUNT;
                break;
            }
        }
    }
    if (!playing || gMenuOpen) {
        return;
    }

    // L + D-pad or L + C buttons (same thing): left/right pick, down uses.
    // Neither does anything else while L is held.
    if (CHECK_BTN_ALL(input->cur.button, BTN_L)) {
        u16 dpad = BTN_DLEFT | BTN_DRIGHT | BTN_DUP | BTN_DDOWN | BTN_CLEFT | BTN_CRIGHT | BTN_CUP | BTN_CDOWN;

        if (input->press.button & (BTN_DLEFT | BTN_DRIGHT | BTN_DDOWN | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN)) {
            sLComboUsed = true;
        }
        if (input->press.button & (BTN_DLEFT | BTN_DRIGHT | BTN_CLEFT | BTN_CRIGHT)) {
            // Skip items you have none of (unless the pouch is empty: then browse).
            s32 dir = (input->press.button & (BTN_DLEFT | BTN_CLEFT)) ? -1 : 1;
            s32 k;
            s32 next = (sPouchSel + dir + PI_COUNT) % PI_COUNT;

            for (k = 1; k <= PI_COUNT; k++) {
                s32 idx = (sPouchSel + dir * k + PI_COUNT * 2) % PI_COUNT;

                if (sPouch[idx] > 0) {
                    next = idx;
                    break;
                }
            }
            sPouchSel = next;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        } else if (input->press.button & (BTN_DDOWN | BTN_CDOWN)) {
            Pouch_Use(play);
        }
        input->cur.button &= ~dpad;
        input->press.button &= ~dpad;
        input->rel.button &= ~dpad;
    }

    // A hop into another room: once that room has loaded, let go of the old one.
    if (sPortalRoomWait > 0) {
        sPortalRoomWait--;
        if ((play->roomCtx.status == 0) && (play->roomCtx.prevRoom.num >= 0)) {
            Room_FinishRoomChange(play, &play->roomCtx);
            sPortalRoomWait = 0;
        }
    }
    // Portals: step onto one to come out of the other.
    if ((sPortalCount == 2) && (sPortalRoomWait == 0)) {
        s32 k;
        s32 onAny = false;

        for (k = 0; k < 2; k++) {
            if ((sPortalRoom[k] == play->roomCtx.curRoom.num) &&
                (Ev_DistXZ(&player->actor.world.pos, &sPortal[k]) < 40.0f) &&
                (fabsf(player->actor.world.pos.y - sPortal[k].y) < 50.0f)) {
                onAny = true;
                if (!sPortalLock) {
                    s32 dest = 1 - k;

                    if ((sPortalRoom[dest] != play->roomCtx.curRoom.num) && (sPortalRoom[dest] >= 0)) {
                        // The other stone is in another room: load it the way a door does.
                        if ((play->roomCtx.status != 0) ||
                            !Room_RequestNewRoom(play, &play->roomCtx, sPortalRoom[dest])) {
                            break; // the game is busy loading; try again next frame
                        }
                        sPortalRoomWait = 3 * FPS;
                    }
                    Pouch_Teleport(play, &sPortal[dest]);
                    sPortalLock = true;
                }
                break;
            }
        }
        if (!onAny) {
            sPortalLock = false;
        }
    } else if (sPortalCount == 1) {
        if (Ev_DistXZ(&player->actor.world.pos, &sPortal[0]) > 40.0f) {
            sPortalLock = false;
        }
    }

    // Recall Feather keeps 10 seconds of where you stood.
    if (((play->gameplayFrames % 5) == 0) && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        if (sRecallRoom != play->roomCtx.curRoom.num) {
            sRecallCount = 0;
            sRecallRoom = play->roomCtx.curRoom.num;
        }
        sRecall[sRecallHead] = player->actor.world.pos;
        sRecallHead = (sRecallHead + 1) % RECALL_LEN;
        sRecallCount = MIN(sRecallCount + 1, RECALL_LEN);
    }

    // Gravity Bomb: pull, then boom.
    if (sGravTimer > 0) {
        Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

        while (actor != NULL) {
            if ((actor->update != NULL) && (Ev_DistXZ(&actor->world.pos, &sGravPos) < 320.0f)) {
                Math_StepToF(&actor->world.pos.x, sGravPos.x, 7.0f);
                Math_StepToF(&actor->world.pos.z, sGravPos.z, 7.0f);
            }
            actor = actor->next;
        }
        if (--sGravTimer == 0) {
            Pouch_EnemyBomb(play, &sGravPos);
        }
    }

}

// ---------------------------------------------------------------------------
// Mutator effects that run every frame
// ---------------------------------------------------------------------------

// Events where standing still (or being held) is the point: no falling rocks then.
static s32 V2_HoldStillEvent(void) {
    return Ev_IsOn(EV_REDLIGHT) || Ev_IsOn(EV_GLARE) || Ev_IsOn(EV_STILL) || Ev_IsOn(EV_FATE) || Ev_IsOn(EV_QUIZ);
}

static void Mut_Update(PlayState* play, s32 playing) {
    Player* player = GET_PLAYER(play);
    static const u8 sAmmoSlots[5] = { SLOT_BOW, SLOT_BOMB, SLOT_BOMBCHU, SLOT_DEKU_STICK, SLOT_DEKU_NUT };
    s32 i;

    if (!gOpt[ID_V2_MUT] || (player == NULL)) {
        sPrevMagic = -1;
        sAmmoReady = false;
        return;
    }
    // Mana Tax and Ammo Tax: whatever gets used, the same again.
    {
        s32 magic = gSaveContext.save.saveInfo.playerData.magic;

        if (Mut_Has(MUT_MANATAX) && (sPrevMagic >= 0) && (magic < sPrevMagic) && (sPrevMagic - magic <= 48)) {
            magic = MAX(magic - (sPrevMagic - magic), 0);
            gSaveContext.save.saveInfo.playerData.magic = magic;
        }
        sPrevMagic = magic;
    }
    for (i = 0; i < 5; i++) {
        s8* ammo = &gSaveContext.save.saveInfo.inventory.ammo[sAmmoSlots[i]];

        if (Mut_Has(MUT_AMMOTAX) && sAmmoReady && (*ammo < sPrevAmmo[i]) && (sPrevAmmo[i] - *ammo <= 5)) {
            *ammo = MAX((s32)*ammo - (sPrevAmmo[i] - *ammo), 0);
        }
        sPrevAmmo[i] = *ammo;
    }
    sAmmoReady = true;

    if (!playing) {
        return;
    }
    // Bleeding
    if ((sBleedAmount > 0) && (--sBleedTimer <= 0)) {
        s32 hp = gSaveContext.save.saveInfo.playerData.health;

        if (hp > 0x04) {
            // Stops at a quarter heart: bleeding alone never kills.
            gSaveContext.save.saveInfo.playerData.health = MAX(hp - sBleedAmount, 0x04);
            Audio_PlaySfx(NA_SE_SY_ERROR);
            if (player != NULL) {
                Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 200, COLORFILTER_BUFFLAG_OPA, 10);
            }
        }
        sBleedAmount = 0;
    }
    // Rupee Rot
    if (Mut_Has(MUT_RUPEEROT) && (--sRotTimer <= 0)) {
        sRotTimer = 20 * FPS;
        if (gSaveContext.save.saveInfo.playerData.rupees > 0) {
            Rupees_ChangeBy(-1);
        }
    }
    // Cold Water
    if (Mut_Has(MUT_COLDWATER) && (player->stateFlags1 & PLAYER_STATE1_8000000) &&
        (player->transformation != PLAYER_FORM_ZORA)) {
        if (++sColdTimer >= 2 * FPS) {
            s32 hp = gSaveContext.save.saveInfo.playerData.health;

            sColdTimer = 0;
            if (hp > 0x10) {
                gSaveContext.save.saveInfo.playerData.health = MAX(hp - 4, 0x10);
            }
        }
    }
    // Restless Earth (not while an event wants you standing still)
    if (Mut_Has(MUT_RESTLESS) && !Ev_InSafeZone(play) && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
        (fabsf(player->speedXZ) < 0.5f) && !V2_HoldStillEvent()) {
        if (++sStillFrames >= 4 * FPS) {
            Vec3f target = player->actor.world.pos;

            sStillFrames = 0;
            SkyRock_Launch(play, &target, 30, 0.3f);
        }
    } else {
        sStillFrames = 0;
    }
    // Moon Tears
    if (Mut_Has(MUT_TEARS) && !Ev_InSafeZone(play) && (--sTearsTimer <= 0)) {
        Vec3f target = player->actor.world.pos;

        sTearsTimer = (35 + V2_Rand(20)) * FPS;
        if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && !V2_HoldStillEvent()) {
            SkyRock_Launch(play, &target, 36, 0.3f);
        }
    }
    // Ambushers
    if (Mut_Has(MUT_AMBUSH) && !sAmbushDone && (sAreaTimer == 4 * FPS)) {
        sAmbushDone = true;
        if (!Ev_InSafeZone(play) && (Rand_ZeroOne() < 0.35f)) {
            s32 n = 2 + V2_Rand(2);
            s32 pick = Ev_PickPoolEnemy(false);

            for (i = 0; (i < n) && (pick >= 0); i++) {
                V2_SpawnEnemy(play, pick, NULL, 250.0f, 500.0f, TAG_V2);
            }
        }
    }
    // Keese Escort
    if (Mut_Has(MUT_ESCORT) && !Ev_InSafeZone(play) && (sAreaTimer > 3 * FPS)) {
        if (--sEscortTimer <= 0) {
            sEscortTimer = 20 * FPS;
            if (Ev_CountTagged(play, ACTORCAT_ENEMY, TAG_V2) < 3) {
                V2_SpawnEnemy(play, 0, NULL, 200.0f, 400.0f, TAG_V2);
            }
        }
    }
}

// Dark Nights: darker nights while no event has its own look.
static void Mut_NightLook(PlayState* play) {
    s32 want = gOpt[ID_V2_MUT] && Mut_Has(MUT_DARKNIGHTS) && Ev_IsNight() && (sActive < 0);

    if (want) {
        Ev_ApplyEnv(play, ENV_DARK, 0.5f);
        sNightDark = true;
    } else if (sNightDark) {
        sNightDark = false;
        if (sActive < 0) {
            Ev_ClearEnv(play);
            Ev_RestoreLighting(play);
        }
    }
}

// ---------------------------------------------------------------------------
// Save code (in-game text box)
// ---------------------------------------------------------------------------

static const char sB32[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

typedef struct {
    u8 bytes[64];
    s32 bit;
} BitBuf;

static void Bits_Put(BitBuf* b, u32 value, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        if ((b->bit >> 3) < 64) {
            if (value & (1u << i)) {
                b->bytes[b->bit >> 3] |= (1 << (b->bit & 7));
            }
        }
        b->bit++;
    }
}

static u32 Bits_Get(BitBuf* b, s32 count) {
    u32 value = 0;
    s32 i;

    for (i = 0; i < count; i++) {
        if (((b->bit >> 3) < 64) && (b->bytes[b->bit >> 3] & (1 << (b->bit & 7)))) {
            value |= (1u << i);
        }
        b->bit++;
    }
    return value;
}

#define SAVE_BYTES 31    // format 3 (2.6): 50 characters
#define SAVE_BYTES_V2 27 // format 2 (2.0-2.5): 44 characters
#define SAVE_CHARS(bytes) (((bytes) * 8 + 4) / 5)

static u8 Save_Sum(u8* bytes, s32 n) {
    u32 sum = 0x5A;
    s32 i;

    for (i = 0; i < n; i++) {
        sum = (sum * 31 + bytes[i] + i) & 0xFFFF;
    }
    return (u8)(sum ^ (sum >> 8));
}

#define SAVE_BYTES4 41 // format 4 (V3): 66 characters, auto-save only

static void Save_Make(char* out) {
    BitBuf b;
    s32 i;
    s32 n = 0;

    bzero(&b, sizeof(b));
    Bits_Put(&b, 4, 4); // format version
    Bits_Put(&b, sMarks, 16);
    for (i = 0; i < 12; i++) {
        Bits_Put(&b, (i < PI_COUNT) ? MIN(sPouch[i], 127) : 0, 7);
    }
    Bits_Put(&b, sMutMask, 22);
    Bits_Put(&b, sMutCount, 3);
    for (i = 0; i < 6; i++) {
        Bits_Put(&b, sMutOrder[i], 5);
    }
    Bits_Put(&b, sDraftDone, 6);
    Bits_Put(&b, (sNem.pool < 0) ? 63 : sNem.pool, 6);
    Bits_Put(&b, sNem.nameA, 5);
    Bits_Put(&b, sNem.nameB, 4);
    Bits_Put(&b, sNem.level, 8);
    Bits_Put(&b, sNem.traitCount, 3);
    for (i = 0; i < 6; i++) {
        Bits_Put(&b, sNem.traitOrder[i], 4);
    }
    Bits_Put(&b, (sNem.zone < 0) ? 0 : sNem.zone, 7);
    Bits_Put(&b, MIN(sNem.wins, 63), 6);
    Bits_Put(&b, MIN(sBountiesDone, 255), 8);
    Bits_Put(&b, MIN(sNemSlain, 255), 8);
    for (i = 0; i < NS_COUNT; i++) {
        Bits_Put(&b, sNemStat[i], 6);
    }
    Bits_Put(&b, sNemInherit, 8);
    Bits_Put(&b, sNem.style, 1);
    Bits_Put(&b, sNem.scarred, 1);
    Bits_Put(&b, MIN(sNem.hpPct, 100), 7);
    Bits_Put(&b, MIN(sNem.dormant, 1023), 10);
    Bits_Put(&b, MIN(sNemStolen, 1023), 10);
    b.bytes[SAVE_BYTES4 - 1] = Save_Sum(b.bytes, SAVE_BYTES4 - 1);

    b.bit = 0;
    for (i = 0; i < SAVE_CHARS(SAVE_BYTES4); i++) {
        out[n++] = sB32[Bits_Get(&b, 5)];
    }
    out[n] = '\0';
}

static s32 Save_Load4(BitBuf* bp) {
    BitBuf b = *bp;
    s32 i;
    s32 t;
    Nemesis nem;
    u8 stats[NS_COUNT];

    b.bit = 4;
    sMarks = Bits_Get(&b, 16);
    for (i = 0; i < 12; i++) {
        t = Bits_Get(&b, 7);
        if (i < PI_COUNT) {
            sPouch[i] = MIN(t, sPouchMax[i]);
        }
    }
    sMutMask = Bits_Get(&b, 22);
    t = Bits_Get(&b, 3);
    sMutCount = MIN(t, 6);
    for (i = 0; i < 6; i++) {
        sMutOrder[i] = Bits_Get(&b, 5) % MUT_COUNT;
    }
    sDraftDone = Bits_Get(&b, 6);
    bzero(&nem, sizeof(nem));
    nem.pool = Bits_Get(&b, 6);
    nem.nameA = Bits_Get(&b, 5);
    nem.nameB = Bits_Get(&b, 4);
    nem.level = Bits_Get(&b, 8);
    t = Bits_Get(&b, 3);
    nem.traitCount = MIN(t, 6);
    for (i = 0; i < 6; i++) {
        nem.traitOrder[i] = Bits_Get(&b, 4) % TR_COUNT;
    }
    t = Bits_Get(&b, 7);
    nem.zone = t % ZONE_COUNT;
    nem.wins = Bits_Get(&b, 6);
    sBountiesDone = Bits_Get(&b, 8);
    sNemSlain = Bits_Get(&b, 8);
    for (i = 0; i < NS_COUNT; i++) {
        t = Bits_Get(&b, 6);
        stats[i] = MIN(t, NEM_STAT_MAX);
    }
    t = Bits_Get(&b, 8);
    sNemInherit = MIN(t, NEM_LEVEL_MAX);
    nem.style = Bits_Get(&b, 1);
    nem.scarred = Bits_Get(&b, 1);
    t = Bits_Get(&b, 7);
    nem.hpPct = CLAMP(t, 1, 100);
    nem.dormant = Bits_Get(&b, 10);
    sNemStolen = Bits_Get(&b, 10);
    nem.level = CLAMP(nem.level, 1, NEM_LEVEL_MAX);
    if ((nem.pool == 63) || (nem.pool >= POOL_FIRST_BOSS) || (nem.pool == 18)) {
        nem.pool = -1;
    }
    nem.spawnedVisit = true;
    nem.migrate = Nem_MigrateFrames(false);
    if ((nem.pool >= 0) && (nem.pool == sNem.pool) && (nem.nameA == sNem.nameA) && (nem.nameB == sNem.nameB)) {
        nem.actor = sNem.actor;
        nem.engaged = sNem.engaged;
    } else {
        sNemOrphan = sNem.actor;
        Nem_ClearFriendStuff();
    }
    sNem = nem;
    Nem_ResetStats();
    if (sNem.pool >= 0) {
        for (i = 0; i < NS_COUNT; i++) {
            sNemStat[i] = stats[i];
        }
    }
    {
        // Retired mutators are dropped.
        s32 kept = 0;
        s32 k;

        for (k = 0; k < sMutCount; k++) {
            if (!Mut_Retired(sMutOrder[k])) {
                sMutOrder[kept++] = sMutOrder[k];
            }
        }
        sMutCount = kept;
        for (k = 0; k < MUT_COUNT; k++) {
            if (Mut_Retired(k)) {
                sMutMask &= ~(1u << k);
            }
        }
    }
    return true;
}

static s32 Save_Load(const char* text) {
    BitBuf b;
    s32 i;
    s32 count = 0;
    s32 ver;
    s32 items;
    Nemesis nem;
    u8 stats[NS_COUNT];
    s32 inherit = 0;
    s32 t;

    bzero(&b, sizeof(b));
    for (i = 0; (text[i] != '\0') && (count < SAVE_CHARS(SAVE_BYTES4)); i++) {
        char c = text[i];
        s32 v = -1;
        s32 k;

        if ((c >= 'a') && (c <= 'z')) {
            c -= 32;
        }
        for (k = 0; k < 32; k++) {
            if (sB32[k] == c) {
                v = k;
            }
        }
        if (v >= 0) {
            Bits_Put(&b, v, 5);
            count++;
        }
    }
    // V3 (format 4), a 2.6 code, or an older (shorter) one.
    b.bit = 0;
    if ((count >= SAVE_CHARS(SAVE_BYTES4)) && (Save_Sum(b.bytes, SAVE_BYTES4 - 1) == b.bytes[SAVE_BYTES4 - 1]) &&
        (Bits_Get(&b, 4) == 4)) {
        return Save_Load4(&b);
    }
    b.bit = 0;
    if ((count >= SAVE_CHARS(SAVE_BYTES)) && (count < SAVE_CHARS(SAVE_BYTES4)) && (Save_Sum(b.bytes, SAVE_BYTES - 1) == b.bytes[SAVE_BYTES - 1]) &&
        (Bits_Get(&b, 4) == 3)) {
        ver = 3;
        items = PI_COUNT;
    } else if ((count >= SAVE_CHARS(SAVE_BYTES_V2)) && (count < SAVE_CHARS(SAVE_BYTES)) &&
               (Save_Sum(b.bytes, SAVE_BYTES_V2 - 1) == b.bytes[SAVE_BYTES_V2 - 1]) &&
               ((b.bit = 0), (Bits_Get(&b, 4) == 2))) {
        ver = 2;
        items = 9; // pouch items before 2.6
    } else {
        return false;
    }
    sMarks = Bits_Get(&b, 16);
    for (i = 0; i < PI_COUNT; i++) {
        sPouch[i] = (i < items) ? Bits_Get(&b, 5) : 0;
    }
    // Items retired in V3 (their slots hold new items now) are dropped.
    sPouch[PI_TONIC] = sPouch[PI_SECONDWIND] = sPouch[PI_BRAND] = sPouch[PI_STILL] = 0;
    sMutMask = Bits_Get(&b, MUT_COUNT);
    // (MIN evaluates its arguments twice: read each field into a variable first.)
    t = Bits_Get(&b, 3);
    sMutCount = MIN(t, 6);
    for (i = 0; i < 6; i++) {
        sMutOrder[i] = Bits_Get(&b, 5) % MUT_COUNT;
    }
    if (ver == 2) {
        // Slots 4-7 were the event mutators (now curses only): drop them.
        s32 k;
        s32 kept = 0;

        for (k = 0; k < sMutCount; k++) {
            if ((sMutOrder[k] < 4) || (sMutOrder[k] > 7)) {
                sMutOrder[kept++] = sMutOrder[k];
            }
        }
        sMutCount = kept;
        sMutMask &= ~0xF0u;
    }
    sMutMask &= ~(1u << MUT_RETIRED);
    sDraftDone = Bits_Get(&b, 6);
    bzero(&nem, sizeof(nem));
    nem.pool = Bits_Get(&b, 6);
    nem.nameA = Bits_Get(&b, 5);
    nem.nameB = Bits_Get(&b, 4);
    nem.level = Bits_Get(&b, 4);
    t = Bits_Get(&b, 3);
    nem.traitCount = MIN(t, 6);
    for (i = 0; i < 6; i++) {
        nem.traitOrder[i] = Bits_Get(&b, 4) % TR_COUNT;
    }
    {
        // 2.x areas to the V3 map.
        static const u8 sOldZone[16] = { 0, 5, 6, 7, 9, 17, 11, 12, 14, 16, 19, 20, 23, 25, 24, 26 };

        nem.zone = sOldZone[Bits_Get(&b, 4) & 15];
    }
    nem.style = V2_Rand(2);
    nem.hpPct = 100;
    nem.wins = Bits_Get(&b, 6);
    sBountiesDone = Bits_Get(&b, 8);
    sNemSlain = Bits_Get(&b, 8);
    bzero(stats, sizeof(stats));
    if (ver >= 3) {
        for (i = 0; i < NS_COUNT; i++) {
            stats[i] = Bits_Get(&b, 3);
        }
        inherit = Bits_Get(&b, 4);
        nem.level |= Bits_Get(&b, 1) << 4;
        inherit |= Bits_Get(&b, 1) << 4;
        inherit = MIN(inherit, NEM_LEVEL_MAX);
        for (i = 0; i < NS_COUNT; i++) {
            t = stats[i] | (Bits_Get(&b, 1) << 3);
            stats[i] = MIN(t, NEM_STAT_MAX);
        }
    }
    nem.level = CLAMP(nem.level, 1, (nem.pool < 63) ? Nem_TypeCap(nem.pool % 64) : NEM_LEVEL_MAX);
    if ((nem.pool == 63) || (nem.pool >= POOL_FIRST_BOSS) || (nem.pool == 18)) {
        nem.pool = -1;
    }
    nem.spawnedVisit = true;
    if ((nem.pool >= 0) && (nem.pool == sNem.pool) && (nem.nameA == sNem.nameA) && (nem.nameB == sNem.nameB)) {
        // Same Nemesis: keep the one that's already out and your friend's changes.
        nem.actor = sNem.actor;
        nem.engaged = sNem.engaged;
    } else {
        sNemOrphan = sNem.actor; // the old one gets removed once we're back in play
        Nem_ClearFriendStuff();
    }
    sNem = nem;
    Nem_ResetStats();
    if (sNem.pool >= 0) {
        for (i = 0; i < NS_COUNT; i++) {
            sNemStat[i] = stats[i];
        }
        // (points above what its level allows are dropped)
        while ((Nem_PointsFree() == 0) && (sNemStat[0] + sNemStat[1] + sNemStat[2] + sNemStat[3] > sNem.level)) {
            s32 k;

            for (k = NS_COUNT - 1; k >= 0; k--) {
                if (sNemStat[k] > 0) {
                    sNemStat[k]--;
                    break;
                }
            }
        }
    }
    sNem.migrate = Nem_MigrateFrames(false);
    sNemInherit = inherit;
    return true;
}

// ---------------------------------------------------------------------------
// Auto-save: your Moon progress (the same data as the save code) is kept for
// each game file by CUSTOM_GlobalEventsRemote.dll, in CUSTOM_GlobalEventsSaves.txt next to it.
// It's written every few seconds when something changed and read back when you
// load the file. Game files are told apart by slot and player name.
// ---------------------------------------------------------------------------

RECOMP_IMPORT(".", void ger_store(const char* key, const char* value));
RECOMP_IMPORT(".", s32 ger_fetch(const char* key, char* out, u32 max));

static char sAutoKey[24] = "";
static char sAutoLast[100] = "";
static s32 sAutoTimer = 0;

static void Auto_MakeKey(void) {
    u32 hash = 2166136261u;
    s32 i;
    s32 n;

    for (i = 0; i < 8; i++) {
        hash = (hash ^ (u8)gSaveContext.save.saveInfo.playerData.playerName[i]) * 16777619u;
    }
    n = Ev_Append(sAutoKey, 0, "file", sizeof(sAutoKey));
    n = Ev_AppendNum(sAutoKey, n, (s32)gSaveContext.fileNum, sizeof(sAutoKey));
    n = Ev_Append(sAutoKey, n, "_", sizeof(sAutoKey));
    for (i = 7; i >= 0; i--) {
        if (n < (s32)sizeof(sAutoKey) - 1) {
            sAutoKey[n++] = "0123456789abcdef"[(hash >> (4 * i)) & 0xF];
        }
    }
    sAutoKey[n] = '\0';
}

// A fresh start: nothing earned yet.
static void V2_ResetProgress(void) {
    sMarks = 0;
    bzero(sPouch, sizeof(sPouch));
    sGigaArmed = sShieldUp = false;
    Mut_ClearAll();
    sMutMask = 0;
    bzero(&sNem, sizeof(sNem));
    sNem.pool = -1;
    Nem_ClearFriendStuff();
    Nem_ResetStats();
    sNemInherit = 0;
    sBountiesDone = 0;
    sNemSlain = 0;
}

static s32 Str_Eq(const char* a, const char* b);

// Paid-for charges and favors belong to the file that bought them.
static void Auto_ClearSessionState(void) {
    sWardCharges = sMercyCharges = sLuckyCharges = sFriendShields = 0;
    sHunterSight = sNemLure = sBountyBait = false;
    sBanMask = 0;
    sGigaArmed = sShieldUp = false;
    sPactTimer = sCompassTimer = sHourglassTimer = sVeilTimer = 0;
}

// Saves now if anything changed (also used when leaving a scene, so the last
// purchase before an owl save or quitting isn't lost).
static void Auto_Flush(void) {
    char code[100];
    s32 i;

    if (!gOpt[ID_V2_AUTOSAVE] || (sAutoKey[0] == '\0')) {
        return;
    }
    Save_Make(code);
    for (i = 0; (code[i] != '\0') && (code[i] == sAutoLast[i]); i++) {
    }
    if ((code[i] == '\0') && (sAutoLast[i] == '\0')) {
        return; // unchanged
    }
    ger_store(sAutoKey, code);
    Ev_Append(sAutoLast, 0, code, sizeof(sAutoLast));
}

// Right after a game file is loaded (or a new one is made).
static void Auto_OnFileLoaded(s32 newFile) {
    char code[100];
    char prevKey[24];
    s32 got;

    Ev_Append(prevKey, 0, sAutoKey, sizeof(prevKey));
    Auto_MakeKey();
    sAutoLast[0] = '\0';
    sAutoTimer = 0;
    sNemOrphan = NULL; // nothing from before the load is still out there
    if (!gOpt[ID_V2_AUTOSAVE]) {
        sAutoKey[0] = '\0'; // (turning it back on waits for the next file load)
        return;
    }
    if (newFile || (prevKey[0] == '\0') || !Str_Eq(prevKey, sAutoKey)) {
        Auto_ClearSessionState();
    }
    sLastSlotSeen = -1; // the time of day of the file played before doesn't apply to this one
    if (newFile) {
        // A new file starts clean, and that's saved right away so an old file with the
        // same slot and name can't hand its progress to it.
        V2_ResetProgress();
        Save_Make(code);
        ger_store(sAutoKey, code);
        Ev_Append(sAutoLast, 0, code, sizeof(sAutoLast));
        return;
    }
    got = ger_fetch(sAutoKey, code, sizeof(code));
    if ((got > 0) && Save_Load(code)) {
        sNem.actor = NULL;
        sNemOrphan = NULL;
        Ev_Append(sAutoLast, 0, code, sizeof(sAutoLast));
        Menu_ShowToast("Moon progress loaded.");
    } else if (got == 0) {
        // Nothing saved for this file yet (or it came from before 2.9.1).
        V2_ResetProgress();
        Menu_ShowToast("No Moon progress saved for this file yet.");
    } else {
        // The save file couldn't be read, or what's there isn't valid: don't touch it.
        sAutoKey[0] = '\0';
        Menu_ShowToast("Couldn't read Moon progress. Auto-Save paused.");
    }
}

// Every frame while playing: save when something changed (checked every 5 seconds).
static void Auto_Tick(void) {
    if (++sAutoTimer < 5 * FPS) {
        return;
    }
    sAutoTimer = 0;
    Auto_Flush();
}

// The code box itself (the mod loader's UI).
static RecompuiContext sSaveCtx;
static RecompuiResource sSaveInput;
static s32 sSaveUiMade = false;
static volatile s32 sSaveUiOpen = false;
static volatile s32 sSaveClickLoad = false;
static volatile s32 sSaveClickClose = false;
static s32 sSaveUiWanted = false;

static void SaveUi_OnLoad(RecompuiResource res, const RecompuiEventData* e, void* user) {
    if (e->type == UI_EVENT_CLICK) {
        sSaveClickLoad = true;
    }
}

static void SaveUi_OnClose(RecompuiResource res, const RecompuiEventData* e, void* user) {
    if (e->type == UI_EVENT_CLICK) {
        sSaveClickClose = true;
    }
}

static void SaveUi_Make(void) {
    static const RecompuiColor sBg = { 12, 10, 28, 235 };
    static const RecompuiColor sFg = { 230, 225, 255, 255 };
    RecompuiResource root;
    RecompuiResource panel;
    RecompuiResource label;
    RecompuiResource load;
    RecompuiResource close;

    sSaveCtx = recompui_create_context();
    recompui_open_context(sSaveCtx);
    root = recompui_context_root(sSaveCtx);
    panel = recompui_create_element(sSaveCtx, root);
    recompui_set_position(panel, POSITION_ABSOLUTE);
    recompui_set_left(panel, 20.0f, UNIT_PERCENT);
    recompui_set_top(panel, 30.0f, UNIT_PERCENT);
    recompui_set_width(panel, 60.0f, UNIT_PERCENT);
    recompui_set_padding(panel, 24.0f, UNIT_DP);
    recompui_set_border_radius(panel, 12.0f, UNIT_DP);
    recompui_set_background_color(panel, &sBg);
    label = recompui_create_label(sSaveCtx, panel,
                                  "Global Events save code. Select it and copy it (Ctrl+C) to keep your Moon Marks, "
                                  "Moon Pouch, Nemesis and mutators. To restore, paste a code here and press Load.",
                                  LABELSTYLE_NORMAL);
    recompui_set_color(label, &sFg);
    sSaveInput = recompui_create_textinput(sSaveCtx, panel);
    recompui_set_width(sSaveInput, 100.0f, UNIT_PERCENT);
    load = recompui_create_button(sSaveCtx, panel, "Load this code", BUTTONSTYLE_PRIMARY);
    close = recompui_create_button(sSaveCtx, panel, "Close", BUTTONSTYLE_SECONDARY);
    recompui_register_callback(load, SaveUi_OnLoad, NULL);
    recompui_register_callback(close, SaveUi_OnClose, NULL);
    recompui_close_context(sSaveCtx);
    sSaveUiMade = true;
}

static void SaveUi_Open(PlayState* play) {
    char code[100];

    if (!sSaveUiMade) {
        SaveUi_Make();
    }
    Save_Make(code);
    recompui_open_context(sSaveCtx);
    recompui_set_input_text(sSaveInput, code);
    recompui_close_context(sSaveCtx);
    recompui_set_context_captures_input(sSaveCtx, 1);
    recompui_set_context_captures_mouse(sSaveCtx, 1);
    recompui_show_context(sSaveCtx);
    sSaveUiOpen = true;
    sSaveClickLoad = sSaveClickClose = false;
    Player_SetCsActionWithHaltedActors(play, NULL, PLAYER_CSACTION_WAIT);
}

static void SaveUi_Close(PlayState* play) {
    recompui_hide_context(sSaveCtx);
    sSaveUiOpen = false;
    Player_SetCsAction(play, NULL, PLAYER_CSACTION_END);
}

static void SaveUi_Update(PlayState* play) {
    if (sSaveUiWanted && !gMenuOpen) {
        sSaveUiWanted = false;
        SaveUi_Open(play);
    }
    if (!sSaveUiOpen) {
        return;
    }
    if (sSaveClickLoad) {
        char* text;

        sSaveClickLoad = false;
        recompui_open_context(sSaveCtx);
        text = recompui_get_input_text(sSaveInput);
        recompui_close_context(sSaveCtx);
        if ((text != NULL) && Save_Load(text)) {
            if ((sNemOrphan != NULL) && V2_IsLive(play, sNemOrphan)) {
                Ev_Poof(play, &sNemOrphan->world.pos, 1);
                Ev_KillOne(play, sNemOrphan);
            }
            sNemOrphan = NULL;
            Menu_ShowToast("Save code loaded!");
            Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
            SaveUi_Close(play);
        } else {
            Menu_ShowToast("That code didn't work.");
            Audio_PlaySfx(NA_SE_SY_ERROR);
        }
        if (text != NULL) {
            recomp_free(text);
        }
    }
    // If the box ever fails to take input, B in game closes it.
    if (CHECK_BTN_ANY(play->state.input[0].press.button, BTN_B | BTN_START)) {
        sSaveClickClose = true;
    }
    if (sSaveClickClose && sSaveUiOpen) {
        sSaveClickClose = false;
        SaveUi_Close(play);
    }
}

// ---------------------------------------------------------------------------
// The Bounty Board: a second rainbow carpenter in South Clock Town. Walk up
// and press A to take bounties and trade Moon Marks.
// ---------------------------------------------------------------------------

#define BOARD_RANGE 90.0f

static Actor* sBoard = NULL;

// V3: no more Bounty Board carpenter. Stand by any owl statue (they all glow)
// or the rainbow carpenter and tap L to open the Moon Menu, where you can trade.
// (Holding L still works the pouch; a tap that wasn't a pouch combo opens it.)
static s32 sMenuSpotIsOwl = false;
static Actor* Board_NearestOwl(PlayState* play, Player* player) {
    static const u8 sCats[] = { ACTORCAT_ITEMACTION, ACTORCAT_PROP, ACTORCAT_BG, ACTORCAT_NPC, ACTORCAT_MISC };
    Actor* best = NULL;
    s32 c;

    for (c = 0; c < ARRAY_COUNT(sCats); c++) {
        Actor* a = play->actorCtx.actorLists[sCats[c]].first;

        while (a != NULL) {
            if ((a->update != NULL) && (a->id == ACTOR_OBJ_WARPSTONE) && (a->xzDistToPlayer < 130.0f) &&
                (fabsf(a->playerHeightRel) < 90.0f) && ((best == NULL) || (a->xzDistToPlayer < best->xzDistToPlayer))) {
                best = a;
            }
            a = a->next;
        }
    }
    return best;
}

static void Board_Update(PlayState* play, Player* player) {
    Input* input = &play->state.input[0];
    Actor* carp = Carpenter_RainbowActor();

    sMenuSpot = Board_NearestOwl(play, player);
    sMenuSpotIsOwl = (sMenuSpot != NULL);
    if ((sMenuSpot == NULL) && (carp != NULL) && (carp->xzDistToPlayer < 110.0f) &&
        (fabsf(carp->playerHeightRel) < 80.0f)) {
        sMenuSpot = carp;
    }
    if (CHECK_BTN_ALL(input->press.button, BTN_L)) {
        sLComboUsed = false;
    }
    if (CHECK_BTN_ALL(input->cur.button, BTN_L) &&
        (input->press.button & (BTN_DLEFT | BTN_DRIGHT | BTN_DDOWN | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN))) {
        sLComboUsed = true;
    }
    if ((sMenuSpot != NULL) && CHECK_BTN_ALL(input->rel.button, BTN_L) && !sLComboUsed && !gMenuOpen &&
        (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        Menu_OpenV2(play, sMenuSpotIsOwl ? 2 : 0);
        gMenuAtBoard = true;
        Audio_PlaySfx(NA_SE_SY_WIN_OPEN);
    }
}

// ---------------------------------------------------------------------------
// Main update (every frame, from Events_Update)
// ---------------------------------------------------------------------------

static void V2_UpdateInner(PlayState* play, s32 playing, s32 prevHp);

static void V2_Update(PlayState* play, s32 playing) {
    Player* player = GET_PLAYER(play);
    s32 prevHp = sV2PrevHp;
    s32 hp = gSaveContext.save.saveInfo.playerData.health;

    // A health change the hook didn't see: react here. Losses only count when
    // Link was really hit (he's flashing), so drains and our own changes don't.
    if (!sHealthHookSeen && playing && (prevHp > 0) && (hp > 0 || prevHp > 0) && (hp != prevHp) &&
        (player != NULL) && ((hp > prevHp) || (player->invincibilityTimer != 0))) {
        Health_React(prevHp);
    }
    sHealthHookSeen = false;
    // Heart Lock and Open Wounds hold healing back (never a revive from zero).
    // (Not gated on "playing": drinking a potion is a cutscene, and the refill happens during it.
    // A fairy revive is let through: Link is flagged dead until it's done.)
    if ((prevHp > 0) && (player != NULL) && !(player->stateFlags1 & PLAYER_STATE1_DEAD)) {
        s32 now = gSaveContext.save.saveInfo.playerData.health;
        s32 cap = gSaveContext.save.saveInfo.playerData.healthCapacity;

        if (now < prevHp) {
            sWoundTimer = 10 * FPS;
        } else if ((now > prevHp) && gOpt[ID_V2_MUT]) {
            s32 keep = now;

            if (Mut_Has(MUT_WOUNDS) && (sWoundTimer > 0)) {
                keep = prevHp;
            }
            if (Mut_Has(MUT_HEARTLOCK) && (keep > MAX(cap - 0x20, 0x20))) {
                keep = MAX(prevHp, MAX(cap - 0x20, 0x20));
            }
            if (keep != now) {
                gSaveContext.save.saveInfo.playerData.health = keep;
                gSaveContext.healthAccumulator = 0;
            }
        }
    }
    if ((sWoundTimer > 0) && playing) {
        sWoundTimer--;
    }
    V2_UpdateInner(play, playing, prevHp);
    sV2PrevHp = gSaveContext.save.saveInfo.playerData.health;
    Link_Update(play, playing);
}

// ===========================================================================
// V3 World: bounties and the Nemesis have a real place in the area you're in.
// They're already out there when you arrive (far off, never by an exit), walk
// out when they leave, come in through an entrance when they arrive, and the
// Nemesis either charges you or shadows you first, depending on its nature.
// ===========================================================================

#define WORLD_EXIT_CLEAR 400.0f // never placed this close to an entrance or door
#define WORLD_MAX_POINTS 48

static Vec3f sEntrances[WORLD_MAX_POINTS]; // spawn points (they sit right inside the loading zones) and doors
static s16 sEntranceYaw[WORLD_MAX_POINTS];
static s32 sEntranceCount = 0;
static ActorEntry* sSpawnListBase = NULL;
static s32 sSpawnListCount = 0;

RECOMP_HOOK("Scene_CommandSpawnList") void World_BeforeSpawnList(PlayState* play, SceneCmd* cmd) {
    sSpawnListBase = (ActorEntry*)Lib_SegmentedToVirtual(cmd->spawnList.segment);
    sSpawnListCount = cmd->spawnList.data1;
}

// Gathers the area's entrances and doors (once per room).
static void World_GatherEntrances(PlayState* play) {
    s32 i;

    sEntranceCount = 0;
    if ((sSpawnListBase != NULL) && (sSpawnListCount > 0) && (sSpawnListCount < 64)) {
        for (i = 0; (i < sSpawnListCount) && (sEntranceCount < WORLD_MAX_POINTS); i++) {
            ActorEntry* e = &sSpawnListBase[i];

            sEntrances[sEntranceCount].x = e->pos.x;
            sEntrances[sEntranceCount].y = e->pos.y;
            sEntrances[sEntranceCount].z = e->pos.z;
            sEntranceYaw[sEntranceCount] = e->rot.y;
            sEntranceCount++;
        }
    }
    if (play->transitionActors.list != NULL) {
        for (i = 0; (i < play->transitionActors.count) && (sEntranceCount < WORLD_MAX_POINTS); i++) {
            TransitionActorEntry* d = &play->transitionActors.list[i];

            sEntrances[sEntranceCount].x = d->pos.x;
            sEntrances[sEntranceCount].y = d->pos.y;
            sEntrances[sEntranceCount].z = d->pos.z;
            sEntranceYaw[sEntranceCount] = d->rotY;
            sEntranceCount++;
        }
    }
}

static f32 World_ExitDist(Vec3f* pos) {
    f32 best = 1.0e9f;
    s32 i;

    for (i = 0; i < sEntranceCount; i++) {
        f32 d = Math_Vec3f_DistXYZ(pos, &sEntrances[i]);

        if (d < best) {
            best = d;
        }
    }
    return best;
}

static s32 World_FloorAt(PlayState* play, Vec3f* pos, f32 nearY) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe = *pos;
    f32 y;
    f32 waterY;
    WaterBox* waterBox;

    probe.y = nearY + 150.0f;
    y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if ((y <= BGCHECK_Y_MIN) || (fabsf(y - nearY) > 300.0f)) {
        return false;
    }
    if (WaterBox_GetSurface1(play, &play->colCtx, pos->x, pos->z, &waterY, &waterBox) && (waterY > y + 10.0f)) {
        return false;
    }
    pos->y = y;
    return true;
}

// A home spot: as far from you as the area allows, never near an exit. Tries
// the places the game itself puts actors first (they're on solid ground).
static s32 World_PickHome(PlayState* play, Vec3f* out) {
    Player* player = GET_PLAYER(play);
    f32 bestScore = -1.0f;
    Vec3f best;
    s32 i;
    s32 tries;

    for (i = 0; (play->setupActorList != NULL) && (i < play->numSetupActors); i++) {
        ActorEntry* e = &play->setupActorList[i];
        Vec3f p;
        f32 d;
        f32 score;

        p.x = e->pos.x;
        p.y = e->pos.y;
        p.z = e->pos.z;
        d = Ev_DistXZ(&p, &player->actor.world.pos);
        if ((d < 450.0f) || (World_ExitDist(&p) < WORLD_EXIT_CLEAR) || !World_FloorAt(play, &p, p.y)) {
            continue;
        }
        score = MIN(d, 2500.0f) + Rand_ZeroOne() * 600.0f; // far, with some variety
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    for (tries = 0; tries < 12; tries++) {
        Vec3f p;

        if (Ev_FindSpot(play, 600.0f, 2000.0f, (s16)(Rand_ZeroOne() * 0xFFFF), 0x7FFF, false, &p) &&
            (World_ExitDist(&p) >= WORLD_EXIT_CLEAR)) {
            f32 score = MIN(Ev_DistXZ(&p, &player->actor.world.pos), 2500.0f) + Rand_ZeroOne() * 600.0f;

            if (score > bestScore) {
                bestScore = score;
                best = p;
            }
        }
    }
    if (bestScore < 0.0f) {
        // A small area: anywhere that isn't by the door.
        for (tries = 0; tries < 16; tries++) {
            Vec3f p;

            if (Ev_FindSpot(play, 250.0f, 900.0f, (s16)(Rand_ZeroOne() * 0xFFFF), 0x7FFF, false, &p) &&
                (World_ExitDist(&p) >= 250.0f)) {
                *out = p;
                return true;
            }
        }
        return false;
    }
    *out = best;
    return true;
}

// An entrance far from you, a few steps inside it.
static s32 World_PickEntrance(PlayState* play, Vec3f* out) {
    Player* player = GET_PLAYER(play);
    f32 bestD = -1.0f;
    s32 i;

    for (i = 0; i < sEntranceCount; i++) {
        Vec3f p = sEntrances[i];
        f32 d;

        p.x += Math_SinS(sEntranceYaw[i]) * 150.0f;
        p.z += Math_CosS(sEntranceYaw[i]) * 150.0f;
        d = Ev_DistXZ(&p, &player->actor.world.pos);
        if ((d > bestD) && (d > 500.0f) && World_FloorAt(play, &p, sEntrances[i].y)) {
            bestD = d;
            *out = p;
        }
    }
    return bestD > 0.0f;
}

// The entrance closest to a creature: its way out.
static s32 World_PickExit(Vec3f* from, Vec3f* out) {
    f32 bestD = 1.0e9f;
    s32 i;

    for (i = 0; i < sEntranceCount; i++) {
        f32 d = Ev_DistXZ(from, &sEntrances[i]);

        if (d < bestD) {
            bestD = d;
            *out = sEntrances[i];
        }
    }
    return bestD < 1.0e8f;
}

static void World_MarkRunning(Actor* actor) {
    actor->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
}

static void World_BountyName(Bounty* b, char* out, s32 size) {
    s32 n = Ev_Append(out, 0, sBountyNames[b->nameIdx % 24], size);

    n = Ev_Append(out, n, " the ", size);
    Ev_Append(out, n, Pool_Name(b->pool), size);
}

// World news pop-ups (they can be switched off).
static void World_News(const char* a, const char* b, const char* c) {
    static char sNews[64];
    s32 n;

    if (!gOpt[ID_V2_NEWS]) {
        return;
    }
    n = Ev_Append(sNews, 0, a, sizeof(sNews));
    n = Ev_Append(sNews, n, b, sizeof(sNews));
    Ev_Append(sNews, n, c, sizeof(sNews));
    Menu_Notify(sNews, NOTE_NEWS);
}

// Drives a creature: wander near home, walk out, shadow or chase you.
// Returns false once it has walked out of the area (it's removed).
static s32 World_Drive(PlayState* play, Actor* actor, WorldBody* body, s32 chase, f32 speedMul) {
    Player* player = GET_PLAYER(play);
    f32 dist = actor->xzDistToPlayer;

    if (body->leaving) {
        if (--body->timer <= 0 || (Ev_DistXZ(&actor->world.pos, &body->exitPos) < 120.0f)) {
            Ev_Poof(play, &actor->world.pos, 1);
            Ev_KillOne(play, actor);
            return false;
        }
        if (Ev_StepToward(play, actor, &body->exitPos, 5.0f * speedMul)) {
            actor->shape.rot.y = actor->world.rot.y = Math_Vec3f_Yaw(&actor->world.pos, &body->exitPos);
        }
        return true;
    }
    if (chase == 1) {
        // Comes straight for you until it's close enough to fight.
        if ((dist > 380.0f) && Ev_StepToward(play, actor, &player->actor.world.pos, 6.0f * speedMul)) {
            actor->shape.rot.y = actor->world.rot.y = actor->yawTowardsPlayer;
        }
        return true;
    }
    if (chase == 2) {
        // Shadowing: keeps 500-800 away, circling, until it decides to strike.
        Vec3f to = actor->world.pos;
        s16 yaw = actor->yawTowardsPlayer;

        if (dist < 520.0f) {
            to.x -= Math_SinS(yaw) * 60.0f;
            to.z -= Math_CosS(yaw) * 60.0f;
        } else if (dist > 820.0f) {
            to = player->actor.world.pos;
        } else {
            to.x += Math_SinS(yaw + 0x4000) * 60.0f;
            to.z += Math_CosS(yaw + 0x4000) * 60.0f;
        }
        Ev_StepToward(play, actor, &to, 4.5f * speedMul);
        actor->shape.rot.y = actor->world.rot.y = yaw;
        return true;
    }
    if (chase == 3) {
        // Skittish: keeps away from you.
        if (dist < 800.0f) {
            Vec3f to = actor->world.pos;

            to.x -= Math_SinS(actor->yawTowardsPlayer) * 80.0f;
            to.z -= Math_CosS(actor->yawTowardsPlayer) * 80.0f;
            if (Ev_StepToward(play, actor, &to, 6.0f * speedMul)) {
                actor->shape.rot.y = actor->world.rot.y = actor->yawTowardsPlayer + 0x8000;
            }
            return true;
        }
    }
    // At home: pottering about while you're far away.
    if (dist > 700.0f) {
        if (--body->wander <= 0) {
            body->goal = body->home;
            body->goal.x += Rand_CenteredFloat(500.0f);
            body->goal.z += Rand_CenteredFloat(500.0f);
            body->wander = (4 + V2_Rand(5)) * FPS;
        }
        if ((Ev_DistXZ(&actor->world.pos, &body->goal) > 30.0f) &&
            Ev_StepToward(play, actor, &body->goal, 2.5f * speedMul)) {
            actor->shape.rot.y = actor->world.rot.y = Math_Vec3f_Yaw(&actor->world.pos, &body->goal);
        }
    }
    return true;
}

// Puts a creature into the area: through an entrance if it's arriving,
// otherwise at its home spot.
static Actor* World_Spawn(PlayState* play, s32 pool, WorldBody* body, u8 tag) {
    Vec3f at;
    Actor* actor;

    if (body->arriving) {
        if (!World_PickEntrance(play, &at)) {
            body->arriving = false;
            return NULL;
        }
        if (!body->hasHome) {
            body->hasHome = World_PickHome(play, &body->home);
        }
    } else {
        if (!body->hasHome) {
            body->hasHome = World_PickHome(play, &body->home);
            if (!body->hasHome) {
                return NULL;
            }
        }
        at = body->home;
    }
    actor = V2_SpawnEnemy(play, pool, &at, 0, 0, tag);
    if (actor != NULL) {
        World_MarkRunning(actor);
        body->goal = body->hasHome ? body->home : at;
        body->wander = 0;
        Ev_Poof(play, &actor->world.pos, 1);
    }
    return actor;
}

// Starts a creature walking out of the area you're in.
static void World_BeginLeave(PlayState* play, Actor* actor, WorldBody* body) {
    if ((actor == NULL) || !V2_IsLive(play, actor) || !World_PickExit(&actor->world.pos, &body->exitPos)) {
        if ((actor != NULL) && V2_IsLive(play, actor)) {
            Ev_Poof(play, &actor->world.pos, 1);
            Ev_KillOne(play, actor);
        }
        body->leaving = false;
        return;
    }
    body->leaving = true;
    body->timer = 15 * FPS;
}

static void World_ApplyWounds(Actor* actor, u8 hpPct) {
    TagData* t = Tag_Get(actor);

    if ((t != NULL) && (hpPct > 0) && (hpPct < 100)) {
        actor->colChkInfo.health = (u8)MAX(t->maxHp * hpPct / 100, 1);
        t->hp2 = actor->colChkInfo.health;
    }
}

static u8 World_Wounds(Actor* actor) {
    TagData* t = Tag_Get(actor);

    if ((t == NULL) || (t->maxHp == 0)) {
        return 100;
    }
    return (u8)CLAMP(actor->colChkInfo.health * 100 / t->maxHp, 1, 100);
}

// Coward: vanishes in smoke at low health and turns up later, healed.
static void World_CowardFlees(PlayState* play, Actor* actor, TagData* t) {
    s32 i;

    Ev_Poof(play, &actor->world.pos, 1);
    Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
    if (actor == sNem.actor) {
        sNem.actor = NULL;
        sNem.hpPct = 100;
        sNem.dormant = 240;
        sNem.zone = V2_Neighbor(Zone_Outdoor(sNem.zone));
        Menu_Notify("Your Nemesis fled in a cloud of smoke!", NOTE_NEMESIS);
    }
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        if (sBounties[i].actor == actor) {
            sBounties[i].actor = NULL;
            sBounties[i].hpPct = 100;
            sBounties[i].zone = V2_Neighbor(sBounties[i].zone);
            sBounties[i].body.hasHome = false;
            Menu_Notify("Your bounty fled in a cloud of smoke!", NOTE_WARN);
        }
    }
    Ev_KillOne(play, actor);
}

// One bounty's move on the map.
static s32 World_BountyNextZone(Bounty* b) {
    switch (b->motive) {
        case MOTIVE_ROAM:
            return (Rand_ZeroOne() < 0.3f) ? V2_Neighbor(b->zone) : b->zone;
        case MOTIVE_MIGRATE:
            if (b->zone == b->target) {
                b->motive = MOTIVE_NEST;
                return b->zone;
            }
            return (Rand_ZeroOne() < 0.5f) ? V2_StepToward(b->zone, b->target) : b->zone;
        case MOTIVE_NEST:
            if (Rand_ZeroOne() < 0.08f) {
                b->motive = MOTIVE_ROAM;
            }
            return b->zone;
        case MOTIVE_HUNT:
            if (b->accepted && (sPlayerZone >= 0)) {
                return (Rand_ZeroOne() < 0.6f) ? V2_StepToward(b->zone, sPlayerZone) : b->zone;
            }
            return (Rand_ZeroOne() < 0.3f) ? V2_Neighbor(b->zone) : b->zone;
        default: { // skittish: away from you
            s32 next = V2_Neighbor(b->zone);

            if ((sPlayerZone >= 0) && (next == V2_StepToward(b->zone, sPlayerZone))) {
                return b->zone;
            }
            return (Rand_ZeroOne() < 0.5f) ? next : b->zone;
        }
    }
}

static void World_UpdateBounties(PlayState* play, s32 zone) {
    char name[40];
    s32 i;

    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        Bounty* b = &sBounties[i];
        s32 live;

        if (!V2_ON(ID_V2_BOUNTY) || (b->pool < 0)) {
            if ((b->actor != NULL) && V2_IsLive(play, b->actor)) {
                Ev_KillOne(play, b->actor);
            }
            b->actor = NULL;
            continue;
        }
        if ((b->actor != NULL) && !V2_IsLive(play, b->actor)) {
            b->actor = NULL;
        }
        live = (b->actor != NULL);
        if (live) {
            b->aliveTimer++;
            b->hpPct = World_Wounds(b->actor);
        }
        // Moving between areas (not while it's fighting you).
        if (!(live && (b->actor->xzDistToPlayer < 600.0f)) && !b->body.leaving && (--b->migrate <= 0)) {
            s32 next = World_BountyNextZone(b);

            b->migrate = Bounty_MoveTime(i);
            if ((next != b->zone) && (next >= 0) && !sZoneBlocked[next]) {
                s32 was = b->zone;

                b->zone = next;
                if (b->accepted) {
                    World_BountyName(b, name, sizeof(name));
                    if (was == zone) {
                        World_News(name, " left for ", sZoneNames[next]);
                    } else if (next == zone) {
                        World_News(name, " came into ", sZoneNames[next]);
                    } else {
                        World_News(name, " moved to ", sZoneNames[next]);
                    }
                }
                if (was == zone) {
                    World_BeginLeave(play, b->actor, &b->body); // you see it walk off
                } else {
                    b->body.hasHome = false;
                    b->body.arriving = (next == zone); // comes in through an entrance
                }
            }
        }
        if (b->body.leaving) {
            if (!live || !World_Drive(play, b->actor, &b->body, 0, 1.0f)) {
                b->actor = NULL;
                b->body.leaving = false;
                b->body.hasHome = false;
            }
            continue;
        }
        if (live) {
            s32 mode = (b->motive == MOTIVE_HUNT) ? 1 : (b->motive == MOTIVE_SKITTISH) ? 3 : 0;

            World_Drive(play, b->actor, &b->body, mode, 1.0f);
            continue;
        }
        // In your area: it's out there somewhere.
        if (b->accepted && (zone == b->zone) && (sAreaTimer > 2 * FPS) && ((sAreaTimer % 10) == 0) &&
            !sZoneBlocked[zone] && (sActive != EV_QUIZ)) {
            b->actor = World_Spawn(play, b->pool, &b->body, TAG_BOUNTY);
            if (b->actor != NULL) {
                V2_ApplyTraits(b->actor, b->traits, 1.0f + 0.25f * b->stars);
                World_ApplyWounds(b->actor, b->hpPct);
                b->aliveTimer = 0;
                if (b->kind == BK_WANTED) {
                    TagData* bt = Tag_Get(b->actor);

                    if (bt != NULL) {
                        bt->v2flags |= TAGF_V2_ELITE;
                    }
                }
                if (b->body.arriving) {
                    World_BountyName(b, name, sizeof(name));
                    World_News(name, " has arrived.", "");
                }
                b->body.arriving = false;
            }
        }
    }
}

static void World_UpdateNemesis(PlayState* play, s32 zone) {
    char name[24];
    s32 hunted = gOpt[ID_V2_MUT] && Mut_Has(MUT_HUNTED);
    s32 live;

    if (!V2_ON(ID_V2_NEM)) {
        if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
            Ev_KillOne(play, sNem.actor);
        }
        sNem.actor = NULL;
        return;
    }
    if ((sNem.pool < 0) && (Mut_SlotNow() >= 1) && (sActive < 0) && !sNemBanished &&
        (Mut_SlotNow() != sNemRestSlot)) {
        // Night 1 and still no Nemesis: the moon chooses one, somewhere else.
        Nem_Promote(play, sHuntPool[V2_Rand(ARRAY_COUNT(sHuntPool))], NULL);
        if (sNem.pool >= 0) {
            s32 far = V2_Rand(ZONE_OUTDOOR);

            if (far == Zone_Outdoor(zone)) {
                far = (far + 7) % ZONE_OUTDOOR;
            }
            sNem.zone = far;
            bzero(&sNem.body, sizeof(sNem.body));
        }
    }
    if (sNem.pool < 0) {
        return;
    }
    Nem_Name(name, sizeof(name));
    if ((sNem.actor != NULL) && !V2_IsLive(play, sNem.actor)) {
        sNem.actor = NULL;
    }
    live = (sNem.actor != NULL);
    if (live) {
        sNem.hpPct = World_Wounds(sNem.actor);
    }
    // Unspent skill points spend themselves after 2 minutes, or right away without a friend.
    if (Nem_PointsFree() > 0) {
        if (!V2_ON(ID_V2_LINK) || (++sNemPointTimer >= 2 * 60 * FPS)) {
            s32 st = Nem_AutoStat();

            sNemPointTimer = 0;
            if (st >= 0) {
                Nem_SpendPoint(st);
            }
        }
    } else {
        sNemPointTimer = 0;
    }
    // Licking its wounds (scarred return, Coward).
    if (sNem.dormant > 0) {
        if ((sAreaTimer % FPS) == 0) {
            if (--sNem.dormant == 0) {
                World_News(name, " stirs. It is hunting again.", "");
            }
        }
        if (live && !sNem.body.leaving) {
            World_BeginLeave(play, sNem.actor, &sNem.body);
        }
    }
    // Travelling toward wherever you are (not while it's fighting you).
    if ((sNem.dormant == 0) && !(live && (sNem.actor->xzDistToPlayer < 900.0f)) && !sNem.body.leaving &&
        (--sNem.migrate <= 0)) {
        s32 was = sNem.zone;
        s32 next = (sPlayerZone >= 0) ? V2_StepToward(sNem.zone, sPlayerZone) : V2_Neighbor(sNem.zone);

        sNem.migrate = Nem_MigrateFrames(hunted);
        if ((next >= 0) && (next != was) && !sZoneBlocked[next]) {
            sNem.zone = next;
            if (next == zone) {
                sNem.body.arriving = true;
                sNem.body.hasHome = false;
                Menu_Notify("Your Nemesis has entered the area...", NOTE_NEMESIS);
                Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
            } else if (Rand_ZeroOne() < 0.45f) {
                World_News(name, " was seen in ", sZoneNames[next]);
            }
        }
    }
    if (sNem.body.leaving) {
        if (!live || !World_Drive(play, sNem.actor, &sNem.body, 0, 1.0f)) {
            sNem.actor = NULL;
            sNem.body.leaving = false;
            sNem.body.hasHome = false;
        }
        return;
    }
    if (live) {
        f32 speedMul = 1.0f + MIN(0.06f * Nem_StatEff(NS_SPEED), 0.8f);
        s32 mode;

        if (sNem.style == 1) {
            // Shadower: follows you at a distance for a while, then strikes.
            if (sNem.body.phase == 0) {
                if (sNem.actor->xzDistToPlayer < 1500.0f) {
                    if (--sNem.body.timer <= 0) {
                        sNem.body.phase = 1;
                        Menu_Notify("It's coming for you!", NOTE_NEMESIS);
                    }
                }
                mode = (sNem.body.phase == 0) ? 2 : 1;
            } else {
                mode = 1;
            }
        } else {
            mode = 1; // charger
        }
        World_Drive(play, sNem.actor, &sNem.body, mode, speedMul);
        return;
    }
    if ((zone == sNem.zone) && (sNem.dormant == 0) && (sAreaTimer > 2 * FPS) && ((sAreaTimer % 10) == 0) &&
        !sZoneBlocked[zone] && (sActive != EV_QUIZ)) {
        Actor* actor = World_Spawn(play, sNem.pool, &sNem.body, TAG_NEMV2);

        if (actor != NULL) {
            f32 diff = (gOpt[ID_V2_NEMDIFF] % 3 == 0) ? 0.8f : (gOpt[ID_V2_NEMDIFF] % 3 == 2) ? 1.3f : 1.0f;

            sNem.actor = actor;
            sNem.engaged = false;
            sNem.body.arriving = false;
            sNem.body.phase = 0;
            sNem.body.timer = (15 + V2_Rand(11)) * FPS;
            V2_ApplyTraits(actor, Nem_Traits(), Nem_HpMult() * diff);
            World_ApplyWounds(actor, sNem.hpPct);
            {
                char sub[48];
                s32 n = Ev_Append(sub, 0, "Lv", sizeof(sub));

                n = Ev_AppendNum(sub, n, Nem_EffLevel(), sizeof(sub));
                n = Ev_Append(sub, n, " ", sizeof(sub));
                Ev_Append(sub, n, Pool_Name(sNem.pool), sizeof(sub));
                V2_Banner(name, sub);
            }
        }
    }
}

// Crowning: an enemy that hits you three times and lives may become your Nemesis.
static void World_CheckCrown(PlayState* play) {
    TagData* t;

    if ((sNem.pool >= 0) || !V2_ON(ID_V2_NEM) || (sLastHitter == NULL) || !V2_IsLive(play, sLastHitter)) {
        return;
    }
    t = Tag_Get(sLastHitter);
    if ((t == NULL) || (t->tag != TAG_NONE) || (t->linkHits < 3) || (t->linkHits >= 200)) {
        return;
    }
    t->linkHits = 200; // rolled once
    if (Rand_ZeroOne() < 0.6f) {
        s32 pool = V2_PoolOfActor(sLastHitter);
        s32 k;

        for (k = 0; k < (s32)ARRAY_COUNT(sHuntPool); k++) {
            if (sHuntPool[k] == pool) {
                Nem_Promote(play, pool, sLastHitter);
                if (sNem.actor == sLastHitter) {
                    World_MarkRunning(sLastHitter);
                    sNem.body.phase = 1;
                }
                break;
            }
        }
    }
}

// Entering an area: everything here gets a fresh spot.
static void World_OnNewArea(void) {
    s32 i;

    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        WorldBody* body = &sBounties[i].body;

        body->hasHome = false;
        body->leaving = false;
    }
    sNem.body.hasHome = false;
    sNem.body.leaving = false;
    sEntranceCount = 0;
}

static void World_OnRoomChange(void) {
    s32 i;

    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        sBounties[i].body.hasHome = false;
        sBounties[i].body.leaving = false;
    }
    sNem.body.hasHome = false;
    sNem.body.leaving = false;
    sEntranceCount = 0;
}

static void World_Update(PlayState* play, s32 zone) {
    if ((sEntranceCount == 0) && (sAreaTimer == 2)) {
        World_GatherEntrances(play);
    }
    if (zone >= 0) {
        // Areas where the game takes your sword away are off limits.
        sZoneBlocked[zone] = (play->interfaceCtx.restrictions.bButton != 0);
    }
    World_CheckCrown(play);
    World_UpdateBounties(play, zone);
    World_UpdateNemesis(play, zone);
}

// Tracking critical mass: it follows you through any door right away.
static void World_OnPlayerZoneChange(s32 from, s32 to) {
    if ((sNem.pool >= 0) && Nem_Crit(NS_TRACK) && (sNem.dormant == 0) && (to >= 0) && (sNem.zone == from)) {
        sNem.zone = to;
        sNem.body.arriving = true;
        sNem.body.hasHome = false;
        Menu_Notify("Your Nemesis followed you in!", NOTE_NEMESIS);
    }
}

static void V2_UpdateInner(PlayState* play, s32 playing, s32 prevHp) {
    Player* player = GET_PLAYER(play);
    s32 zone = V2_ZoneNow(play);
    s32 hp = gSaveContext.save.saveInfo.playerData.health;
    s32 i;

    gPlayState_v2 = play;
    if ((zone >= 0) && (zone != sPlayerZone)) {
        World_OnPlayerZoneChange(sPlayerZone, zone);
    }
    if (zone >= 0) {
        sPlayerZone = zone; // where you are (the Nemesis heads there; your friend's map shows it)
    }
    if (playing) {
        Auto_Tick();
    }
    V2_InitOnce();
    // Thorned: striking one stings you.
    if ((sThornTimer > 0) && (player != NULL) && playing) {
        sThornTimer = 0;
        func_800B8D50(play, &player->actor, 2.0f, player->actor.shape.rot.y + 0x8000, 2.0f, 0x04);
    }
    SaveUi_Update(play);
    Pouch_Update(play, playing);
    Mut_Update(play, playing);
    Mut_NightLook(play);
    if (!Ev_IsOn(EV_TINGLE) && !Ev_IsOn(EV_CURSETAG) && playing) {
        Drops_Update(play);
    }
    if (player == NULL) {
        return;
    }

    // Who last hit Link? The game clears this during Link's own update, so it
    // has to be caught here, before that, and remembered for a moment.
    {
        Actor* ac = player->cylinder.base.ac;

        if ((ac != NULL) && (ac->category == ACTORCAT_ENEMY) && (ac->update != NULL)) {
            sLastHitter = ac;
            sLastHitterTimer = 2 * FPS;
        } else if ((sLastHitterTimer > 0) && (--sLastHitterTimer == 0)) {
            sLastHitter = NULL;
        }
    }

    // Link going down: who did it?
    if ((prevHp > 0) && (hp == 0)) {
        Actor* killer = sLastHitter;
        TagData* kt;

        sBleedAmount = 0; // no bleeding into the respawn
        sLinkDeaths++;
        if ((killer == NULL) || !V2_IsLive(play, killer)) {
            killer = V2_NearestEnemy(play, 200.0f); // e.g. a grab or an explosion
        }
        if ((killer == NULL) && (sNemCandidate != NULL) && V2_IsLive(play, sNemCandidate)) {
            killer = sNemCandidate;
        }
        kt = (killer != NULL) ? Tag_Get(killer) : NULL;
        if ((kt != NULL) && (kt->tag == TAG_NEMV2) && (killer == sNem.actor)) {
            sNem.wins++;
            Nem_LevelUp("Your Nemesis grows stronger.");
        } else if (killer != NULL) {
            Nem_Promote(play, V2_PoolOfActor(killer), killer);
        } else if ((sNemCandidate != NULL) && (sNemCandidatePool >= 0)) {
            Nem_Promote(play, sNemCandidatePool, NULL); // it wore you down and slipped away
        }
    }
    // Knocked below 2 hearts: that enemy is a candidate if it survives.
    if ((prevHp >= 0x20) && (hp > 0) && (hp < 0x20)) {
        Actor* hitter = ((sLastHitter != NULL) && V2_IsLive(play, sLastHitter)) ? sLastHitter
                                                                                 : V2_NearestEnemy(play, 200.0f);

        if ((hitter != NULL) && (hitter->category == ACTORCAT_ENEMY) && (V2_PoolOfActor(hitter) >= 0)) {
            sNemCandidate = hitter;
            sNemCandidatePool = V2_PoolOfActor(hitter);
        }
    }
    if (!playing) {
        return;
    }
    sAreaTimer++;
    if (sAreaTimer == 1) {
        sEntryPos = player->actor.world.pos; // where you came in
        // Nemesis Lure / Bounty Bait: they come to the first area you enter
        // outside Clock Town.
        if ((zone >= 0) && (Zone_Outdoor(zone) > 4) && sNemLure && (sNem.pool >= 0)) {
            sNemLure = false;
            sNem.zone = zone;
            sNem.dormant = 0;
            sNem.body.arriving = true;
            sNem.body.hasHome = false;
            sNem.spawnedVisit = false;
            sNem.tries = 0;
            sNem.migrate = MAX(sNem.migrate, 4 * 60 * FPS);
            Menu_ShowToast("Your Nemesis followed you here.");
        }
        if ((zone >= 0) && (Zone_Outdoor(zone) > 4) && sBountyBait) {
            sBountyBait = false;
            for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
                Bounty* b = &sBounties[i];

                if ((b->pool >= 0) && b->accepted) {
                    b->zone = zone;
                    b->body.arriving = true;
                    b->body.hasHome = false;
                    b->spawnedVisit = false;
                    b->tries = 0;
                    b->migrate = MAX(b->migrate, 4 * 60 * FPS);
                }
            }
            Menu_ShowToast("Your bounties took the bait.");
        }
    }
    V2_ScanEnemies(play);
    Board_Update(play, player);

    // Mutator draft: pick 1 of 2 at every dawn and dusk.
    if (gOpt[ID_V2_MUT] && (sActive < 0) && (sRouletteTimer == 0) && (sCurseCardTimer == 0) && !gMenuOpen &&
        !sSaveUiOpen && !Ev_InSafeZone(play)) {
        s32 now = Mut_SlotNow();
        s32 slot;

        for (slot = 0; slot <= now; slot++) {
            if (!(sDraftDone & (1 << slot))) {
                break;
            }
        }
        if ((now >= 0) && (slot <= now) && (sMutCount >= 6)) {
            sDraftDone |= (1 << slot); // the deck is full
        } else if ((now >= 0) && (slot <= now) && (sWardCharges > 0)) {
            // Moon Ward: this draft is skipped.
            sWardCharges--;
            sDraftDone |= (1 << slot);
            Menu_ShowToast("Moon Ward: no mutator this time.");
            Audio_PlaySfx(NA_SE_IT_SHIELD_REFLECT_SW);
        } else if ((now >= 0) && (slot <= now)) {
            if (Quiz_CanHold(player)) {
                sDraftSlot = slot;
                sOfferA = Mut_RandomNew(-1);
                sOfferB = Mut_RandomNew(sOfferA);
                // Friend Link: your friend chose what's on offer.
                if ((sFriendDraftA >= 0) && !Mut_Has(sFriendDraftA)) {
                    sOfferA = sFriendDraftA;
                }
                if ((sFriendDraftB >= 0) && !Mut_Has(sFriendDraftB) && (sFriendDraftB != sOfferA)) {
                    sOfferB = sFriendDraftB;
                } else if (sOfferB == sOfferA) {
                    sOfferB = Mut_RandomNew(sOfferA);
                }
                sFriendDraftA = sFriendDraftB = -1;
                if (sOfferA < 0) {
                    sDraftDone |= (1 << slot);
                } else {
                    if (sOfferB < 0) {
                        sOfferB = sOfferA;
                    }
                    Menu_OpenV2(play, 1);
                    Audio_PlaySfx(NA_SE_EN_WIZ_LAUGH);
                }
            }
        }
    }

    // Each dawn: untaken postings and untouched contracts are replaced.
    {
        s32 slot = Mut_SlotNow();

        if ((slot >= 0) && (slot != sLastSlotSeen)) {
            if ((sLastSlotSeen >= 0) && (slot > sLastSlotSeen) && ((slot % 2) == 0) && V2_ON(ID_V2_BOUNTY)) {
                s32 replaced = 0;

                for (i = 0; i < 3; i++) {
                    if (!sBounties[i].accepted) {
                        Bounty_New(i);
                        replaced++;
                    }
                    if (sContracts[i].progress == 0) {
                        Contract_New(i);
                    }
                }
                if (replaced > 0) {
                    Menu_ShowToast("New postings at the Bounty Board.");
                }
            }
            sLastSlotSeen = slot;
        }
    }

    // Day 3: a WANTED poster goes up.
    if (V2_ON(ID_V2_BOUNTY) && !sWantedPosted && (Mut_SlotNow() >= 4)) {
        sWantedPosted = true;
        Bounty_Fill(SLOT_WANTED, sHuntPool[V2_Rand(ARRAY_COUNT(sHuntPool))], 5, BK_WANTED);
        Menu_ShowToast("A WANTED poster went up in Clock Town!");
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    }

    World_Update(play, zone);
}

static void V2_OnNewArea(void) {
    s32 i;

    if ((sNem.pool >= 0) && sNemEscaped) {
        Nem_LevelUp("Your Nemesis grows stronger.");
    }
    sNemEscaped = false;
    if ((sPendingPromote >= 0) && (sNem.pool < 0)) {
        Nem_Promote(NULL, sPendingPromote, NULL); // it hurt you and you left
    }
    sPendingPromote = -1;
    sNemCandidate = NULL;
    sNemCrash = false; // a crash is for the area where the event was
    sBleedAmount = 0;
    sNem.actor = NULL;
    sNem.engaged = false;
    sNem.tries = 0;
    // The Nemesis only shows up about half the time you walk into its zone.
    // (Tracking makes it show up more often: 50%, +10% per point.)
    sNem.spawnedVisit =
        (Rand_ZeroOne() < ((gOpt[ID_V2_MUT] && Mut_Has(MUT_HUNTED)) ? 0.0f : (0.5f - 0.1f * sNemStat[NS_TRACK])));
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        sBounties[i].actor = NULL;
        sBounties[i].spawnedVisit = false;
        sBounties[i].tries = 0;
    }
    sGuards[0] = sGuards[1] = NULL;
    sDecoy = NULL;
    sDecoyTimer = 0;
    sVeilTimer = 0;
    sAreaTimer = 0;
    World_OnNewArea();
    sV2PrevHp = -1; // loads, respawns and warps aren't heals or hits
    sSpawnWait = (8 + V2_Rand(18)) * FPS; // targets show up a little while after you arrive
    sBoard = NULL;
    sAmbushDone = false;
    sPortalCount = 0;
    sRecallCount = 0;
    sGravTimer = 0;
    sNightDark = false;
}

// Room change inside an area: our actors are being removed.
static void V2_OnRoomChange(void) {
    s32 i;

    sBoard = NULL;
    sDecoy = NULL; // killed with the room
    sDecoyTimer = 0;
    // Leaving with them still alive counts as getting away (checked in V2_OnNewArea).
    // (Not when Link fell: that already counted as its win.)
    if ((gPlayState_v2 != NULL) && (sNem.actor != NULL) && sNem.engaged && V2_IsLive(gPlayState_v2, sNem.actor) &&
        (GET_PLAYER(gPlayState_v2) != NULL) && !(GET_PLAYER(gPlayState_v2)->stateFlags1 & PLAYER_STATE1_DEAD)) {
        sNemEscaped = true;
    }
    if ((gPlayState_v2 != NULL) && (sNemCandidate != NULL) && V2_IsLive(gPlayState_v2, sNemCandidate)) {
        sPendingPromote = sNemCandidatePool;
    }
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        // A live bounty follows you into the next room.
        if ((gPlayState_v2 != NULL) && (sBounties[i].actor != NULL) && V2_IsLive(gPlayState_v2, sBounties[i].actor)) {
            sBounties[i].spawnedVisit = false;
            sBounties[i].tries = 0;
        }
        sBounties[i].actor = NULL;
    }
    sGuards[0] = sGuards[1] = NULL;
    sNem.actor = NULL;
    sNemCandidate = NULL;
    sLastHitter = NULL;
    World_OnRoomChange();
    sRecallCount = 0; // (portals stay: they work across rooms)
}

static void V2_OnNewCycle(void) {
    s32 i;

    Mut_ClearAll();
    sBanMask = 0;
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        sBounties[i].spawnedVisit = false;
    }
    Bounty_New(SLOT_WANTED);
    Bounty_New(SLOT_RARE);
    Bounty_New(SLOT_FRIEND);
    sWantedPosted = false;
    sHuntStreak = 0;
    sStreakPaid = false;
    sHunterSight = false;
    sNemBanished = false;
    sNemRestSlot = -1;
    sLastSlotSeen = -1;
    // Lure and Bait were paid for: they carry over until they happen.
    sFriendNext = sFriendCombo = -1;
    sForesight = false;
    sFriendDraftA = sFriendDraftB = -1;
    Link_DropFriendNow();
}

// A save was loaded (or a new file started). Per-cycle state only carries
// over if the file picks up where we were; an earlier point in the cycle (or
// a new file) starts clean. The save code restores mutators if you want them.
static void V2_OnLoadSave(s32 newFile) {
    s32 slot;

    sPlayerZone = -1; // where you were in another file (or before a reload) doesn't count

    Auto_OnFileLoaded(newFile);
    slot = Mut_SlotNow();

    sPrevMagic = -1;
    sAmmoReady = false;
    sBleedAmount = 0;
    sFriendNext = sFriendCombo = -1;
    sForesight = false;
    Link_DropFriendNow();
    sFriendDraftA = sFriendDraftB = -1;
    if (newFile || (slot < 0) || ((sLastSlotSeen >= 0) && (slot < sLastSlotSeen)) || (sDraftDone >> (slot + 1))) {
        V2_OnNewCycle();
    }
    sLastSlotSeen = -1;
}

// Event tie-ins
static s32 V2_UseMercy(void) {
    if ((sMercyCharges <= 0) || sManualTrigger) {
        return false;
    }
    sMercyCharges--;
    Menu_ShowToast("Moon's Mercy: an event fades away.");
    Audio_PlaySfx(NA_SE_IT_SHIELD_REFLECT_SW);
    return true;
}

static void V2_OnEventStartHook(PlayState* play) {
    sEventWasCalled = sCallPending || sFreeRollPending; // no survival Marks for events you start yourself
    sEventByFriend = sStartByFriend;
    sStartByFriend = false;
    sFreeRollPending = false;
    sFriendEmpowered = sFriendEmpowerNext;
    sFriendEmpowerNext = false;
    if (sFriendEmpowered) {
        Menu_ShowToast("Your friend empowered this one!");
    }
    V2_OnEventStart();
    // The Nemesis may crash the event.
    if (V2_ON(ID_V2_NEM) && (sNem.pool >= 0) && (sNem.actor == NULL) && (Rand_ZeroOne() < 0.15f) &&
        (V2_ZoneNow(play) >= 0) && !Ev_InSafeZone(play)) {
        sNem.zone = V2_ZoneNow(play);
        sNem.spawnedVisit = false;
        sNemCrash = true;
        Menu_ShowToast("Your Nemesis joins the fray!");
    }
}

static void V2_OnEventEnded(s32 ev, s32 quiet, s32 died) {
    s32 n;

    sFriendEmpowered = false;
    sNemCrash = false;
    // For the Friend Link panel (wagers resolve on this).
    sEvSeq++;
    n = Ev_Append(sEvResult, 0, Events_Name(ev), sizeof(sEvResult));
    n = Ev_Append(sEvResult, n, "|", sizeof(sEvResult));
    Ev_Append(sEvResult, n, died ? "fell" : quiet ? "stopped" : "survived", sizeof(sEvResult));
    if (!quiet && !died) {
        sLinkSurvived++;
    }
    if (!quiet && !died) {
        V2_OnEventSurvived(ev, sCombo);
    }
    // A Nemesis-event enemy that lived through it becomes your Nemesis.
    if ((ev == EV_NEMESIS) && (sNemActor != NULL) && (sNemPick >= 0) && (gPlayState_v2 != NULL) &&
        V2_IsLive(gPlayState_v2, sNemActor)) {
        Nem_Promote(gPlayState_v2, sNemPick, NULL);
    }
}

// Smoke Veil and Decoy: right before an enemy thinks, tell it where "you" are.
// (The game worked out its distance and direction to you just before this.)
static void V2_Perception(PlayState* play, Actor* actor) {
    if ((actor->category != ACTORCAT_ENEMY) || (actor->update == NULL)) {
        return;
    }
    if ((sDecoy != NULL) && (sDecoyTimer > 0)) {
        f32 d = Ev_DistXZ(&actor->world.pos, &sDecoy->world.pos);

        if (d < 900.0f) {
            actor->xzDistToPlayer = d;
            actor->xyzDistToPlayerSq = SQ(d) + SQ(actor->playerHeightRel);
            actor->yawTowardsPlayer = Math_Vec3f_Yaw(&actor->world.pos, &sDecoy->world.pos);
            return;
        }
    }
    if (sVeilTimer > 0) {
        actor->xzDistToPlayer = 3000.0f;
        actor->xyzDistToPlayerSq = 3000.0f * 3000.0f;
        actor->yawTowardsPlayer = actor->shape.rot.y + 0x8000;
    }
}

s32 V2_ShouldFreeze(Actor* actor) {
    if ((Tag_Of(actor) == TAG_BOARD) || (Tag_Of(actor) == TAG_DECOY)) {
        return true; // we animate the Bounty Board carpenter ourselves; the Decoy stands still
    }
    if ((sHourglassTimer > 0) && (actor->category == ACTORCAT_ENEMY)) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void V2_DrawWorld(PlayState* play) {
    s32 k;

    if (!Ev_IsOn(EV_TINGLE) && !Ev_IsOn(EV_CURSETAG)) {
        Drops_Draw(play);
    }
    for (k = 0; k < sPortalCount; k++) {
        if (sPortalRoom[k] == play->roomCtx.curRoom.num) {
            u8 a = 150 + (u8)(Math_SinS((s16)(play->gameplayFrames * 0x800 + k * 0x4000)) * 60.0f);

            Ev_DrawGroundCircle(play, &sPortal[k], NULL, 0.7f, (k == 0) ? 60 : 255, (k == 0) ? 140 : 140,
                                (k == 0) ? 255 : 30, a);
        }
    }
    if (sGravTimer > 0) {
        Ev_DrawGroundCircle(play, &sGravPos, NULL, 1.0f + sGravTimer * 0.08f, 160, 60, 255, 200);
    }
}

s32 V2_WantsDraw(void) {
    return V2_ON(ID_V2_POUCH) || V2_ON(ID_V2_NEM) || V2_ON(ID_V2_BOUNTY);
}

// Cloaked actors hide their tags too, unless the Lens of Truth is on.
static s32 V2_TagVisible(PlayState* play, Actor* actor) {
    return !(actor->flags & ACTOR_FLAG_REACT_TO_LENS) || play->actorCtx.lensActive;
}

// A label floating over an actor.
static Gfx* V2_Tag(PlayState* play, Gfx* gfx, GfxPrint* printer, Actor* actor, const char* text, u8 r, u8 g, u8 b) {
    Vec3f pos = actor->world.pos;
    Vec3f proj;
    f32 invW;
    s32 x;
    s32 y;

    pos.y += 90.0f * (actor->scale.y / 0.01f) * 0.5f + 40.0f;
    Actor_GetProjectedPos(play, &pos, &proj, &invW);
    if ((proj.z < 1.0f) || (proj.z > 2500.0f)) {
        return gfx;
    }
    x = (s32)(SCREEN_WIDTH / 2 + proj.x * invW * (SCREEN_WIDTH / 2)) - Ui_StrLen(text) * 4;
    y = (s32)(SCREEN_HEIGHT / 2 - proj.y * invW * (SCREEN_HEIGHT / 2));
    if ((x < 0) || (y < 8) || (x > SCREEN_WIDTH - 16) || (y > SCREEN_HEIGHT - 16)) {
        return gfx;
    }
    GfxPrint_SetColor(printer, r, g, b, 255);
    Ui_Print(printer, x, y, text);
    return gfx;
}

static Gfx* V2_Draw(PlayState* play, Gfx* gfx) {
    GfxPrint printer;
    char line[48];
    s32 i;
    s32 n;
    Player* player = GET_PLAYER(play);
    s32 nemNear = (sNem.actor != NULL) && V2_IsLive(play, sNem.actor) && (sNem.actor->xzDistToPlayer < 700.0f);

    if (player == NULL) {
        return gfx;
    }
    if (nemNear) {
        s32 w = 150;
        s32 hpw = (s32)((w - 4) * CLAMP((f32)sNem.actor->colChkInfo.health / MAX(Tag_Get(sNem.actor)->maxHp, 1),
                                        0.0f, 1.0f));

        gfx = Ui_DrawRect(gfx, (SCREEN_WIDTH - w) / 2, 196, (SCREEN_WIDTH + w) / 2, 214, 20, 0, 0, 190);
        gfx = Ui_DrawRect(gfx, (SCREEN_WIDTH - w) / 2 + 2, 209, (SCREEN_WIDTH - w) / 2 + 2 + hpw, 212, 230, 30, 30,
                          240);
    }
    // Pouch
    if (V2_ON(ID_V2_POUCH)) {
        Input* input = &play->state.input[0];
        s32 show = CHECK_BTN_ALL(input->cur.button, BTN_L) || (sPouch[sPouchSel] > 0) || (sPouchFlash > 0);

        if (show) {
            n = Ev_Append(line, 0, "L+Down: ", sizeof(line));
            n = Ev_Append(line, n, sPouchNames[sPouchSel], sizeof(line));
            n = Ev_Append(line, n, " x", sizeof(line));
            n = Ev_AppendNum(line, n, sPouch[sPouchSel], sizeof(line));
            if ((sPouchSel == PI_GIGA) && sGigaArmed) {
                n = Ev_Append(line, n, " ARMED", sizeof(line));
            }
            if ((sPouchSel == PI_SHIELD) && sShieldUp) {
                n = Ev_Append(line, n, " UP", sizeof(line));
            }
            gfx = Ev_BeginLeftAligned(gfx);
            gfx = Ui_DrawRect(gfx, 14, 218, 20 + Ui_StrLen(line) * 8, 232, 0, 0, 30, 150);
            // Holding L: everything in the pouch, above the selected item.
            if (CHECK_BTN_ALL(input->cur.button, BTN_L) && (Pouch_OwnedCount() > 0)) {
                s32 m = Pouch_OwnedCount();

                gfx = Ui_DrawRect(gfx, 14, 214 - 10 * m, 20 + 20 * 8, 216, 0, 0, 30, 170);
            }
            gfx = Ev_EndAligned(gfx);
        }
    }

    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2);

    // Name tags (not while a Cloaked target is hidden, unless the Lens is on)
    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        Actor* ba = sBounties[i].actor;

        if ((sBounties[i].pool >= 0) && (ba != NULL) && V2_IsLive(play, ba) && V2_TagVisible(play, ba)) {
            s32 k;
            s32 kind = sBounties[i].kind;

            n = Ev_Append(line, 0,
                          (kind == BK_WANTED)   ? "WANTED "
                          : (kind == BK_RARE)   ? "RARE "
                          : (kind == BK_FRIEND) ? "FRIEND "
                                                : "BOUNTY ",
                          sizeof(line));
            for (k = 0; (k < sBounties[i].stars) && (n < (s32)sizeof(line) - 1); k++) {
                line[n++] = '*';
            }
            line[n] = '\0';
            if (kind == BK_WANTED) {
                V2_Tag(play, NULL, &printer, ba, line, 255, 90, 50);
            } else if (kind == BK_RARE) {
                V2_Tag(play, NULL, &printer, ba, line, 200, 130, 255);
            } else if (kind == BK_FRIEND) {
                V2_Tag(play, NULL, &printer, ba, line, 90, 230, 230);
            } else {
                V2_Tag(play, NULL, &printer, ba, line, 255, 220, 60);
            }
        }
    }
    // Elite Foes: name the trait over nearby elites.
    if (gOpt[ID_V2_MUT] && Mut_Has(MUT_ELITE)) {
        Actor* e = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
        s32 shown = 0;

        while ((e != NULL) && (shown < 4)) {
            TagData* et = Tag_Get(e);

            if ((e->update != NULL) && (et != NULL) && (et->v2flags & TAGF_V2_ELITEFOE) &&
                (e->colChkInfo.health > 0) && (e->xzDistToPlayer < 500.0f) && V2_TagVisible(play, e)) {
                s32 k;

                for (k = 0; k < TR_COUNT; k++) {
                    if (et->traits & (1 << k)) {
                        n = Ev_Append(line, 0, "ELITE: ", sizeof(line));
                        Ev_Append(line, n, sTraitNames[k], sizeof(line));
                        V2_Tag(play, NULL, &printer, e, line, 255, 170, 60);
                        shown++;
                        break;
                    }
                }
            }
            e = e->next;
        }
    }
    for (i = 0; i < 2; i++) {
        if ((sGuards[i] != NULL) && V2_IsLive(play, sGuards[i]) && V2_TagVisible(play, sGuards[i])) {
            V2_Tag(play, NULL, &printer, sGuards[i], "GUARD", 255, 140, 60);
        }
    }
    if ((sMenuSpot != NULL) && !gMenuOpen) {
        const char* prompt = "Press L: Moon Menu";

        GfxPrint_SetColor(&printer, 255, 240, 120, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(prompt) * 8) / 2, 178, prompt);
    }
    if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor) && V2_TagVisible(play, sNem.actor)) {
        Nem_Name(line, sizeof(line));
        V2_Tag(play, NULL, &printer, sNem.actor, line, 255, 60, 60);
    }
    if (nemNear) {
        n = Ev_Append(line, 0, "NEMESIS ", sizeof(line));
        Nem_Name(line + n, sizeof(line) - n);
        n = Ui_StrLen(line);
        n = Ev_Append(line, n, " Lv", sizeof(line));
        Ev_AppendNum(line, n, Nem_EffLevel(), sizeof(line));
        GfxPrint_SetColor(&printer, 255, 90, 90, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(line) * 8) / 2, 199, line);
    }
    // Compass
    if ((sCompassTimer > 0) && (sNem.pool >= 0)) {
        n = Ev_Append(line, 0, "Nemesis: ", sizeof(line));
        if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
            static const char* sArrows[8] = { "^", "<^", "<", "<v", "v", "v>", ">", "^>" };
            Camera* cam = GET_ACTIVE_CAM(play);
            s16 rel = Math_Vec3f_Yaw(&player->actor.world.pos, &sNem.actor->world.pos) -
                      ((cam != NULL) ? Camera_GetCamDirYaw(cam) : 0);

            n = Ev_Append(line, n, "HERE ", sizeof(line));
            Ev_Append(line, n, sArrows[((u16)(rel + 0x1000) >> 13) & 7], sizeof(line));
        } else {
            Ev_Append(line, n, sZoneNames[sNem.zone % ZONE_COUNT], sizeof(line));
        }
        GfxPrint_SetColor(&printer, 255, 120, 120, 255);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(line) * 8) / 2, 30, line);
    }
    // V3 compass: an arrow to a bounty you took when it's in your area (or a
    // note when it's behind one of this area's doors).
    if (V2_ON(ID_V2_TRACKER) && V2_ON(ID_V2_BOUNTY)) {
        s32 zoneNow = V2_ZoneNow(play);
        s32 best = -1;
        s32 inside = -1;
        f32 bestD = 1.0e9f;
        Vec3f target;

        for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
            Bounty* b = &sBounties[i];
            Vec3f p;
            f32 d;

            if ((b->pool < 0) || !b->accepted || (zoneNow < 0)) {
                continue;
            }
            if ((b->zone != zoneNow) && Zone_IsIndoor(b->zone) && (sZones[b->zone].parent == zoneNow)) {
                inside = i;
                continue;
            }
            if (b->zone != zoneNow) {
                continue;
            }
            if ((b->actor != NULL) && V2_IsLive(play, b->actor)) {
                p = b->actor->world.pos;
            } else if (b->body.hasHome) {
                p = b->body.home;
            } else {
                continue;
            }
            d = Ev_DistXZ(&p, &player->actor.world.pos);
            if (d < bestD) {
                bestD = d;
                best = i;
                target = p;
            }
        }
        if (best >= 0) {
            static const char* sArrows[8] = { "^", "<^", "<", "<v", "v", "v>", ">", "^>" };
            Camera* cam = GET_ACTIVE_CAM(play);
            s16 rel = Math_Vec3f_Yaw(&player->actor.world.pos, &target) - ((cam != NULL) ? Camera_GetCamDirYaw(cam) : 0);

            n = Ev_Append(line, 0, sArrows[((u16)(rel + 0x1000) >> 13) & 7], sizeof(line));
            n = Ev_Append(line, n, " ", sizeof(line));
            World_BountyName(&sBounties[best], line + n, sizeof(line) - n);
            n = Ui_StrLen(line);
            if ((sBounties[best].bonus == BONUS_QUICK) && (sBounties[best].actor != NULL) &&
                (sBounties[best].aliveTimer < QUICK_FRAMES)) {
                n = Ev_Append(line, n, " ", sizeof(line));
                n = Ev_AppendNum(line, n, (QUICK_FRAMES - sBounties[best].aliveTimer + FPS - 1) / FPS, sizeof(line));
                Ev_Append(line, n, "s", sizeof(line));
            }
            GfxPrint_SetColor(&printer, 255, 220, 60, 255);
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(line) * 8) / 2, ((sCompassTimer > 0) && (sNem.pool >= 0)) ? 40 : 30,
                     line);
        } else if (inside >= 0) {
            n = Ev_Append(line, 0, "Your bounty is in the ", sizeof(line));
            Ev_Append(line, n, sZoneNames[sBounties[inside].zone], sizeof(line));
            GfxPrint_SetColor(&printer, 255, 220, 60, 220);
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(line) * 8) / 2, 30, line);
        }
    }
    // Active pouch effects with seconds left (while L isn't held).
    if (V2_ON(ID_V2_POUCH) && !CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_L)) {
        n = 0;
        line[0] = '\0';
        if (sCompassTimer > 0) {
            n = Ev_Append(line, n, "Compass ", sizeof(line));
            n = Ev_AppendNum(line, n, (sCompassTimer + FPS - 1) / FPS, sizeof(line));
            n = Ev_Append(line, n, "s  ", sizeof(line));
        }
        if (sHourglassTimer > 0) {
            n = Ev_Append(line, n, "Hourglass ", sizeof(line));
            n = Ev_AppendNum(line, n, (sHourglassTimer + FPS - 1) / FPS, sizeof(line));
            n = Ev_Append(line, n, "s  ", sizeof(line));
        }
        if (sVeilTimer > 0) {
            n = Ev_Append(line, n, "Veil ", sizeof(line));
            n = Ev_AppendNum(line, n, (sVeilTimer + FPS - 1) / FPS, sizeof(line));
            n = Ev_Append(line, n, "s  ", sizeof(line));
        }
        if (sGigaArmed) {
            n = Ev_Append(line, n, "Giga  ", sizeof(line));
        }
        if (sShieldUp) {
            n = Ev_Append(line, n, "Shield  ", sizeof(line));
        }
        if (sBrandArmed) {
            n = Ev_Append(line, n, "Brand", sizeof(line));
        }
        if (n > 0) {
            GfxPrint_SetColor(&printer, 140, 255, 190, 230);
            printer.dList = Ev_BeginLeftAligned(printer.dList);
            Ui_Print(&printer, 17, 209, line);
            printer.dList = Ev_EndAligned(printer.dList);
        }
    }
    if (V2_ON(ID_V2_POUCH)) {
        Input* input = &play->state.input[0];

        if (CHECK_BTN_ALL(input->cur.button, BTN_L) || (sPouch[sPouchSel] > 0) || (sPouchFlash > 0)) {
            n = Ev_Append(line, 0, "L+Down: ", sizeof(line));
            n = Ev_Append(line, n, sPouchNames[sPouchSel], sizeof(line));
            n = Ev_Append(line, n, " x", sizeof(line));
            n = Ev_AppendNum(line, n, sPouch[sPouchSel], sizeof(line));
            if ((sPouchSel == PI_GIGA) && sGigaArmed) {
                n = Ev_Append(line, n, " ARMED", sizeof(line));
            }
            if ((sPouchSel == PI_SHIELD) && sShieldUp) {
                n = Ev_Append(line, n, " UP", sizeof(line));
            }
            GfxPrint_SetColor(&printer, (sPouchFlash > 0) ? 255 : 210, (sPouchFlash > 0) ? 255 : 200, 255, 255);
            printer.dList = Ev_BeginLeftAligned(printer.dList);
            Ui_Print(&printer, 17, 221, line);
            if (CHECK_BTN_ALL(input->cur.button, BTN_L) && (Pouch_OwnedCount() > 0)) {
                s32 m = Pouch_OwnedCount();
                s32 row = 0;
                s32 k;

                for (k = 0; k < PI_COUNT; k++) {
                    if (sPouch[k] == 0) {
                        continue;
                    }
                    n = Ev_Append(line, 0, (k == sPouchSel) ? "> " : "  ", sizeof(line));
                    n = Ev_Append(line, n, sPouchNames[k], sizeof(line));
                    n = Ev_Append(line, n, " x", sizeof(line));
                    Ev_AppendNum(line, n, sPouch[k], sizeof(line));
                    if (k == sPouchSel) {
                        GfxPrint_SetColor(&printer, 255, 230, 120, 255);
                    } else {
                        GfxPrint_SetColor(&printer, 190, 190, 220, 255);
                    }
                    Ui_Print(&printer, 17, 215 - 10 * m + 10 * row, line);
                    row++;
                }
            }
            printer.dList = Ev_EndAligned(printer.dList);
        }
    }
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    return gfx;
}

// ---------------------------------------------------------------------------
// Menu rows and actions
// ---------------------------------------------------------------------------

// Moon Services: one-off purchases that act on the other systems.
enum {
    SV_REFRESH, SV_REROLL, SV_SHED, SV_CONTRACTS, SV_RARE, SV_LURE, SV_BANISH, SV_BAIT,
    SV_LULL, SV_LUCKY, SV_MERCY, SV_WARD, SV_SIGHT, SV_GAMBLE, SV_FSHIELD, SV_FORESIGHT, SV_WEAKEN, SV_COUNT
};
static const char* sSvNames[SV_COUNT] = {
    "New Bounties", "Reroll Curse", "Shed a Mutator", "New Contracts", "Rare Bounty", "Nemesis Lure",
    "Banish Nemesis", "Bounty Bait", "Moon Lull", "Lucky Moon", "Moon's Mercy", "Moon Ward",
    "Hunter's Sight", "Moon Gamble", "Friend Shield", "Foresight", "Weaken Nemesis",
};
static const char* sSvDescs[SV_COUNT] = {
    "Replace the bounties you haven't taken.",
    "Roll a new Cycle Curse.",
    "Remove your most recent mutator.",
    "Replace all three contracts.",
    "Post a 4-star rare bounty (double pay).",
    "Your Nemesis and taken bounties come to your next area.",
    "Removes your Nemesis. No moon pick this cycle.",
    "Taken bounties move to the next area you enter.",
    "Push the next event back 3 minutes.",
    "Your next bounty claim pays double.",
    "Cancels the next random event before it starts.",
    "Skip your next mutator draft.",
    "Tracker points to bounties (this cycle).",
    "Bet 20 for a 1 in 3 shot at 55.",
    "Blocks your friend's next mean move. Hold 3.",
    "Shows your next event and locks it in.",
    "Strips one trait from your Nemesis.",
};
// Balance: nothing bought here pays back more than it costs on its own.
// V3 prices. Retired services (0) are hidden: they overlapped other things.
//                                   REF REROLL SHED CONTR RARE LURE BANISH BAIT LULL LUCKY MERCY WARD SIGHT GAMBLE FSHIELD FORE WEAKEN
static const u8 sSvPrice[SV_COUNT] = { 25, 90,  150, 20,   0,   60,  250,   0,   0,   0,    0,    0,   0,    0,     60,     50,  80 };

s32 V2_ServiceHidden(s32 sv) {
    return (sv < 0) || (sv >= SV_COUNT) || (sSvPrice[sv] == 0);
}
_Static_assert(PI_COUNT + SV_COUNT <= 32, "shop IDs");
#define UPGRADE_PRICE 25 // +1 star is +10 base pay (more with rank/bonus), plus a trait and more health

s32 V2_ShopItemCount(void) {
    return PI_COUNT;
}

s32 V2_ShopServiceCount(void) {
    return SV_COUNT;
}

// Only taken bounties: untaken ones get replaced at dawn, which would waste it.
s32 V2_BountyUpgradable(s32 i) {
    return V2_BountyActive(i) && (sBounties[i].kind == BK_NORMAL) && (sBounties[i].stars < 5) &&
           sBounties[i].accepted;
}
#define BAN_PRICE 75
#define CALL_PRICE 25

// At the board, or anywhere with Remote Board on.
static s32 V2_AtBoard(void) {
    return gMenuAtBoard || V2_ON(ID_V2_REMOTE);
}

static s32 V2_Pay(s32 price) {
    if (!V2_AtBoard()) {
        Menu_ShowToast("Trade at the South Clock Town board.");
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return false;
    }
    if (sMarks < price) {
        Menu_ShowToast("Not enough Moon Marks.");
        Audio_PlaySfx(NA_SE_SY_ERROR);
        return false;
    }
    sMarks -= price;
    Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    return true;
}

static char sV2Label[40];

s32 gEvDetail = 0;

// The k-th bounty you've taken (slot), or -1.
s32 V2_TakenBounty(s32 k) {
    s32 i;

    for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
        if ((sBounties[i].pool >= 0) && sBounties[i].accepted && (k-- == 0)) {
            return i;
        }
    }
    return -1;
}

static s32 Link_FriendsOnline(void);
static const char* Link_FriendLabel(s32 row);
static const char* Link_FriendValue(s32 row, char* out, s32 maxLen, u8* r, u8* g, u8* b);
static void Link_FriendCycle(s32 row);
static const char* Events_FlavorOf(s32 ev);

const char* V2_Label(s32 id) {
    s32 n;

    if ((id >= ID_LNK_FR_FIRST) && (id <= ID_LNK_FR_LAST)) {
        return Link_FriendLabel(id - ID_LNK_FR_FIRST);
    }
    if ((id >= ID_OV_BNT_FIRST) && (id < ID_OV_BNT_FIRST + 3)) {
        s32 slot = V2_TakenBounty(id - ID_OV_BNT_FIRST);

        if (slot < 0) {
            return "-";
        }
        World_BountyName(&sBounties[slot], sV2Label, sizeof(sV2Label));
        return sV2Label;
    }
    switch (id) {
        case ID_OV_NEM: return "Nemesis";
        case ID_OV_CONTRACTS: return "Contracts";
        case ID_OV_MUTS: return "Mutators";
        case ID_OV_FRIENDS: return "Friends online";
        case ID_EVD_TIER: return "Difficulty";
        case ID_EVD_HOW: return "How to beat it";
        case ID_V2_NEWS: return "World News Pop-ups";
        case ID_V2_BANNERS: return "Big Banners";
        case ID_SUB_V2: return "Moon Marks";
        case ID_SUB_V2_LINK: return "Friend Link";
        case ID_SUB_V2_BOUNTY: return "Bounties & Contracts";
        case ID_SUB_V2_SHOP: return "Moon Shop";
        case ID_SUB_V2_BAN: return "Ban an Event (20)";
        case ID_SUB_V2_CALL: return "Call an Event (15)";
        case ID_SUB_V2_NEM: return "Nemesis";
        case ID_SUB_V2_MUTS: return "Active Mutators";
        case ID_SUB_V2_ITEMS: return "Moon Pouch Items";
        case ID_SUB_V2_SERVICES: return "Moon Services";
        case ID_V2_MUT: return "Mutator Deck";
        case ID_V2_BOUNTY: return "Bounty Board";
        case ID_V2_NEM: return "Nemesis";
        case ID_V2_POUCH: return "Moon Pouch";
        case ID_V2_NEMDIFF: return "Nemesis Difficulty";
        case ID_V2_MARKS: return "Moon Marks";
        case ID_V2_SAVECODE: return "Save Code";
        case ID_V2_AUTOSAVE: return "Auto-Save";
        case ID_V2_DRAFT_A: return (sOfferA >= 0) ? sMutNames[sOfferA] : "-";
        case ID_V2_DRAFT_B: return (sOfferB >= 0) ? sMutNames[sOfferB] : "-";
        case ID_V2_DRAFT_REROLL: return "Reroll both (15 Marks)";
        case ID_V2_TRACKER: return "Bounty Compass";
        case ID_V2_STREAK: return "Hunt Streak";
        case ID_V2_RANK: return "Hunter Rank";
        case ID_V2_REMOTE: return "Remote Board";
        case ID_V2_LINK: return "Friend Link";
        case ID_V2_LINKSTATE: return "Link Status";
        case ID_V2_BELITE: return "Trait: Meteor Caller";
        default: break;
    }
    if ((id >= ID_V2_BNT_FIRST) && (id < ID_V2_BNT_FIRST + V2_BOUNTY_SLOTS)) {
        Bounty* b = &sBounties[id - ID_V2_BNT_FIRST];

        // "+" marks the ones you've taken; stars (or WANTED/RARE) are on the right.
        n = Ev_Append(sV2Label, 0, b->accepted ? "+" : "", sizeof(sV2Label));
        Ev_Append(sV2Label, n, (b->pool >= 0) ? Pool_Name(b->pool) : "-", sizeof(sV2Label));
        return sV2Label;
    }
    if ((id >= ID_V2_CON_FIRST) && (id < ID_V2_CON_FIRST + 3)) {
        return sContractNames[sContracts[id - ID_V2_CON_FIRST].type % CT_TYPES];
    }
    if ((id >= ID_V2_BINFO_FIRST) && (id < ID_V2_BINFO_FIRST + 5)) {
        static const char* sInfoLabels[5] = { "Target", "Difficulty", "Location", "Moves in", "Reward" };

        return sInfoLabels[id - ID_V2_BINFO_FIRST];
    }
    if ((id >= ID_V2_BTRAIT_FIRST) && (id < ID_V2_BTRAIT_FIRST + TR_COUNT)) {
        n = Ev_Append(sV2Label, 0, "Trait: ", sizeof(sV2Label));
        Ev_Append(sV2Label, n, sTraitNames[id - ID_V2_BTRAIT_FIRST], sizeof(sV2Label));
        return sV2Label;
    }
    if (id == ID_V2_BACCEPT) {
        return sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS].accepted ? "Drop this bounty" : "Accept this bounty";
    }
    if (id == ID_V2_BBONUS) {
        n = Ev_Append(sV2Label, 0, "Bonus: ", sizeof(sV2Label));
        Ev_Append(sV2Label, n, sBonusNames[sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS].bonus % BONUS_COUNT],
                  sizeof(sV2Label));
        return sV2Label;
    }
    if (id == ID_V2_BUPGRADE) {
        n = Ev_Append(sV2Label, 0, "Add a star (", sizeof(sV2Label));
        n = Ev_AppendNum(sV2Label, n, UPGRADE_PRICE, sizeof(sV2Label));
        Ev_Append(sV2Label, n, ")", sizeof(sV2Label));
        return sV2Label;
    }
    if ((id >= ID_V2_SHOP_FIRST) && (id < ID_V2_SHOP_FIRST + PI_COUNT + SV_COUNT)) {
        s32 k = id - ID_V2_SHOP_FIRST;
        s32 price = (k < PI_COUNT) ? sPouchPrice[k] : sSvPrice[k - PI_COUNT];

        // V3: "Giga Blade  x2"; the price sits on the right.
        n = Ev_Append(sV2Label, 0, (k < PI_COUNT) ? sPouchNames[k] : sSvNames[k - PI_COUNT], sizeof(sV2Label));
        if ((k < PI_COUNT) && (sPouch[k] > 0)) {
            n = Ev_Append(sV2Label, n, "  x", sizeof(sV2Label));
            Ev_AppendNum(sV2Label, n, sPouch[k], sizeof(sV2Label));
        }
        (void)price;
        return sV2Label;
    }
    if ((id >= ID_V2_BAN_FIRST) && (id < ID_V2_BAN_FIRST + EV_COUNT)) {
        s32 ev = id - ID_V2_BAN_FIRST;

        n = Ev_Append(sV2Label, 0, (sBanMask & (1ull << ev)) ? "[banned] " : "", sizeof(sV2Label));
        Ev_Append(sV2Label, n, Events_ShortName(ev), sizeof(sV2Label));
        return sV2Label;
    }
    if ((id >= ID_V2_CALL_FIRST) && (id < ID_V2_CALL_FIRST + EV_COUNT)) {
        return Events_Name(id - ID_V2_CALL_FIRST);
    }
    if ((id >= ID_V2_NEMROW_FIRST) && (id < ID_V2_NEMROW_FIRST + 12)) {
        static const char* sRows[12] = { "Name", "Enemy",      "Level", "Traits", "Nature", "Zone",
                                         "Free points", "Beaten you", "Speed", "Power", "Health", "Tracking" };

        return sRows[id - ID_V2_NEMROW_FIRST];
    }
    if ((id >= ID_V2_MUTROW_FIRST) && (id < ID_V2_MUTROW_FIRST + 6)) {
        s32 k = id - ID_V2_MUTROW_FIRST;

        return (k < sMutCount) ? sMutNames[sMutOrder[k]] : "-";
    }
    return NULL;
}

// Value text for V2 info rows.
const char* V2_Value(s32 id, char* out, s32 maxLen, u8* r, u8* g, u8* b) {
    s32 n;

    out[0] = '\0';
    *r = *g = *b = 255;
    if ((id >= ID_LNK_FR_FIRST) && (id <= ID_LNK_FR_LAST)) {
        return Link_FriendValue(id - ID_LNK_FR_FIRST, out, maxLen, r, g, b);
    }
    if ((id >= ID_OV_BNT_FIRST) && (id < ID_OV_BNT_FIRST + 3)) {
        s32 slot = V2_TakenBounty(id - ID_OV_BNT_FIRST);

        if (slot < 0) {
            return NULL;
        }
        Ev_Append(out, 0, sZoneNames[sBounties[slot].zone % ZONE_COUNT], MIN(maxLen, 18));
        *r = 255;
        *g = 220;
        *b = 90;
        return out;
    }
    switch (id) {
        case ID_OV_NEM:
            if (sNem.pool < 0) {
                Ev_Append(out, 0, "None", maxLen);
                *r = *g = *b = 150;
            } else {
                n = Ev_Append(out, 0, "Lv", maxLen);
                n = Ev_AppendNum(out, n, sNem.level, maxLen);
                n = Ev_Append(out, n, " ", maxLen);
                Ev_Append(out, n, Pool_Name(sNem.pool), maxLen);
                *g = 110;
                *b = 110;
            }
            return out;
        case ID_OV_CONTRACTS: {
            s32 k;

            n = 0;
            for (k = 0; k < 3; k++) {
                n = Ev_AppendNum(out, n, sContracts[k].progress, maxLen);
                n = Ev_Append(out, n, "/", maxLen);
                n = Ev_AppendNum(out, n, sContractGoal[sContracts[k].type % CT_TYPES], maxLen);
                n = Ev_Append(out, n, (k < 2) ? " " : "", maxLen);
            }
            return out;
        }
        case ID_OV_MUTS:
            n = Ev_AppendNum(out, 0, sMutCount, maxLen);
            n = Ev_Append(out, n, "/6", maxLen);
            if (sMutCount > 0) {
                n = Ev_Append(out, n, " (+", maxLen);
                n = Ev_AppendNum(out, n, sMutCount * 10, maxLen);
                Ev_Append(out, n, "% Marks)", maxLen);
            }
            *g = 170;
            *b = 170;
            return out;
        case ID_OV_FRIENDS:
            Ev_AppendNum(out, 0, Link_FriendsOnline(), maxLen);
            *r = 255;
            *g = 255;
            *b = 170;
            return out;
        case ID_EVD_TIER: {
            static const char* sTierNames[4] = { "Mild", "Medium", "Hard", "Brutal" };
            s32 t = Ev_Tier(gEvDetail) & 3;

            Ev_Append(out, 0, sTierNames[t], maxLen);
            *r = (t >= 2) ? 255 : 160;
            *g = (t >= 3) ? 90 : 220;
            *b = 120;
            return out;
        }
        default:
            break;
    }
    if (id == ID_V2_MARKS) {
        Ev_Itoa(out, sMarks);
        *r = 200;
        *g = 160;
        return out;
    }
    if ((id >= ID_V2_SHOP_FIRST) && (id < ID_V2_SHOP_FIRST + PI_COUNT + SV_COUNT)) {
        // V3: the price, gold if you can afford it, red if you can't.
        s32 k = id - ID_V2_SHOP_FIRST;
        s32 price = (k < PI_COUNT) ? sPouchPrice[k] : sSvPrice[k - PI_COUNT];
        s32 held = 0;
        const char* word = NULL;

        if (k >= PI_COUNT) {
            switch (k - PI_COUNT) {
                case SV_FSHIELD: held = sFriendShields; break;
                case SV_FORESIGHT: word = sForesight ? "Seen " : NULL; break;
                case SV_LURE: word = (sNemLure || sBountyBait) ? "Set " : NULL; break;
                default: break;
            }
        }
        n = 0;
        if (held > 0) {
            n = Ev_Append(out, 0, "x", maxLen);
            n = Ev_AppendNum(out, n, held, maxLen);
            n = Ev_Append(out, n, "  ", maxLen);
        } else if (word != NULL) {
            n = Ev_Append(out, 0, word, maxLen);
        }
        Ev_AppendNum(out, n, price, maxLen);
        if (sMarks >= price) {
            *r = 255;
            *g = 220;
            *b = 110;
        } else {
            *r = 255;
            *g = 80;
            *b = 80;
        }
        return out;
    }
    if (false) {
        s32 held = 0;
        const char* word = NULL;

        switch (id - ID_V2_SHOP_FIRST - PI_COUNT) {
            case SV_WARD: held = sWardCharges; break;
            case SV_MERCY: held = sMercyCharges; break;
            case SV_FSHIELD: held = sFriendShields; break;
            case SV_FORESIGHT: word = sForesight ? "Seen" : NULL; break;
            case SV_LUCKY: word = (sLuckyCharges > 0) ? "Ready" : NULL; break;
            case SV_SIGHT: word = sHunterSight ? "On" : NULL; break;
            case SV_LURE: word = sNemLure ? "Set" : NULL; break;
            case SV_BAIT: word = sBountyBait ? "Set" : NULL; break;
            default: break;
        }
        if (held > 0) {
            n = Ev_Append(out, 0, "x", maxLen);
            Ev_AppendNum(out, n, held, maxLen);
        } else if (word != NULL) {
            Ev_Append(out, 0, word, maxLen);
        } else {
            return NULL;
        }
        *r = 120;
        *b = 120;
        return out;
    }
    if (id == ID_V2_LINKSTATE) {
        return Link_StateValue(out, maxLen, r, g, b);
    }
    if (id == ID_V2_RANK) {
        Ev_Append(out, 0, sRankNames[Rank_Of(sBountiesDone)], maxLen);
        *b = 120;
        return out;
    }
    if (id == ID_V2_BBONUS) {
        Bounty* bt = &sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS];

        Ev_Append(out, 0, "+50%", maxLen);
        *r = 120;
        *b = 120;
        (void)bt;
        return out;
    }
    if (id == ID_V2_STREAK) {
        if (sStreakPaid) {
            Ev_Append(out, 0, "Paid", maxLen);
            *r = 120;
            *b = 120;
        } else {
            n = Ev_AppendNum(out, 0, MIN(sHuntStreak, 3), maxLen);
            Ev_Append(out, n, "/3", maxLen);
            *b = 150;
        }
        return out;
    }
    if ((id >= ID_V2_BNT_FIRST) && (id < ID_V2_BNT_FIRST + V2_BOUNTY_SLOTS)) {
        Bounty* bt = &sBounties[id - ID_V2_BNT_FIRST];
        s32 k;

        if (bt->kind == BK_WANTED) {
            Ev_Append(out, 0, "WANTED", maxLen);
            *g = 90;
            *b = 50;
        } else if (bt->kind == BK_RARE) {
            Ev_Append(out, 0, "RARE", maxLen);
            *r = 200;
            *g = 130;
        } else if (bt->kind == BK_FRIEND) {
            Ev_Append(out, 0, "FRIEND", maxLen);
            *r = 90;
            *g = 230;
            *b = 230;
        } else {
            for (k = 0; (k < bt->stars) && (k < maxLen - 1); k++) {
                out[k] = '*';
            }
            out[k] = '\0';
            *g = 220;
            *b = 60;
            if (!bt->accepted) {
                *r = *g = *b = 150;
            }
        }
        return out;
    }
    if ((id >= ID_V2_BINFO_FIRST) && (id < ID_V2_BINFO_FIRST + 5)) {
        Bounty* bt = &sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS];
        s32 k;

        *r = 255;
        *g = 220;
        *b = 120;
        switch (id - ID_V2_BINFO_FIRST) {
            case 0: Ev_Append(out, 0, Pool_Name(bt->pool), maxLen); break;
            case 1:
                for (k = 0; (k < bt->stars) && (k < maxLen - 1); k++) {
                    out[k] = '*';
                }
                out[k] = '\0';
                break;
            case 2: Ev_Append(out, 0, sZoneNames[bt->zone % ZONE_COUNT], maxLen); break;
            case 3: Ev_FormatTime(out, bt->migrate); break;
            case 4:
                n = Ev_AppendNum(out, 0, Bounty_Pay(bt), maxLen);
                Ev_Append(out, n, (sLuckyCharges > 0) ? " x2" : " Marks", maxLen);
                break;
        }
        return out;
    }
    if ((id >= ID_V2_CON_FIRST) && (id < ID_V2_CON_FIRST + 3)) {
        Contract* c = &sContracts[id - ID_V2_CON_FIRST];

        n = Ev_AppendNum(out, 0, c->progress, maxLen);
        n = Ev_Append(out, n, "/", maxLen);
        Ev_AppendNum(out, n, sContractGoal[c->type % CT_TYPES], maxLen);
        *r = 150;
        *b = 150;
        return out;
    }
    if ((id >= ID_V2_NEMROW_FIRST) && (id < ID_V2_NEMROW_FIRST + 12)) {
        s32 k = id - ID_V2_NEMROW_FIRST;

        if (sNem.pool < 0) {
            if (k == 0) {
                Ev_Append(out, 0, "None yet", maxLen);
            }
            *r = *g = *b = 150;
            return out;
        }
        *g = 110;
        *b = 110;
        switch (k) {
            case 0: Nem_Name(out, maxLen); break;
            case 1: Ev_Append(out, 0, Pool_Name(sNem.pool), maxLen); break;
            case 2:
                n = Ev_AppendNum(out, 0, sNem.level, maxLen);
                n = Ev_Append(out, n, " / ", maxLen);
                Ev_AppendNum(out, n, NEM_LEVEL_MAX, maxLen);
                break;
            case 4:
                n = Ev_Append(out, 0, sNem.style ? "Shadower" : "Charger", maxLen);
                if (sNem.scarred) {
                    n = Ev_Append(out, n, ", scarred", maxLen);
                }
                if (sNem.dormant > 0) {
                    Ev_Append(out, n, ", resting", maxLen);
                }
                break;
            case 8:
            case 9:
            case 10:
            case 11:
                Ev_AppendNum(out, 0, sNemStat[k - 8], maxLen);
                if (sNemStat[k - 8] >= 10) {
                    *r = 255;
                    *g = 230;
                    *b = 120;
                }
                break;
            case 3: {
                u16 tr = Nem_Traits();
                s32 idx = 0;
                s32 t;

                n = 0;
                for (t = 0; t < TR_COUNT; t++) {
                    if (tr & (1 << t)) {
                        if ((idx / 2) == (k - 3)) {
                            n = Ev_Append(out, n, (n > 0) ? ", " : "", maxLen);
                            n = Ev_Append(out, n, sTraitNames[t], maxLen);
                        }
                        idx++;
                    }
                }
                break;
            }
            case 5: Ev_Append(out, 0, sZoneNames[sNem.zone % ZONE_COUNT], maxLen); break;
            case 6:
                Ev_AppendNum(out, 0, Nem_PointsFree(), maxLen);
                break;
            case 7: Ev_AppendNum(out, 0, sNem.wins, maxLen); break;
        }
        return out;
    }
    return NULL;
}

s32 V2_NemStatValue(s32 k) {
    return ((sNem.pool >= 0) && (k >= 0) && (k < NS_COUNT)) ? sNemStat[k] : -1;
}

// Footer text while a V2 row is highlighted.
const char* V2_Desc(s32 id) {
    static char sDesc[64];
    s32 n;

    if ((id >= ID_LNK_FR_FIRST) && (id <= ID_LNK_FR_LAST)) {
        return "A: Full (anything), Helpful (only nice things), Blocked (nothing, and they see the moon's wrath).";
    }
    if ((id >= ID_OV_BNT_FIRST) && (id < ID_OV_BNT_FIRST + 3)) {
        s32 slot = V2_TakenBounty(id - ID_OV_BNT_FIRST);

        if (slot >= 0) {
            n = Ev_Append(sDesc, 0, sMotiveNames[sBounties[slot].motive % MOTIVE_COUNT], sizeof(sDesc));
            n = Ev_Append(sDesc, n, ". Reward ", sizeof(sDesc));
            n = Ev_AppendNum(sDesc, n, Bounty_Pay(&sBounties[slot]), sizeof(sDesc));
            Ev_Append(sDesc, n, " Marks.", sizeof(sDesc));
            return sDesc;
        }
    }
    switch (id) {
        case ID_OV_NEM: return "Your Nemesis. Its page has its stats and traits.";
        case ID_OV_CONTRACTS: return "Progress on your three contracts.";
        case ID_OV_MUTS: return "Each active mutator adds 10% to your Marks.";
        case ID_OV_FRIENDS: return "Friends with the web panel open right now.";
        case ID_EVD_TIER: return "Sets its damage, its enemies and what it pays.";
        case ID_EVD_HOW: return Events_FlavorOf(gEvDetail);
        case ID_V2_NEWS: return "Pop-ups when your bounties or Nemesis move.";
        case ID_V2_BANNERS: return "Big banners for big moments (off: pop-ups).";
        case ID_SUB_V2: return "Bounties, contracts, the Moon Shop and your pouch.";
        case ID_SUB_V2_LINK: return "Let a friend mess with your game from the web.";
        case ID_SUB_V2_NEM: return "Your Nemesis, its stats and its settings.";
        case ID_V2_MUT: return "Draft a penalty mutator each dawn and dusk.";
        case ID_V2_BOUNTY: return "Bounties, contracts and Moon Marks.";
        case ID_V2_NEM: return "An enemy that bests you becomes your Nemesis.";
        case ID_V2_POUCH: return "Hold L. Left/Right picks, Down uses (D-pad or C).";
        case ID_V2_NEMDIFF: return "Scales how tough your Nemesis gets.";
        case ID_V2_MARKS: return "Earned from bounties, contracts and Nemeses.";
        case ID_V2_SAVECODE: return "Copy or paste your save code (Marks, Nemesis).";
        case ID_V2_AUTOSAVE: return "Keep Marks, pouch and Nemesis with each file.";
        case ID_SUB_V2_SHOP: return "Spend your Moon Marks.";
        case ID_SUB_V2_ITEMS: return "Items for your Moon Pouch.";
        case ID_SUB_V2_SERVICES: return "Favors that bend bounties and events.";
        case ID_SUB_V2_BAN: return "Stop an event for the rest of this cycle.";
        case ID_SUB_V2_CALL: return "Start any event now.";
        case ID_V2_DRAFT_A: return (sOfferA >= 0) ? sMutDescs[sOfferA] : NULL;
        case ID_V2_DRAFT_B: return (sOfferB >= 0) ? sMutDescs[sOfferB] : NULL;
        case ID_V2_DRAFT_REROLL: return "Swap both offers for two new ones.";
        case ID_V2_TRACKER: return "Arrow to a bounty you took, when it's near.";
        case ID_V2_REMOTE: return "Take bounties and shop from anywhere.";
        case ID_V2_LINK: return "Friends' web panels can mess with you. Set each friend below.";
        case ID_V2_LINKSTATE: return Link_StateDesc();
        case ID_V2_STREAK: return "Claim 3 in a cycle: bonus Marks + a rare bounty.";
        case ID_V2_BELITE: return "Drops bombs from the sky near you every 10s.";
        case ID_V2_BBONUS: return sBonusDescs[sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS].bonus % BONUS_COUNT];
        case ID_V2_RANK: {
            s32 r = Rank_Of(sBountiesDone);

            n = Ev_AppendNum(sDesc, 0, sBountiesDone, sizeof(sDesc));
            n = Ev_Append(sDesc, n, " claimed, +", sizeof(sDesc));
            n = Ev_AppendNum(sDesc, n, 5 * r, sizeof(sDesc));
            n = Ev_Append(sDesc, n, "% pay.", sizeof(sDesc));
            if (r < 5) {
                n = Ev_Append(sDesc, n, " Next at ", sizeof(sDesc));
                Ev_AppendNum(sDesc, n, sRankAt[r + 1], sizeof(sDesc));
            }
            return sDesc;
        }
        default: break;
    }
    if ((id >= ID_V2_BNT_FIRST) && (id < ID_V2_BNT_FIRST + V2_BOUNTY_SLOTS)) {
        Bounty* b = &sBounties[id - ID_V2_BNT_FIRST];
        char time[12];

        Ev_FormatTime(time, b->migrate);
        n = Ev_Append(sDesc, 0, sZoneNames[b->zone % ZONE_COUNT], sizeof(sDesc));
        n = Ev_Append(sDesc, n, ", moves in ", sizeof(sDesc));
        n = Ev_Append(sDesc, n, time, sizeof(sDesc));
        Ev_Append(sDesc, n, ". A: details", sizeof(sDesc));
        return sDesc;
    }
    if ((id >= ID_V2_NEMROW_FIRST) && (id < ID_V2_NEMROW_FIRST + 12)) {
        switch (id - ID_V2_NEMROW_FIRST) {
            case 2: return "Every Nemesis can reach level 200. Nothing holds it back.";
            case 4: return "Chargers come straight at you. Shadowers stalk you first.";
            case 5: return "It heads toward wherever you are.";
            case 6: return "Skill points your friend (or time) will spend.";
            case 7: return "Times it has beaten you.";
            case 8: return "Faster. At 10 the bar glows: it lunges at you.";
            case 9: return "Hits harder. At 10: its blows go through your shield.";
            case 10: return "Tougher. At 10: extra health in reserve, and it heals.";
            case 11: return "Travels faster. At 10: follows you through any door.";
            default: return NULL;
        }
    }
    if ((id >= ID_V2_BTRAIT_FIRST) && (id < ID_V2_BTRAIT_FIRST + TR_COUNT)) {
        return sTraitDescs[id - ID_V2_BTRAIT_FIRST];
    }
    if (id == ID_V2_BINFO_FIRST + 3) {
        return "Then it moves to a neighboring area.";
    }
    if (id == ID_V2_BACCEPT) {
        if (!V2_AtBoard()) {
            return "Only at the board (Remote Board is off).";
        }
        return sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS].accepted ? "Stop hunting this one."
                                                                    : "It will show up in its area.";
    }
    if ((id >= ID_V2_CON_FIRST) && (id < ID_V2_CON_FIRST + 3)) {
        Contract* c = &sContracts[id - ID_V2_CON_FIRST];

        n = Ev_Append(sDesc, 0, sContractDescs[c->type % CT_TYPES], sizeof(sDesc));
        n = Ev_Append(sDesc, n, " Pays ", sizeof(sDesc));
        n = Ev_AppendNum(sDesc, n, sContractPay[c->type % CT_TYPES], sizeof(sDesc));
        Ev_Append(sDesc, n, ".", sizeof(sDesc));
        return sDesc;
    }
    if ((id >= ID_V2_SHOP_FIRST) && (id < ID_V2_SHOP_FIRST + PI_COUNT)) {
        return sPouchDescs[id - ID_V2_SHOP_FIRST];
    }
    if ((id >= ID_V2_SHOP_FIRST + PI_COUNT) && (id < ID_V2_SHOP_FIRST + PI_COUNT + SV_COUNT)) {
        return sSvDescs[id - ID_V2_SHOP_FIRST - PI_COUNT];
    }
    if (id == ID_V2_BUPGRADE) {
        return "+10 pay, one more trait. Not while it's out.";
    }
    if ((id >= ID_V2_MUTROW_FIRST) && (id < ID_V2_MUTROW_FIRST + 6)) {
        s32 k = id - ID_V2_MUTROW_FIRST;

        return (k < sMutCount) ? sMutDescs[sMutOrder[k]] : NULL;
    }
    if ((id >= ID_V2_BAN_FIRST) && (id < ID_V2_BAN_FIRST + EV_COUNT)) {
        return Events_Description(id - ID_V2_BAN_FIRST);
    }
    if ((id >= ID_V2_CALL_FIRST) && (id < ID_V2_CALL_FIRST + EV_COUNT)) {
        return Events_Description(id - ID_V2_CALL_FIRST);
    }
    return NULL;
}

// Actions. Returns true if the menu should close.
s32 V2_RunAction(PlayState* play, s32 id) {
    if ((id >= ID_LNK_FR_FIRST) && (id <= ID_LNK_FR_LAST)) {
        Link_FriendCycle(id - ID_LNK_FR_FIRST);
        return false;
    }
    if (id == ID_V2_DRAFT_REROLL) {
        s32 a = sOfferA;
        s32 b = sOfferB;
        s32 na;
        s32 nb;

        // Two new offers, different from the two on the table when possible.
        na = Mut_RandomNew(a);
        if ((na == b) || (na < 0)) {
            na = Mut_RandomNew(b);
        }
        nb = Mut_RandomNew(na);
        if ((nb == a) || (nb == b)) {
            s32 k;

            for (k = 0; k < 8; k++) {
                nb = Mut_RandomNew(na);
                if ((nb != a) && (nb != b)) {
                    break;
                }
            }
        }
        if ((na < 0) || (nb < 0) || ((na == a) && (nb == b))) {
            Menu_ShowToast("Nothing else to offer.");
        } else if (sMarks < DRAFT_REROLL_PRICE) {
            Menu_ShowToast("Not enough Moon Marks.");
            Audio_PlaySfx(NA_SE_SY_ERROR);
        } else {
            sMarks -= DRAFT_REROLL_PRICE; // (drafts open anywhere, so no board needed)
            Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
            sOfferA = na;
            sOfferB = nb;
            Menu_ShowToast("New offers!");
        }
        return false;
    }
    if ((id == ID_V2_DRAFT_A) || (id == ID_V2_DRAFT_B)) {
        s32 m = (id == ID_V2_DRAFT_A) ? sOfferA : sOfferB;

        Mut_Add(m);
        if (sDraftSlot >= 0) {
            sDraftDone |= (1 << sDraftSlot);
        }
        sDraftSlot = -1;
        if (m >= 0) {
            V2_Toast("Mutator: ", -1, sMutNames[m]);
        }
        return true;
    }
    if (id == ID_V2_SAVECODE) {
        sSaveUiWanted = true;
        return true;
    }
    if (id == ID_V2_BACCEPT) {
        id = ID_V2_BNT_FIRST + (gV2InfoBounty % V2_BOUNTY_SLOTS);
    }
    if ((id >= ID_V2_BNT_FIRST) && (id < ID_V2_BNT_FIRST + V2_BOUNTY_SLOTS)) {
        Bounty* b = &sBounties[id - ID_V2_BNT_FIRST];

        if (!V2_AtBoard()) {
            Menu_ShowToast("Take bounties at the Bounty Board.");
            Audio_PlaySfx(NA_SE_SY_ERROR);
        } else {
            b->accepted = !b->accepted;
            b->spawnedVisit = false;
            Menu_ShowToast(b->accepted ? "Bounty accepted." : "Bounty dropped.");
            Audio_PlaySfx(NA_SE_SY_DECIDE);
        }
        return false;
    }
    if (id == ID_V2_BUPGRADE) {
        Bounty* b = &sBounties[gV2InfoBounty % V2_BOUNTY_SLOTS];

        if (!V2_BountyUpgradable(gV2InfoBounty % V2_BOUNTY_SLOTS)) {
            Menu_ShowToast("It can't go any higher.");
        } else if (b->actor != NULL) {
            Menu_ShowToast("Not while it's out hunting.");
        } else if (V2_Pay(UPGRADE_PRICE)) {
            s32 tries;

            b->stars++;
            for (tries = 0; tries < 40; tries++) {
                u16 bit = 1 << Trait_Random();

                if (!(b->traits & bit)) {
                    b->traits |= bit;
                    break;
                }
            }
            Menu_ShowToast("Bounty upgraded: +1 star.");
        }
        return false;
    }
    if ((id >= ID_V2_SHOP_FIRST) && (id < ID_V2_SHOP_FIRST + PI_COUNT)) {
        s32 k = id - ID_V2_SHOP_FIRST;

        if (sPouch[k] + sPouchAmount[k] > sPouchMax[k]) {
            Menu_ShowToast("Your pouch can't hold more.");
        } else if (V2_Pay(sPouchPrice[k])) {
            sPouch[k] += sPouchAmount[k];
            sPouchSel = k;
            V2_Toast("Got: ", -1, sPouchNames[k]);
        }
        return false;
    }
    if ((id >= ID_V2_SHOP_FIRST + PI_COUNT) && (id < ID_V2_SHOP_FIRST + PI_COUNT + SV_COUNT)) {
        s32 sv = id - ID_V2_SHOP_FIRST - PI_COUNT;
        s32 i;

        // Check first, pay after, so a service that can't run costs nothing.
        switch (sv) {
            case SV_REFRESH:
                if (sBounties[0].accepted && sBounties[1].accepted && sBounties[2].accepted) {
                    Menu_ShowToast("You've taken all three.");
                    return false;
                }
                break;
            case SV_FSHIELD:
                if (sFriendShields >= 3) {
                    Menu_ShowToast("You can hold three at most.");
                    return false;
                }
                break;
            case SV_REROLL:
                if (!gOpt[ID_EV_CURSE]) {
                    Menu_ShowToast("Cycle Curse is off.");
                    return false;
                }
                break;
            case SV_SHED:
                if (sMutCount == 0) {
                    Menu_ShowToast("You have no mutators.");
                    return false;
                }
                break;
            case SV_RARE:
                if (sBounties[SLOT_RARE].pool >= 0) {
                    Menu_ShowToast("A rare bounty is already up.");
                    return false;
                }
                break;
            case SV_LURE: {
                s32 any = (sNem.pool >= 0) && V2_ON(ID_V2_NEM);

                for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
                    if ((sBounties[i].pool >= 0) && sBounties[i].accepted) {
                        any = true;
                    }
                }
                if (!any) {
                    Menu_ShowToast("No Nemesis or taken bounty to lure.");
                    return false;
                }
                if (sNemLure || sBountyBait) {
                    Menu_ShowToast("The lure is already set.");
                    return false;
                }
                break;
            }
            case SV_BANISH:
                if (sNem.pool < 0) {
                    Menu_ShowToast("You have no Nemesis.");
                    return false;
                }
                if ((sv == SV_LURE) && !V2_ON(ID_V2_NEM)) {
                    Menu_ShowToast("Nemesis is turned off.");
                    return false;
                }
                if ((sv == SV_LURE) && sNemLure) {
                    Menu_ShowToast("It's already on its way.");
                    return false;
                }
                break;
            case SV_BAIT: {
                s32 any = false;

                for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
                    if ((sBounties[i].pool >= 0) && sBounties[i].accepted) {
                        any = true;
                    }
                }
                if (!any) {
                    Menu_ShowToast("You haven't taken a bounty.");
                    return false;
                }
                if (sBountyBait) {
                    Menu_ShowToast("Bait is already set.");
                    return false;
                }
                break;
            }
            case SV_LULL:
                if (!gOpt[ID_EV_MASTER]) {
                    Menu_ShowToast("Events are turned off.");
                    return false;
                }
                if (sActive >= 0) {
                    Menu_ShowToast("An event is already running.");
                    return false;
                }
                break;
            case SV_LUCKY:
                if (sLuckyCharges >= 1) {
                    Menu_ShowToast("Your luck is already set.");
                    return false;
                }
                break;
            case SV_MERCY:
                if (sMercyCharges >= 2) {
                    Menu_ShowToast("You can hold two at most.");
                    return false;
                }
                break;
            case SV_WARD:
                if (sWardCharges >= 2) {
                    Menu_ShowToast("You can hold two wards at most.");
                    return false;
                }
                break;
            case SV_FORESIGHT:
                if (!gOpt[ID_EV_MASTER]) {
                    Menu_ShowToast("Events are turned off.");
                    return false;
                }
                if (sForesight) {
                    Menu_ShowToast("You already know what's next.");
                    return false;
                }
                if ((sFriendNext < 0) && (Ev_PickRandom(play) < 0)) {
                    Menu_ShowToast("The moon shows nothing yet.");
                    return false;
                }
                break;
            case SV_WEAKEN:
                if ((sNem.pool < 0) || (Nem_Traits() == 0)) {
                    Menu_ShowToast("Your Nemesis has no traits to lose.");
                    return false;
                }
                break;
            case SV_SIGHT:
                if (!V2_ON(ID_V2_TRACKER)) {
                    Menu_ShowToast("Turn on the Bounty Tracker first.");
                    return false;
                }
                if (sHunterSight) {
                    Menu_ShowToast("Already active this cycle.");
                    return false;
                }
                break;
            default:
                break;
        }
        if (!V2_Pay(sSvPrice[sv])) {
            return false;
        }
        switch (sv) {
            case SV_REFRESH:
                for (i = 0; i < 3; i++) {
                    if (!sBounties[i].accepted) {
                        Bounty_New(i); // taken (and upgraded) ones stay
                    }
                }
                Menu_ShowToast("New bounties posted.");
                break;
            case SV_REROLL:
                Curse_Roll();
                break;
            case SV_SHED:
                sMutCount--;
                sMutMask &= ~(1u << sMutOrder[sMutCount]);
                V2_Toast("Shed: ", -1, sMutNames[sMutOrder[sMutCount]]);
                break;
            case SV_CONTRACTS:
                for (i = 0; i < 3; i++) {
                    sContracts[i].type = -1; // so the new ones don't dodge the old types
                }
                for (i = 0; i < 3; i++) {
                    Contract_New(i);
                }
                Menu_ShowToast("New contracts posted.");
                break;
            case SV_RARE:
                Bounty_Fill(SLOT_RARE, sRarePool[V2_Rand(ARRAY_COUNT(sRarePool))], 4, BK_RARE);
                Menu_ShowToast("A rare bounty was posted!");
                break;
            case SV_LURE:
                sNemLure = (sNem.pool >= 0);
                sBountyBait = true;
                Menu_ShowToast("Lure set: they'll come to your next area.");
                break;
            case SV_BANISH:
                if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
                    Ev_Poof(play, &sNem.actor->world.pos, 1);
                    Ev_KillOne(play, sNem.actor);
                }
                sNem.pool = -1;
                sNem.actor = NULL;
                Nem_ClearFriendStuff();
                Nem_ResetStats();
                sNemBanished = true;
                Menu_ShowToast("Your Nemesis is banished.");
                break;
            case SV_BAIT:
                sBountyBait = true;
                Menu_ShowToast("Bait set: they'll follow you.");
                break;
            case SV_LULL:
                sUntilNext += 3 * 60 * FPS;
                Menu_ShowToast("The moon rests a while.");
                break;
            case SV_LUCKY:
                sLuckyCharges = 1;
                Menu_ShowToast("The moon smiles on your next hunt.");
                break;
            case SV_MERCY:
                sMercyCharges++;
                Menu_ShowToast("Moon's Mercy ready.");
                break;
            case SV_WARD:
                sWardCharges++;
                Menu_ShowToast("Moon Ward ready.");
                break;
            case SV_SIGHT:
                sHunterSight = true;
                Menu_ShowToast("Hunter's Sight: follow the arrow.");
                break;
            case SV_GAMBLE:
                if (Rand_ZeroOne() < (1.0f / 3.0f)) { // expected -1.7 per bet: can't be farmed
                    V2_GiveMarks(55, "Moon Gamble won! +");
                } else {
                    Menu_ShowToast("Moon Gamble lost.");
                    Audio_PlaySfx(NA_SE_SY_ERROR);
                }
                break;
            case SV_FSHIELD:
                sFriendShields++;
                Menu_ShowToast("Friend Shield up.");
                break;
            case SV_FORESIGHT:
                // Your friend's pick if they made one, otherwise the moon's pick, decided now.
                if (sFriendNext < 0) {
                    sFriendNext = Ev_PickRandom(play);
                    sFriendCombo = -1;
                }
                sForesight = true;
                V2_Toast("Next event: ", -1, Events_Name(sFriendNext));
                break;
            case SV_WEAKEN:
                if (sNemBonusTraits != 0) {
                    s32 k;

                    for (k = TR_COUNT - 1; k >= 0; k--) {
                        if (sNemBonusTraits & (1 << k)) {
                            sNemBonusTraits &= ~(1 << k);
                            V2_Toast("Your Nemesis lost ", -1, sTraitNames[k]);
                            break;
                        }
                    }
                } else if (sNem.traitCount > 0) {
                    s32 lost = MIN(sNem.traitCount, (Nem_EffLevel() + 1) / 2) - 1; // the newest active one

                    if (lost >= 0) {
                        V2_Toast("Your Nemesis lost ", -1, sTraitNames[sNem.traitOrder[lost]]);
                        // Keep only the ones still active, so nothing dormant slides in (level-ups add new ones).
                        sNem.traitCount = lost;
                    }
                }
                Nem_RefreshLive(play);
                break;
        }
        return false;
    }
    if ((id >= ID_V2_BAN_FIRST) && (id < ID_V2_BAN_FIRST + EV_COUNT)) {
        s32 ev = id - ID_V2_BAN_FIRST;

        if (sBanMask & (1ull << ev)) {
            Menu_ShowToast("Already banned this cycle.");
        } else if (V2_Pay(BAN_PRICE)) {
            sBanMask |= (1ull << ev);
            V2_Toast("Banned: ", -1, Events_Name(ev));
        }
        return false;
    }
    if ((id >= ID_V2_CALL_FIRST) && (id < ID_V2_CALL_FIRST + EV_COUNT)) {
        s32 ev = id - ID_V2_CALL_FIRST;

        if ((sActive >= 0) || (sRouletteTimer > 0)) {
            Menu_ShowToast("An event is already running.");
            return false;
        }
        if (sBanMask & (1ull << ev)) {
            Menu_ShowToast("You banned it this cycle.");
            return false;
        }
        if (!Ev_CanRun(play, ev)) {
            Menu_ShowToast("That event can't run here.");
            return false;
        }
        if (V2_Pay(CALL_PRICE)) {
            sRouletteTimer = 0;
            sCallPending = true;
            Ev_Start(play, ev);
            sCallPending = false;
            return true;
        }
        return false;
    }
    return false;
}
