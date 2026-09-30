#include "mf.h"

// Moon Marks are the mod's own currency. They only buy Moon Pouch items, which
// are temporary effects: healing, protection, ammo refills for gear you already
// own, and tools that manipulate the mod's own systems. Nothing here gives
// items, rupees, upgrades or movement abilities, so randomizer logic is safe.

enum { PC_SURVIVAL, PC_UTILITY, PC_HUNT, PC_META };

const PouchItemDef gPouchItems[NUM_POUCH_ITEMS] = {
    { "Lunar Tonic",        "Heals 3 hearts.",                                        8, 5, PC_SURVIVAL },
    { "Full Moon Draught",  "Heals you completely.",                                  20, 3, PC_SURVIVAL },
    { "Moonshield",         "8s: all damage is negated.",                             15, 3, PC_SURVIVAL },
    { "Stillwater Charm",   "Ends the current event as survived (half reward).",      18, 3, PC_META },
    { "Omen Scroll",        "Reveals the next event and delays it by 60s.",           6, 5, PC_META },
    { "Nemesis Lure",       "Your nemesis comes to you right now.",                   12, 3, PC_HUNT },
    { "Smoke Veil",         "Your nemesis loses your trail for a while.",             14, 3, PC_HUNT },
    { "Bounty Compass",     "60s: points to claimed bounties in this area.",          6, 5, PC_HUNT },
    { "Hunter's Whetstone", "90s: +50% damage to nemeses and bounty targets.",        16, 3, PC_HUNT },
    { "Moonsteel Ward",     "60s: all damage you take is halved.",                    20, 3, PC_SURVIVAL },
    { "Second Wind",        "Passive: survive one lethal blow with 4 hearts.",        30, 2, PC_SURVIVAL },
    { "Magic Flask",        "Refills magic (if you have a magic meter).",             10, 3, PC_UTILITY },
    { "Quiver Refill",      "Refills arrows (if you own the bow).",                   8, 3, PC_UTILITY },
    { "Bomb Satchel",       "Refills bombs and bombchus you already carry.",          8, 3, PC_UTILITY },
    { "Deku Bundle",        "Refills Deku sticks and nuts you already carry.",        5, 3, PC_UTILITY },
    { "Curse Ward",         "Suppresses this cycle's curse until the next dawn.",     35, 2, PC_META },
    { "Mark Magnet",        "Your next 3 Moon Mark payouts are +50%.",                15, 3, PC_META },
    { "Moon Spyglass",      "Reveals where your nemesis is, all cycle long.",       4, 3, PC_META },
    { "Stasis Orb",         "6s: every enemy nearby is frozen in place.",             22, 3, PC_SURVIVAL },
    { "Riposte Charm",      "60s: enemies that hit you are hurt in return.",          16, 3, PC_HUNT },
    { "Event Tempter",      "Starts a random event now, +50% reward.",                5, 5, PC_META },
    { "Nemesis Brand",      "The next enemy you hit becomes your nemesis.",           25, 1, PC_HUNT },
    { "Bounty Reroll",      "Replaces every unclaimed bounty on the board.",          5, 5, PC_META },
    { "Heart Siphon",       "45s: every hit you land heals a quarter heart.",         18, 3, PC_SURVIVAL },
};

s32 gBuffTimers[NUM_POUCH_ITEMS];
static s32 sSealedCooldown = 0;
static s32 sTempterBonus = 0;

s32 Pouch_BuffActive(s32 item) {
    return gBuffTimers[item] > 0;
}

s32 Marks_Scale(s32 amount) {
    if (amount <= 0) {
        return 0;
    }
    if (Curse_Active(CURSE_POVERTY)) {
        amount = amount * 60 / 100;
    }
    if (gMf.markMagnet > 0) {
        amount = amount * 3 / 2;
        gMf.markMagnet--;
    }
    return MF_MAX(1, amount);
}

void Marks_Add(s32 amount, const char* reason) {
    char buf[64];
    MfColor c = { 190, 170, 255, 255 };

    if (amount <= 0) {
        return;
    }
    gMf.moonMarks += amount;
    gMf.lifetimeMarks += amount;
    buf[0] = '\0';
    Str_Cat(buf, "+", sizeof(buf));
    Str_CatInt(buf, amount, sizeof(buf));
    Str_Cat(buf, " Moon Marks", sizeof(buf));
    if (reason != NULL) {
        Str_Cat(buf, ": ", sizeof(buf));
        Str_Cat(buf, reason, sizeof(buf));
    }
    Hud_Notify(buf, c);
    Mf_Sfx(NA_SE_SY_GET_RUPY);
    Save_MarkDirty();
}

s32 Marks_Spend(s32 amount) {
    if ((s32)gMf.moonMarks < amount) {
        return false;
    }
    gMf.moonMarks -= amount;
    Save_MarkDirty();
    return true;
}

