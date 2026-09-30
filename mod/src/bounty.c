#include "mf.h"

// Bounty targets are named, trait-bearing enemies living on the zone graph.
// Claim up to three contracts on the board; claimed targets show up when you
// enter their zone. Rewards scale with the species' difficulty and the rank.

const char* gRankNames[5] = { "D", "C", "B", "A", "S" };
static const u8 sRankMult[5] = { 10, 15, 22, 32, 45 }; // x0.1
static const u8 sRankMinDiff[5] = { 1, 2, 4, 5, 6 };
static const u8 sRankMaxDiff[5] = { 3, 5, 6, 8, 9 };
static const char* sMotiveNames[MOTIVE_MAX] = { "Roaming", "Migrating", "Nesting", "Hunting you", "Fleeing" };

static const char* sBountyNames[] = {
    "Rotfang", "Mudgut", "Bristle", "Old Scratch", "Nettle", "Cinder", "Hollow", "Grimbly",
    "Snarl", "Mossback", "Widow", "Pale Tom", "Ruckus", "Gnash", "Ember", "Crooked Kit",
    "Sallow", "Brine", "Thistle", "Grub", "Lantern", "Soot", "Marrow", "Tallow",
};

s32 gCompassTimer = 0;
static s32 sSpawnCooldown[MAX_BOUNTIES];
static s32 sBlockedNotice = 0;
static s32 sMoveTimers[MAX_BOUNTIES];

static s32 NextMoveInterval(BountyRec* b) {
    if (b->motive == MOTIVE_HUNT || (b->claimed && Curse_Active(CURSE_HUNTED))) {
        return SEC(Rng_Range(20, 50));
    }
    switch (b->motive) {
        case MOTIVE_MIGRATE: return SEC(Rng_Range(30, 70));
        case MOTIVE_FLEE: return SEC(Rng_Range(30, 80));
        case MOTIVE_NEST: return SEC(Rng_Range(60, 150));
        default: return SEC(Rng_Range(40, 110));
    }
}

void Bounty_Name(char* out, s32 max, BountyRec* b) {
    out[0] = '\0';
    Str_Cat(out, sBountyNames[b->nameA % ARRAY_LEN(sBountyNames)], max);
    Str_Cat(out, " the ", max);
    Str_Cat(out, gSpecies[b->species % gSpeciesCount].name, max);
}

static s32 RandomWildZone(void) {
    s32 tries;
    for (tries = 0; tries < 50; tries++) {
        s32 z = Rng_Range(0, gZoneCount - 1);
        if (gZones[z].danger >= 1) {
            return z;
        }
    }
    return 5;
}

static s32 ComputeReward(BountyRec* b) {
    s32 traits = 0;
    s32 t;
    s32 r;
    for (t = 0; t < NUM_TRAITS; t++) {
        traits += HAS_TRAIT(b->traits, t);
    }
    r = gSpecies[b->species].difficulty * 3 * sRankMult[b->rank] / 10 + traits * 5;
    // Wilder zones pay a little more: they are further and nastier.
    r += gZones[b->zone].danger;
    return MF_MAX(5, r);
}

void Bounty_Generate(BountyRec* b, s32 zoneHint) {
    s32 roll = Rng_Range(0, 99);
    s32 rank = (roll < 35) ? 0 : (roll < 65) ? 1 : (roll < 85) ? 2 : (roll < 96) ? 3 : 4;
    s32 traitCount = (rank == 0) ? 0 : (rank == 1) ? 1 : (rank == 2) ? Rng_Range(1, 2) : (rank == 3) ? 2 : 3;
    s32 i;
    s32 m;

    b->active = true;
    b->claimed = false;
    b->sponsored = false;
    b->sponsor[0] = '\0';
    b->rank = rank;
    b->species = Roster_Random(sRankMinDiff[rank], sRankMaxDiff[rank]);
    b->zone = (zoneHint >= 0 && zoneHint < gZoneCount) ? zoneHint : RandomWildZone();
    b->traits = 0;
    for (i = 0; i < traitCount; i++) {
        s32 t = Rng_Range(0, NUM_TRAITS - 1);
        // Warlord and Ambusher only make sense on a nemesis; reroll them.
        if (t == TR_WARLORD || t == TR_AMBUSHER || t == TR_RELENTLESS || t == TR_GRUDGE) {
            t = TR_SWIFT;
        }
        b->traits |= 1u << t;
    }
    m = Rng_Range(0, 99);
    b->motive = (m < 35) ? MOTIVE_ROAM : (m < 60) ? MOTIVE_MIGRATE : (m < 85) ? MOTIVE_NEST : (m < 95) ? MOTIVE_HUNT : MOTIVE_FLEE;
    if (gSpecies[b->species].flags & SPF_STATIONARY) {
        b->motive = MOTIVE_NEST;
    }
    b->targetZone = RandomWildZone();
    b->nameA = Rng_Range(0, ARRAY_LEN(sBountyNames) - 1);
    b->nameB = 0;
    b->hpPercent = 100;
    b->expireMins = Rng_Range(30, 55); // in world ticks (45s each)
    b->reward = ComputeReward(b);
}

