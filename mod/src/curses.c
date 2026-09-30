#include "mf.h"

// One curse per three-day cycle. Each one bends a different part of the mod.
const CurseDef gCurses[NUM_CURSES] = {
    { "Curse of Haste",         "Events",      "Global Events arrive about every 2 minutes." },
    { "Curse of Frailty",       "Damage",      "Damage from Global Events is increased by 50%." },
    { "Curse of Vendetta",      "Nemesis",     "Your nemesis tracks you twice as fast and gains double stats." },
    { "Curse of the Hunted",    "Bounties",    "Bounty targets hunt you across Termina." },
    { "Curse of Poverty",       "Moon Marks",  "Moon Mark payouts -40% and pouch prices +25%." },
    { "Curse of Promotion",     "Promotion",   "Enemies become nemeses far more easily." },
    { "Curse of the Crowd",     "Friends",     "Your friends' meddling is cheaper and faster." },
    { "Curse of the Sealed Pouch", "Pouch",    "Moon Pouch items need 45 seconds between uses." },
    { "Curse of the Long Night","Duration",    "Global Events last 15 seconds longer." },
    { "Curse of Brittle Bounties", "Contracts","Claimed bounties expire faster and their targets flee." },
};

s32 Curse_Active(s32 id) {
    return (gMf.curseId == id) && !gMf.curseWarded &&
           (gMf.curseCycle == gSaveContext.save.saveInfo.playerData.threeDayResetCount);
}

void Curse_Roll(s32 announce) {
    s32 prev = gMf.curseId;
    s32 id;

    do {
        id = Rng_Range(0, NUM_CURSES - 1);
    } while (id == prev && NUM_CURSES > 1);
    gMf.curseId = id;
    gMf.curseCycle = gSaveContext.save.saveInfo.playerData.threeDayResetCount;
    gMf.curseWarded = false;
    if (announce) {
        Hud_Banner(gCurses[id].name, gCurses[id].desc);
        Mf_Sfx(NA_SE_SY_STALKIDS_PSYCHO);
    }
    World_Logf2("A new cycle. ", gCurses[id].name, " takes hold.");
    Save_MarkDirty();
}

void Curse_Update(PlayState* play) {
    u16 cycle = gSaveContext.save.saveInfo.playerData.threeDayResetCount;

    if (gMf.curseCycle != cycle || gMf.curseId >= NUM_CURSES) {
        // Wait until the player is actually in control so the banner is seen.
        if (gRt.gameplayOk && gRt.framesSinceSceneStart > SEC(2)) {
            Curse_Roll(true);
        }
    }
    if (gMf.curseWarded && gMf.wardDay != CURRENT_DAY) {
        gMf.curseWarded = false;
        World_Log("Dawn breaks. The Curse Ward crumbles to dust.");
    }
}