s32 Pouch_Price(s32 item) {
    s32 price = gPouchItems[item].cost;
    if (Curse_Active(CURSE_POVERTY)) {
        price = price * 5 / 4;
    }
    return price;
}

void Pouch_Give(s32 item, s32 count) {
    s32 n = gMf.pouch[item] + count;
    gMf.pouch[item] = (u8)MF_MIN(n, gPouchItems[item].maxStack);
    Save_MarkDirty();
}

s32 Pouch_Buy(s32 item) {
    if (gMf.pouch[item] >= gPouchItems[item].maxStack) {
        return false;
    }
    if (!Marks_Spend(Pouch_Price(item))) {
        return false;
    }
    Pouch_Give(item, 1);
    return true;
}

static s32 RefillAmmo(ItemId item, s32 upg) {
    if (INV_CONTENT(item) != item) {
        return false;
    }
    AMMO(item) = CUR_CAPACITY(upg);
    return true;
}

// Returns true if the item was consumed.
static s32 ApplyItem(PlayState* play, s32 item) {
    MfColor warn = { 255, 150, 150, 255 };

    switch (item) {
        case PI_LUNAR_TONIC:
            Mf_HealPlayer(12);
            return true;
        case PI_FULL_MOON_DRAUGHT:
            Mf_HealPlayer(80);
            return true;
        case PI_MOONSHIELD:
            gBuffTimers[item] = SEC(8);
            return true;
        case PI_STILLWATER_CHARM:
            if (gEv.id < 0) {
                Hud_Notify("No event to calm right now.", warn);
                return false;
            }
            gEv.friendBonus -= 50; // half reward
            Events_Stop(play, true);
            return true;
        case PI_OMEN_SCROLL: {
            gEv.nextIn += SEC(60);
            Hud_Notify("The omen is written. The next event is delayed by a minute.", warn);
            return true;
        }
        case PI_NEMESIS_LURE:
            if (gMf.nemesis.state == NEM_NONE) {
                Hud_Notify("You have no nemesis to lure.", warn);
                return false;
            }
            Nemesis_Summon(play);
            return true;
        case PI_SMOKE_VEIL:
            if (gMf.nemesis.state == NEM_NONE) {
                Hud_Notify("You have no nemesis to hide from.", warn);
                return false;
            }
            Actors_KillTagged(TAG_NEMESIS);
            gMf.nemesis.zone = World_RandomNeighbour(World_RandomNeighbour(gMf.nemesis.zone));
            gMf.nemesis.state = NEM_DORMANT;
            gMf.nemesis.dormantSecs = 180;
            World_Log("Smoke veils your trail. Your nemesis loses the scent.");
            return true;
        case PI_BOUNTY_COMPASS:
            gCompassTimer = SEC(60);
            return true;
        case PI_HUNTERS_WHETSTONE:
            gBuffTimers[item] = SEC(90);
            return true;
        case PI_MOONSTEEL_WARD:
            gBuffTimers[item] = SEC(60);
            return true;
        case PI_SECOND_WIND:
            Hud_Notify("Second Wind works on its own when you would fall.", warn);
            return false;
        case PI_MAGIC_FLASK:
            if (!gSaveContext.save.saveInfo.playerData.isMagicAcquired) {
                Hud_Notify("You have no magic meter.", warn);
                return false;
            }
            Magic_Add(play, 0x60);
            return true;
        case PI_QUIVER_REFILL:
            if (!RefillAmmo(ITEM_BOW, UPG_QUIVER)) {
                Hud_Notify("You don't own a bow.", warn);
                return false;
            }
            return true;
        case PI_BOMB_SATCHEL: {
            s32 a = RefillAmmo(ITEM_BOMB, UPG_BOMB_BAG);
            s32 b = RefillAmmo(ITEM_BOMBCHU, UPG_BOMB_BAG);
            if (!a && !b) {
                Hud_Notify("You don't carry bombs.", warn);
                return false;
            }
            return true;
        }
        case PI_DEKU_BUNDLE: {
            s32 a = RefillAmmo(ITEM_DEKU_STICK, UPG_DEKU_STICKS);
            s32 b = RefillAmmo(ITEM_DEKU_NUT, UPG_DEKU_NUTS);
            if (!a && !b) {
                Hud_Notify("You don't carry sticks or nuts.", warn);
                return false;
            }
            return true;
        }
        case PI_CURSE_WARD:
            gMf.curseWarded = true;
            gMf.wardDay = CURRENT_DAY;
            World_Log("A Curse Ward flickers to life. The curse sleeps until dawn.");
            return true;
        case PI_MARK_MAGNET:
            gMf.markMagnet += 3;
            return true;
        case PI_CHRONICLE_SPYGLASS:
            gMf.spyglassCycle = gSaveContext.save.saveInfo.playerData.threeDayResetCount + 1;
            World_Log("Through the spyglass, your nemesis is revealed.");
            return true;
        case PI_STASIS_ORB:
            gBuffTimers[item] = SEC(6);
            return true;
        case PI_RIPOSTE_CHARM:
            gBuffTimers[item] = SEC(60);
            return true;
        case PI_EVENT_TEMPTER:
            if (gEv.id >= 0) {
                Hud_Notify("An event is already underway.", warn);
                return false;
            }
            Events_Start(play, Events_RandomEligible(play, false), false, NULL);
            if (gEv.id >= 0) {
                gEv.friendBonus += 50;
                return true;
            }
            return false;
        case PI_NEMESIS_BRAND:
            if (gMf.nemesis.state != NEM_NONE) {
                Hud_Notify("You already have a nemesis.", warn);
                return false;
            }
            gPromoteNextHit = true;
            Hud_Notify("The brand burns. Strike an enemy to mark it.", warn);
            return true;
        case PI_BOUNTY_REROLL:
            Bounty_Refresh(true);
            return true;
        case PI_HEART_SIPHON:
            gBuffTimers[item] = SEC(45);
            return true;
    }
    return false;
}