void Bounty_Refresh(s32 keepClaimed) {
    s32 i;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        if (keepClaimed && gMf.bounties[i].active && gMf.bounties[i].claimed) {
            continue;
        }
        Bounty_Generate(&gMf.bounties[i], -1);
    }
    Save_MarkDirty();
}

s32 Bounty_ClaimedCount(void) {
    s32 i;
    s32 n = 0;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        if (gMf.bounties[i].active && gMf.bounties[i].claimed) {
            n++;
        }
    }
    return n;
}

// Friends can post bounties from the website.
s32 Bounty_Add(s32 species, s32 zone, s32 reward, const char* sponsor) {
    s32 i;
    s32 slot = -1;
    BountyRec* b;

    for (i = 0; i < MAX_BOUNTIES; i++) {
        if (!gMf.bounties[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (i = 0; i < MAX_BOUNTIES; i++) {
            if (!gMf.bounties[i].claimed && !gMf.bounties[i].sponsored) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        return false;
    }
    b = &gMf.bounties[slot];
    Bounty_Generate(b, zone);
    if (species >= 0 && species < gSpeciesCount) {
        b->species = species;
    }
    b->sponsored = true;
    Str_Copy(b->sponsor, sponsor, sizeof(b->sponsor));
    b->reward = MF_CLAMP(MF_MAX(reward, ComputeReward(b)), 5, 300);
    {
        char name[40];
        char buf[LOG_WIDTH];
        Bounty_Name(name, sizeof(name), b);
        buf[0] = '\0';
        Str_Cat(buf, sponsor, LOG_WIDTH);
        Str_Cat(buf, " posted a bounty: ", LOG_WIDTH);
        Str_Cat(buf, name, LOG_WIDTH);
        World_Log(buf);
    }
    Save_MarkDirty();
    return true;
}

void Bounty_ApplyStats(Actor* actor, ActorTag* tag) {
    BountyRec* b = &gMf.bounties[tag->index % MAX_BOUNTIES];
    Nemesis_ApplyStats(actor, tag);
    if (b->hpPercent > 0 && b->hpPercent < 100) {
        actor->colChkInfo.health = (u8)MF_MAX(1, actor->colChkInfo.health * b->hpPercent / 100);
        tag->lastHealth = actor->colChkInfo.health;
    }
}

static void Spawn(PlayState* play, s32 i) {
    BountyRec* b = &gMf.bounties[i];
    Player* player = GET_PLAYER(play);
    Vec3f p;
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    f32 minD = 350.0f;
    f32 maxD = 650.0f;
    Actor* actor;

    if (!Actors_SpeciesObjectReady(play, b->species)) {
        if (sBlockedNotice <= 0) {
            char name[40];
            Bounty_Name(name, sizeof(name), b);
            {
                MfColor c = { 255, 210, 120, 255 };
                World_Logf2(name, " is here, but hiding out of reach.", NULL);
                Hud_Notify("A bounty target is here but can't reach you in this area.", c);
            }
            sBlockedNotice = SEC(120);
        }
        sSpawnCooldown[i] = SEC(60);
        return;
    }
    if (b->motive == MOTIVE_HUNT || Curse_Active(CURSE_HUNTED)) {
        minD = 200.0f;
        maxD = 320.0f;
        yaw = player->actor.shape.rot.y + 0x8000;
    }
    Mf_FindSpawnPoint(play, &p, minD, maxD, yaw);
    actor = Actors_SpawnSpecies(play, b->species, &p, Mf_YawTo(&p, &player->actor.world.pos), TAG_BOUNTY, i);
    sSpawnCooldown[i] = SEC(45);
    if (actor == NULL) {
        Str_Copy(gPreloadNote, "Bounty spawn failed (the game refused it)", sizeof(gPreloadNote));
    }
    if (actor != NULL) {
        char name[40];
        char sub[48];
        Bounty_Name(name, sizeof(name), b);
        sub[0] = '\0';
        Str_Cat(sub, "Rank ", sizeof(sub));
        Str_Cat(sub, gRankNames[b->rank], sizeof(sub));
        Str_Cat(sub, " bounty target - ", sizeof(sub));
        Str_CatInt(sub, b->reward, sizeof(sub));
        Str_Cat(sub, " Marks", sizeof(sub));
        Hud_Banner(name, sub);
        Mf_Sfx(NA_SE_SY_FOUND);
    }
}

static void MoveBounty(BountyRec* b, s32 idx);

// Every target walks on its own clock rather than all at once.
static void TravelUpdate(void) {
    s32 i;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        BountyRec* b = &gMf.bounties[i];
        if (!b->active) {
            continue;
        }
        if (sMoveTimers[i] <= 0) {
            sMoveTimers[i] = NextMoveInterval(b);
        }
        if (--sMoveTimers[i] == 0) {
            World_SetRumor(true);
            MoveBounty(b, i);
            World_SetRumor(false);
            sMoveTimers[i] = NextMoveInterval(b);
            Save_MarkDirty();
        }
    }
}

void Bounty_Update(PlayState* play) {
    s32 i;

    if (gMf.settings.bountiesEnabled) {
        TravelUpdate();
    }

    if (gCompassTimer > 0) {
        gCompassTimer--;
    }
    if (sBlockedNotice > 0) {
        sBlockedNotice--;
    }
    if (!gMf.settings.bountiesEnabled || gRt.zone == ZONE_NONE || gRt.framesSinceSceneStart < SEC(6)) {
        return;
    }
    for (i = 0; i < MAX_BOUNTIES; i++) {
        BountyRec* b = &gMf.bounties[i];
        Actor* a;
        if (sSpawnCooldown[i] > 0) {
            sSpawnCooldown[i]--;
        }
        if (!b->active || !b->claimed || b->zone != gRt.zone) {
            continue;
        }
        a = Actors_FindTagged(TAG_BOUNTY, i);
        if (a != NULL) {
            ActorTag* tag = Tag_Get(a);
            if (tag != NULL && tag->maxHealth > 0) {
                b->hpPercent = (u8)MF_CLAMP(a->colChkInfo.health * 100 / tag->maxHealth, 1, 100);
            }
            continue;
        }
        if (sSpawnCooldown[i] == 0 && Actors_CountTagged(TAG_BOUNTY, -1) < 2) {
            Spawn(play, i);
        }
    }
}

void Bounty_OnSceneChange(PlayState* play) {
    s32 i;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        sSpawnCooldown[i] = 0;
    }
}

void Bounty_OnTaggedKilled(PlayState* play, Actor* actor, ActorTag* tag) {
    BountyRec* b = &gMf.bounties[tag->index % MAX_BOUNTIES];
    char name[40];

    if (!b->active) {
        return;
    }
    Bounty_Name(name, sizeof(name), b);
    if (actor->colChkInfo.health > 0) {
        if (Curse_Active(CURSE_BRITTLE)) {
            b->zone = World_RandomNeighbour(b->zone);
            World_Logf2(name, " slipped away to ", gZones[b->zone].name);
        }
        return;
    }
    if (b->claimed) {
        s32 reward = Marks_Scale(b->reward);
        Hud_Banner("BOUNTY COLLECTED", name);
        Marks_Add(reward, "bounty");
    } else {
        MfColor c = { 200, 200, 200, 255 };
        Hud_Notify("That was an unclaimed bounty target. No payout.", c);
    }
    gMf.bountiesKilled++;
    World_Logf2(name, " was slain by the hero.", NULL);
    Bounty_Generate(b, -1);
    Save_MarkDirty();
}

// ---------------------------------------------------------------------------
// Off-screen life
// ---------------------------------------------------------------------------

static void MoveBounty(BountyRec* b, s32 idx) {
    s32 hunting = (b->motive == MOTIVE_HUNT) || (b->claimed && Curse_Active(CURSE_HUNTED));
    s32 before = b->zone;
    char name[40];

    if (gSpecies[b->species].flags & SPF_STATIONARY) {
        return;
    }
    if (gRt.zone == b->zone && Actors_FindTagged(TAG_BOUNTY, idx) != NULL) {
        return; // it's busy with you
    }
    if (hunting) {
        if (Rng_Chance(60)) {
            b->zone = World_NextStepToward(b->zone, gRt.lastZone);
        }
    } else {
        switch (b->motive) {
            case MOTIVE_ROAM:
                if (Rng_Chance(30)) {
                    b->zone = World_RandomNeighbour(b->zone);
                }
                break;
            case MOTIVE_MIGRATE:
                if (Rng_Chance(50)) {
                    b->zone = World_NextStepToward(b->zone, b->targetZone);
                }
                if (b->zone == b->targetZone) {
                    b->motive = MOTIVE_NEST;
                }
                break;
            case MOTIVE_NEST:
                if (Rng_Chance(8)) {
                    b->motive = MOTIVE_ROAM;
                }
                break;
            case MOTIVE_FLEE: {
                s32 n = World_RandomNeighbour(b->zone);
                if (World_Distance(n, gRt.lastZone) >= World_Distance(b->zone, gRt.lastZone) && Rng_Chance(50)) {
                    b->zone = n;
                }
                break;
            }
        }
    }
    // Towns are off limits unless it's hunting you.
    if (!hunting && gZones[b->zone].danger == 0) {
        b->zone = before;
    }
    if (b->zone != before && (b->claimed || Rng_Chance(15))) {
        Bounty_Name(name, sizeof(name), b);
        World_Logf2(name, " moved to ", gZones[b->zone].name);
    }
}

static void Brawl(void) {
    s32 i;
    s32 j;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        for (j = i + 1; j < MAX_BOUNTIES; j++) {
            BountyRec* a = &gMf.bounties[i];
            BountyRec* b = &gMf.bounties[j];
            if (!a->active || !b->active || a->zone != b->zone || a->zone == gRt.zone) {
                continue;
            }
            if (Rng_Chance(12)) {
                s32 pa = gSpecies[a->species].difficulty + a->rank * 2 + Rng_Range(0, 4);
                s32 pb = gSpecies[b->species].difficulty + b->rank * 2 + Rng_Range(0, 4);
                BountyRec* win = (pa >= pb) ? a : b;
                BountyRec* lose = (pa >= pb) ? b : a;
                char wn[40];
                char ln[40];
                char buf[LOG_WIDTH];
                if (lose->claimed && !win->claimed) {
                    continue; // don't steal a contract you're working on
                }
                Bounty_Name(wn, sizeof(wn), win);
                Bounty_Name(ln, sizeof(ln), lose);
                buf[0] = '\0';
                Str_Cat(buf, sBountyNames[win->nameA % ARRAY_LEN(sBountyNames)], LOG_WIDTH);
                Str_Cat(buf, " killed ", LOG_WIDTH);
                Str_Cat(buf, ln, LOG_WIDTH);
                World_Log(buf);
                if (win->rank < 4) {
                    win->rank++;
                }
                win->reward = ComputeReward(win) + lose->reward / 3;
                Bounty_Generate(lose, -1);
                return;
            }
        }
    }
}