s32 Pouch_Use(PlayState* play, s32 item) {
    MfColor c = { 160, 220, 255, 255 };
    char buf[48];

    if (gMf.pouch[item] == 0) {
        MfColor warn = { 255, 150, 150, 255 };
        Hud_Notify("You have none of that in your pouch.", warn);
        Mf_Sfx(NA_SE_SY_ERROR);
        return false;
    }
    if (Curse_Active(CURSE_SEALED) && sSealedCooldown > 0) {
        MfColor warn = { 255, 150, 150, 255 };
        Hud_Notify("Curse of the Sealed Pouch: wait a moment.", warn);
        Mf_Sfx(NA_SE_SY_ERROR);
        return false;
    }
    if (!ApplyItem(play, item)) {
        Mf_Sfx(NA_SE_SY_ERROR);
        return false;
    }
    gMf.pouch[item]--;
    if (Curse_Active(CURSE_SEALED)) {
        sSealedCooldown = SEC(45);
    }
    buf[0] = '\0';
    Str_Cat(buf, "Used ", sizeof(buf));
    Str_Cat(buf, gPouchItems[item].name, sizeof(buf));
    Hud_Notify(buf, c);
    Mf_Sfx(NA_SE_SY_DECIDE);
    Save_MarkDirty();
    return true;
}

void Pouch_Update(PlayState* play) {
    s32 i;
    for (i = 0; i < NUM_POUCH_ITEMS; i++) {
        if (gBuffTimers[i] > 0) {
            gBuffTimers[i]--;
        }
    }
    if (sSealedCooldown > 0) {
        sSealedCooldown--;
    }
    (void)sTempterBonus;
}

// Bottom-left pouch panel while L is held, and a compact buff line otherwise.
void Pouch_DrawHud(PlayState* play) {
    char buf[48];
    s32 i;
    s32 x = 10;

    if (gRt.menuOpen) {
        return;
    }
    if ((gRt.btnCur & BTN_L) && gRt.gameplayOk) {
        const PouchItemDef* it = &gPouchItems[gMf.pouchSel];
        MfColor bg = { 10, 0, 30, 190 };
        MfColor name = { 255, 230, 140, 255 };
        MfColor desc = { 220, 220, 255, 255 };
        MfColor dim = { 150, 150, 170, 255 };

        Draw2D_Rect(6, 176, 308, 42, bg);
        buf[0] = '\0';
        Str_Cat(buf, "< ", sizeof(buf));
        Str_Cat(buf, it->name, sizeof(buf));
        Str_Cat(buf, " x", sizeof(buf));
        Str_CatInt(buf, gMf.pouch[gMf.pouchSel], sizeof(buf));
        Str_Cat(buf, " >", sizeof(buf));
        Draw2D_Text(12, 180, name, buf);
        Draw2D_Text(12, 191, desc, it->desc);
        buf[0] = '\0';
        Str_Cat(buf, "Down: use   Up: next owned   Marks: ", sizeof(buf));
        Str_CatInt(buf, gMf.moonMarks, sizeof(buf));
        Draw2D_Text(12, 204, dim, buf);
        return;
    }
    for (i = 0; i < NUM_POUCH_ITEMS; i++) {
        if (gBuffTimers[i] > 0) {
            MfColor c = { 140, 255, 200, 230 };
            buf[0] = '\0';
            Str_Cat(buf, gPouchItems[i].name, sizeof(buf));
            Str_Cat(buf, " ", sizeof(buf));
            Str_CatInt(buf, gBuffTimers[i] / FPS + 1, sizeof(buf));
            Str_Cat(buf, "s", sizeof(buf));
            Draw2D_Text(x, 222, c, buf);
            x += (Str_Len(buf) + 2) * 8;
            if (x > 280) {
                break;
            }
        }
    }
}