void Bounty_WorldTick(PlayState* play) {
    s32 i;
    for (i = 0; i < MAX_BOUNTIES; i++) {
        BountyRec* b = &gMf.bounties[i];
        s32 decay;
        if (!b->active) {
            Bounty_Generate(b, -1);
            continue;
        }
        decay = (b->claimed && Curse_Active(CURSE_BRITTLE)) ? 2 : 1;
        if (b->expireMins > decay) {
            b->expireMins -= decay;
        } else {
            char name[40];
            Bounty_Name(name, sizeof(name), b);
            World_Logf2(b->claimed ? "Your contract expired: " : "Bounty withdrawn: ", name, NULL);
            Bounty_Generate(b, -1);
        }
    }
    Brawl();
    Save_MarkDirty();
}

// Compass read-out while the Bounty Compass is active.
void Bounty_DrawHud(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* a;
    char buf[48];
    MfColor c = { 255, 220, 90, 255 };
    MfColor bg = { 0, 0, 0, 140 };

    if (gCompassTimer <= 0 || gRt.menuOpen) {
        return;
    }
    a = Actors_FindTagged(TAG_BOUNTY, -1);
    buf[0] = '\0';
    if (a == NULL) {
        s32 i;
        Str_Cat(buf, "Compass: no target here", sizeof(buf));
        for (i = 0; i < MAX_BOUNTIES; i++) {
            if (gMf.bounties[i].active && gMf.bounties[i].claimed) {
                buf[0] = '\0';
                Str_Cat(buf, "Compass: target in ", sizeof(buf));
                Str_Cat(buf, gZones[gMf.bounties[i].zone].name, sizeof(buf));
                break;
            }
        }
    } else {
        s16 rel = Actor_WorldYawTowardActor(&player->actor, a) - Camera_GetCamDirYaw(GET_ACTIVE_CAM(play));
        const char* dir = (ABS(rel) < 0x1800) ? "ahead" : (ABS(rel) > 0x6800) ? "behind" : (rel > 0) ? "left" : "right";
        Str_Cat(buf, "Compass: ", sizeof(buf));
        Str_CatInt(buf, (s32)(a->xzDistToPlayer / 10.0f), sizeof(buf));
        Str_Cat(buf, "m ", sizeof(buf));
        Str_Cat(buf, dir, sizeof(buf));
    }
    Draw2D_Rect(80, 206, 160, 12, bg);
    Draw2D_TextCentered(208, c, buf);
}

const char* Bounty_MotiveName(s32 m) {
    return sMotiveNames[m % MOTIVE_MAX];
}
