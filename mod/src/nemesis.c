#include "mf.h"

#define NEM (gMf.nemesis)

// ---------------------------------------------------------------------------
// Nemesis system. One nemesis at a time. It is promoted from an ordinary
// enemy that earned it, remembers what it did to you, grows a stat point
// every time it kills you, evolves traits, and walks the zone graph to find you.
// ---------------------------------------------------------------------------

const TraitDef gTraits[NUM_TRAITS] = {
    { "Ironhide",     "Shrugs off a third of all damage." },
    { "Regenerating", "Slowly heals when not being hit." },
    { "Berserker",    "Faster and deadlier below half health." },
    { "Vampiric",     "Heals itself whenever it hurts you." },
    { "Venomous",     "Its hits poison you for 10 seconds." },
    { "Frostbrand",   "Its hits chill you, slowing your steps." },
    { "Stormcaller",  "Its hits shock you." },
    { "Pyromaniac",   "Its hits set you ablaze." },
    { "Blinker",      "Teleports next to you if you keep away." },
    { "Ambusher",     "Appears right behind you, without warning." },
    { "Relentless",   "Crosses Termina twice as fast." },
    { "Thief",        "Steals Moon Marks with every hit." },
    { "Mark-Hungry",  "Hits harder the more Moon Marks you carry." },
    { "Moonblessed",  "Immune to harm while a Global Event rages." },
    { "Giant",        "Larger, with half again as much health." },
    { "Swift",        "Moves much faster than its kind." },
    { "Hexcaller",    "Its hits can call down a Global Event." },
    { "Coward",       "Flees at low health and returns healed." },
    { "Warlord",      "Arrives with two minions of its kind." },
    { "Grudge-Bearer","Grows deadlier every time you run from it." },
};

static const char* sNamesA[] = {
    "Grak", "Vorn", "Skrell", "Ulgoth", "Maz", "Kethra", "Brum", "Ziv", "Horga", "Drel", "Vasko", "Tuk",
    "Oona", "Ragnor", "Pell", "Syx", "Gorm", "Irla", "Kaz", "Mordu", "Fenn", "Zorba", "Quill", "Hask",
};
static const char* sNamesB[] = {
    "the Unbroken", "Moonbitten", "Skullcleaver", "the Patient", "Ashmaw", "the Grinning", "Nightstalker",
    "the Twice-Dead", "Gloomfang", "the Collector", "Bonegnaw", "the Laughing", "Stormhide", "the Hungry",
    "Duskwalker", "the Scarred", "Ironjaw", "the Whisper", "Blightclaw", "the Relentless", "the Mirthless",
    "Cinderheart", "the Masked", "Frostmourn",
};

const char* gPromotionReasons[] = {
    "It killed you.",
    "It struck you three times and lived.",
    "It watched you run away.",
    "It survived your blade.",
    "You branded it yourself.",
    "It outlasted a Moon ambush.",
    "Your friends crowned it.",
};

u8 gPromoteNextHit = false;
s32 gChillTimer = 0;
s32 gPoisonTimer = 0;
static s32 sManifestCooldown = 0;
static s32 sPresentLastFrame = false;
static s32 sBlockedNotice = 0;
static s32 sPoisonTick = 0;
static s32 sMoveTimer = -1;

// Each hunter keeps its own travel clock, so things don't all move at once.
// Base 60-140 seconds per zone, faster with Tracking, Relentless and Vendetta.
static s32 NextTravelInterval(void) {
    s32 secs = Rng_Range(60, 140) * 10 / (10 + 5 * NEM.stats[STAT_TRACKING]);
    if (HAS_TRAIT(NEM.traits, TR_RELENTLESS)) {
        secs /= 2;
    }
    if (Curse_Active(CURSE_VENDETTA)) {
        secs /= 2;
    }
    return SEC(MF_MAX(15, secs));
}

static void TravelUpdate(PlayState* play) {
    s32 target = gRt.lastZone;
    char name[48];

    if (NEM.state != NEM_HUNTING) {
        sMoveTimer = -1;
        return;
    }
    if (sMoveTimer < 0) {
        sMoveTimer = NextTravelInterval();
    }
    if (--sMoveTimer > 0) {
        return;
    }
    sMoveTimer = NextTravelInterval();
    if (NEM.zone == target || Actors_FindTagged(TAG_NEMESIS, -1) != NULL) {
        return;
    }
    NEM.zone = World_NextStepToward(NEM.zone, target);
    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    World_SetRumor(true);
    if (Rng_Chance(45) || NEM.zone == target) {
        World_Logf2(name, " was seen in ", gZones[NEM.zone].name);
    }
    World_SetRumor(false);
    Save_MarkDirty();
}


void Nemesis_Name(char* out, s32 max, u8 a, u8 b, s32 species) {
    out[0] = '\0';
    Str_Cat(out, sNamesA[a % ARRAY_LEN(sNamesA)], max);
    Str_Cat(out, " ", max);
    Str_Cat(out, sNamesB[b % ARRAY_LEN(sNamesB)], max);
    if (species >= 0 && species < gSpeciesCount) {
        Str_Cat(out, " (", max);
        Str_Cat(out, gSpecies[species].name, max);
        Str_Cat(out, ")", max);
    }
}

static s32 TraitCount(u32 mask) {
    s32 n = 0;
    while (mask) {
        n += mask & 1;
        mask >>= 1;
    }
    return n;
}

static s32 StatSum(void) {
    return NEM.stats[0] + NEM.stats[1] + NEM.stats[2] + NEM.stats[3];
}

static void RefreshLevel(void) {
    NEM.level = (u8)MF_MIN(99, 1 + StatSum());
}

void Nemesis_GainTrait(void) {
    s32 tries;
    if (TraitCount(NEM.traits) >= 6) {
        return;
    }
    for (tries = 0; tries < 40; tries++) {
        s32 t = Rng_Range(0, NUM_TRAITS - 1);
        if (!HAS_TRAIT(NEM.traits, t)) {
            char buf[LOG_WIDTH];
            NEM.traits |= (1u << t);
            buf[0] = '\0';
            Str_Cat(buf, sNamesA[NEM.nameA % ARRAY_LEN(sNamesA)], LOG_WIDTH);
            Str_Cat(buf, " evolved: ", LOG_WIDTH);
            Str_Cat(buf, gTraits[t].name, LOG_WIDTH);
            World_Log(buf);
            return;
        }
    }
}

void Nemesis_AddStat(s32 stat, s32 amount) {
    if (stat < 0 || stat >= STAT_MAX) {
        stat = Rng_Range(0, STAT_MAX - 1);
    }
    NEM.stats[stat] = (u8)MF_CLAMP(NEM.stats[stat] + amount, 0, 20);
    RefreshLevel();
    Save_MarkDirty();
}

static const char* sStatNames[STAT_MAX] = { "Health", "Power", "Tracking", "Speed" };

void Nemesis_Promote(PlayState* play, s32 species, s32 reason, Actor* actor) {
    char name[48];
    char sub[64];
    s32 i;

    if (NEM.state != NEM_NONE || species < 0 || !gMf.settings.nemesisEnabled) {
        return;
    }
    for (i = 0; i < (s32)sizeof(NemesisRec); i++) {
        ((u8*)&NEM)[i] = 0;
    }
    NEM.state = NEM_HUNTING;
    NEM.species = species;
    NEM.nameA = Rng_Range(0, ARRAY_LEN(sNamesA) - 1);
    NEM.nameB = Rng_Range(0, ARRAY_LEN(sNamesB) - 1);
    NEM.zone = (gRt.zone != ZONE_NONE) ? gRt.zone : gRt.lastZone;
    NEM.promotionReason = reason;
    NEM.hpPercent = 100;
    Nemesis_GainTrait();
    if (Curse_Active(CURSE_VENDETTA)) {
        Nemesis_AddStat(-1, 1);
    }
    RefreshLevel();
    gPromoteNextHit = false;

    if (actor != NULL) {
        ActorTag* tag = Tag_Get(actor);
        if (tag != NULL) {
            tag->kind = TAG_NEMESIS;
            tag->species = species;
            Nemesis_ApplyStats(actor, tag);
            tag->lastHealth = actor->colChkInfo.health;
        }
    }

    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    sub[0] = '\0';
    Str_Cat(sub, gSpecies[species].name, sizeof(sub));
    Str_Cat(sub, " - ", sizeof(sub));
    Str_Cat(sub, gPromotionReasons[reason % ARRAY_LEN(gPromotionReasons)], sizeof(sub));
    Hud_Banner("A NEMESIS RISES", name);
    {
        MfColor c = { 255, 120, 120, 255 };
        Hud_Notify(sub, c);
    }
    Mf_Sfx(NA_SE_SY_STALKIDS_PSYCHO);
    World_Logf2(name, " was promoted to your nemesis.", NULL);
    Save_MarkDirty();
}

// ---------------------------------------------------------------------------
// Stats and traits on the live actor (shared with bounty targets)
// ---------------------------------------------------------------------------

u32 Tagged_Traits(ActorTag* tag) {
    if (tag->kind == TAG_NEMESIS) {
        return NEM.traits;
    }
    if (tag->kind == TAG_BOUNTY && tag->index < MAX_BOUNTIES) {
        return gMf.bounties[tag->index].traits;
    }
    return 0;
}

s32 Tagged_Stat(ActorTag* tag, s32 stat) {
    if (tag->kind == TAG_NEMESIS) {
        s32 v = NEM.stats[stat];
        if (stat == STAT_POWER && HAS_TRAIT(NEM.traits, TR_MARK_HUNGRY)) {
            v += MF_MIN(3, (s32)gMf.moonMarks / 50);
        }
        return v;
    }
    if (tag->kind == TAG_BOUNTY && tag->index < MAX_BOUNTIES) {
        return gMf.bounties[tag->index].rank; // rank doubles as a stat budget
    }
    return 0;
}

void Tagged_Name(ActorTag* tag, char* out, s32 max) {
    if (tag->kind == TAG_NEMESIS) {
        Nemesis_Name(out, max, NEM.nameA, NEM.nameB, -1);
    } else if (tag->kind == TAG_BOUNTY && tag->index < MAX_BOUNTIES) {
        Bounty_Name(out, max, &gMf.bounties[tag->index]);
    } else {
        Str_Copy(out, "Something", max);
    }
}

void Nemesis_ApplyStats(Actor* actor, ActorTag* tag) {
    s32 base = MF_MAX(1, actor->colChkInfo.health);
    s32 hpStat = Tagged_Stat(tag, STAT_HEALTH);
    u32 traits = Tagged_Traits(tag);
    s32 hp = base * (10 + hpStat * 4) / 10;

    if (tag->kind == TAG_NEMESIS) {
        hp += base * (NEM.level / 3);
        if (NEM.scarred) {
            hp += base / 2;
        }
    }
    if (HAS_TRAIT(traits, TR_GIANT)) {
        hp = hp * 3 / 2;
        actor->scale.x *= 1.35f;
        actor->scale.y *= 1.35f;
        actor->scale.z *= 1.35f;
    }
    if (tag->kind == TAG_NEMESIS && NEM.hpPercent > 0 && NEM.hpPercent < 100) {
        hp = MF_MAX(1, hp * NEM.hpPercent / 100);
    }
    actor->colChkInfo.health = (u8)MF_CLAMP(hp, 1, 250);
    tag->maxHealth = actor->colChkInfo.health;
    tag->lastHealth = actor->colChkInfo.health;
    tag->applied = true;
}

s32 Nemesis_FilterDamage(Actor* actor, ActorTag* tag, s32 drop) {
    u32 traits = Tagged_Traits(tag);
    if (HAS_TRAIT(traits, TR_MOONBLESSED) && gEv.id >= 0) {
        return 0;
    }
    if (HAS_TRAIT(traits, TR_IRONHIDE)) {
        return drop - (drop + 1) / 3;
    }
    return drop;
}

// Trait effects when a tagged enemy hurts the player.
static void OnTaggedHitPlayer(PlayState* play, Actor* actor, ActorTag* tag, s32 damage) {
    u32 traits = Tagged_Traits(tag);
    s32 power = Tagged_Stat(tag, STAT_POWER);
    MfColor c = { 255, 150, 150, 255 };

    if (HAS_TRAIT(traits, TR_BERSERKER) && actor->colChkInfo.health * 2 < tag->maxHealth) {
        power += 2;
    }
    if (power > 0 && !gRt.playerDead) {
        s32 extra = MF_MAX(1, damage * power / 5);
        extra = Mf_DifficultyScale(extra);
        gSaveContext.save.saveInfo.playerData.health = MF_MAX(0, gSaveContext.save.saveInfo.playerData.health - extra);
    }
    if (HAS_TRAIT(traits, TR_VAMPIRIC) && actor->colChkInfo.health < tag->maxHealth) {
        actor->colChkInfo.health = MF_MIN(tag->maxHealth, actor->colChkInfo.health + 2);
        tag->lastHealth = actor->colChkInfo.health;
    }
    if (HAS_TRAIT(traits, TR_VENOMOUS)) {
        gPoisonTimer = SEC(10);
    }
    if (HAS_TRAIT(traits, TR_FROSTBRAND)) {
        gChillTimer = SEC(4);
    }
    if (HAS_TRAIT(traits, TR_STORMCALLER)) {
        Mf_ShockPlayer(1);
    }
    if (HAS_TRAIT(traits, TR_PYROMANIAC)) {
        Mf_BurnPlayer();
    }
    if (HAS_TRAIT(traits, TR_THIEF) && gMf.moonMarks > 0) {
        s32 steal = MF_MIN(3, (s32)gMf.moonMarks);
        gMf.moonMarks -= steal;
        if (tag->kind == TAG_NEMESIS) {
            NEM.stolenMarks += steal;
        }
        Hud_Notify("Your Moon Marks were stolen!", c);
    }
    if (HAS_TRAIT(traits, TR_HEXCALLER) && gEv.id < 0 && Rng_Chance(35)) {
        Events_Start(play, Events_RandomEligible(play, true), false, NULL);
    }
}

void Nemesis_OnPlayerHit(PlayState* play, Actor* attacker, s32 damage) {
    ActorTag* tag = Tag_Get(attacker);
    if (tag == NULL) {
        return;
    }
    if (tag->kind == TAG_NEMESIS || tag->kind == TAG_BOUNTY) {
        OnTaggedHitPlayer(play, attacker, tag, damage);
    }
}

void Nemesis_ActorUpdate(PlayState* play, Actor* actor, ActorTag* tag) {
    u32 traits = Tagged_Traits(tag);
    s32 speed = Tagged_Stat(tag, STAT_SPEED);
    f32 k = speed * 0.1f;
    Player* player = GET_PLAYER(play);

    if (HAS_TRAIT(traits, TR_SWIFT)) {
        k += 0.3f;
    }
    if (HAS_TRAIT(traits, TR_BERSERKER) && actor->colChkInfo.health * 2 < tag->maxHealth) {
        k += 0.4f;
    }
    k = MF_MIN(k, 0.8f);
    if (k > 0.0f && !tag->fleeing) {
        f32 dx = actor->world.pos.x - actor->prevPos.x;
        f32 dz = actor->world.pos.z - actor->prevPos.z;
        if (dx * dx + dz * dz < 400.0f) {
            actor->world.pos.x += dx * k;
            actor->world.pos.z += dz * k;
        }
    }

    // Regenerating: +1 HP every 4 seconds without being hit.
    if (tag->lastHealth < tag->maxHealth && HAS_TRAIT(traits, TR_REGENERATING) && actor->colChkInfo.health > 0) {
        tag->regenTimer++;
        if (tag->regenTimer > SEC(4)) {
            tag->regenTimer = 0;
            actor->colChkInfo.health++;
            tag->lastHealth = actor->colChkInfo.health;
        }
    }

    // Blinker: closes the distance if the player keeps away.
    if (HAS_TRAIT(traits, TR_BLINKER) && actor->xzDistToPlayer > 350.0f && actor->colChkInfo.health > 0) {
        tag->blinkTimer++;
        if (tag->blinkTimer > SEC(8)) {
            Vec3f p;
            tag->blinkTimer = 0;
            EffectSsDeadDb_Spawn(play, &actor->world.pos, &gZeroVec3f, &gZeroVec3f, &(Color_RGBA8){ 80, 0, 120, 255 },
                                 &(Color_RGBA8){ 20, 0, 40, 255 }, 120, 0, 12);
            Mf_FindSpawnPoint(play, &p, 150.0f, 230.0f, player->actor.shape.rot.y + 0x8000);
            actor->world.pos = p;
            actor->prevPos = p;
            Mf_Sfx(NA_SE_EN_STALKIDS_APPEAR);
        }
    }

    // Coward: bolts at low health.
    if (HAS_TRAIT(traits, TR_COWARD) && !tag->fleeing && actor->colChkInfo.health > 0 &&
        actor->colChkInfo.health * 10 <= tag->maxHealth * 3) {
        char name[48];
        tag->fleeing = true;
        Tagged_Name(tag, name, sizeof(name));
        EffectSsDeadDb_Spawn(play, &actor->world.pos, &gZeroVec3f, &gZeroVec3f, &(Color_RGBA8){ 120, 120, 120, 255 },
                             &(Color_RGBA8){ 40, 40, 40, 255 }, 160, 0, 14);
        World_Logf2(name, " fled like a coward!", NULL);
        if (tag->kind == TAG_NEMESIS) {
            NEM.state = NEM_DORMANT;
            NEM.dormantSecs = 240;
            NEM.hpPercent = 100;
            NEM.encounters++;
            if (Rng_Chance(35)) {
                Nemesis_GainTrait();
            }
        }
        tag->kind = TAG_NONE;
        Actor_Kill(actor);
        return;
    }

    // Warlord: brings minions on arrival.
    if (HAS_TRAIT(traits, TR_WARLORD) && tag->aliveFrames == 2) {
        s32 i;
        for (i = 0; i < 2; i++) {
            Vec3f p = actor->world.pos;
            p.x += Math_SinS(actor->shape.rot.y + 0x4000 * (i * 2 - 1)) * 80.0f;
            p.z += Math_CosS(actor->shape.rot.y + 0x4000 * (i * 2 - 1)) * 80.0f;
            Actors_SpawnSpecies(play, tag->species, &p, actor->shape.rot.y, TAG_MINION, 0);
        }
    }

    // Identification sparkles: purple for the nemesis, gold for bounty targets.
    if ((gRt.frame % 6) == 0) {
        Vec3f pos = actor->focus.pos;
        Vec3f vel = { 0.0f, 1.5f, 0.0f };
        Vec3f acc = { 0.0f, 0.0f, 0.0f };
        Color_RGBA8 prim = { 255, 255, 255, 255 };
        Color_RGBA8 env = { 170, 60, 255, 255 };
        if (tag->kind == TAG_BOUNTY) {
            env.r = 255;
            env.g = 200;
            env.b = 40;
        }
        pos.x += Rng_Range(-20, 20);
        pos.z += Rng_Range(-20, 20);
        EffectSsKirakira_SpawnSmall(play, &pos, &vel, &acc, &prim, &env);
    }
}

// ---------------------------------------------------------------------------
// Manifesting near the player
// ---------------------------------------------------------------------------

static void Manifest(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 ambusher = HAS_TRAIT(NEM.traits, TR_AMBUSHER);
    Vec3f p;
    Actor* actor;
    char name[48];
    char sub[64];
    s32 i;

    if (!Actors_SpeciesObjectReady(play, NEM.species)) {
        if (sBlockedNotice <= 0) {
            Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
            {
                MfColor c = { 255, 170, 120, 255 };
                World_Logf2(name, " is here but can't reach you in this area.", NULL);
                Hud_Notify("Your nemesis is here but can't reach you in this area.", c);
            }
            sBlockedNotice = SEC(120);
        }
        sManifestCooldown = SEC(30);
        return;
    }
    if (ambusher) {
        Mf_FindSpawnPoint(play, &p, 120.0f, 200.0f, player->actor.shape.rot.y + 0x8000);
    } else {
        Mf_FindSpawnPoint(play, &p, 380.0f, 600.0f, player->actor.shape.rot.y + 0x8000);
    }
    actor = Actors_SpawnSpecies(play, NEM.species, &p, Mf_YawTo(&p, &player->actor.world.pos), TAG_NEMESIS, 0);
    if (actor == NULL) {
        Str_Copy(gPreloadNote, "Nemesis spawn failed (the game refused it)", sizeof(gPreloadNote));
        sManifestCooldown = SEC(20);
        return;
    }
    NEM.encounters++;
    sManifestCooldown = SEC(60);
    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    sub[0] = '\0';
    Str_Cat(sub, "Lv", sizeof(sub));
    Str_CatInt(sub, NEM.level, sizeof(sub));
    Str_Cat(sub, " ", sizeof(sub));
    Str_Cat(sub, gSpecies[NEM.species].name, sizeof(sub));
    for (i = 0; i < NUM_TRAITS; i++) {
        if (HAS_TRAIT(NEM.traits, i)) {
            Str_Cat(sub, " - ", sizeof(sub));
            Str_Cat(sub, gTraits[i].name, sizeof(sub));
            break;
        }
    }
    if (!ambusher) {
        Hud_Banner(name, sub);
    } else {
        MfColor c = { 255, 90, 90, 255 };
        Hud_Notify("Behind you!", c);
    }
    Mf_Sfx(NA_SE_EN_STALKIDS_APPEAR);
    EffectSsDeadDb_Spawn(play, &p, &gZeroVec3f, &gZeroVec3f, &(Color_RGBA8){ 90, 0, 140, 255 },
                         &(Color_RGBA8){ 20, 0, 40, 255 }, 180, 0, 16);
    Save_MarkDirty();
}

void Nemesis_Summon(PlayState* play) {
    char name[48];
    if (NEM.state == NEM_NONE) {
        return;
    }
    NEM.state = NEM_HUNTING;
    NEM.zone = (gRt.zone != ZONE_NONE) ? gRt.zone : gRt.lastZone;
    sManifestCooldown = 0;
    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    World_Logf2(name, " answers the lure.", NULL);
}

void Nemesis_Update(PlayState* play) {
    Actor* present;

    // Status ailments from trait hits
    if (gPoisonTimer > 0) {
        gPoisonTimer--;
        if (++sPoisonTick >= SEC(2)) {
            sPoisonTick = 0;
            if (play->damagePlayer != NULL && gSaveContext.save.saveInfo.playerData.health > 4) {
                play->damagePlayer(play, -Mf_DifficultyScale(3));
            }
        }
    }
    if (gChillTimer > 0) {
        gChillTimer--;
    }
    if (sBlockedNotice > 0) {
        sBlockedNotice--;
    }
    if (!gMf.settings.nemesisEnabled || NEM.state == NEM_NONE) {
        return;
    }
    TravelUpdate(play);
    if (sManifestCooldown > 0) {
        sManifestCooldown--;
    }
    present = Actors_FindTagged(TAG_NEMESIS, -1);
    sPresentLastFrame = (present != NULL);
    if (present != NULL) {
        // Remember wounds so it comes back hurt.
        ActorTag* tag = Tag_Get(present);
        if (tag != NULL && tag->maxHealth > 0) {
            NEM.hpPercent = (u8)MF_CLAMP(present->colChkInfo.health * 100 / tag->maxHealth, 1, 100);
        }
        return;
    }
    if (NEM.state != NEM_HUNTING || gRt.zone == ZONE_NONE || NEM.zone != gRt.zone) {
        return;
    }
    if (!gMf.settings.allowDungeons && gZones[gRt.zone].danger == 0 && NEM.level < 3) {
        return;
    }
    if (sManifestCooldown > 0) {
        return;
    }
    if (gRt.framesSinceSceneStart < (HAS_TRAIT(NEM.traits, TR_AMBUSHER) ? SEC(1) : SEC(5))) {
        return;
    }
    Manifest(play);
}

void Nemesis_OnSceneChange(PlayState* play) {
    s32 fledSpecies = Actors_TakeFledSpecies();

    // Ran away from the nemesis mid-fight?
    if (sPresentLastFrame && NEM.state == NEM_HUNTING) {
        char name[48];
        NEM.escapes++;
        if (HAS_TRAIT(NEM.traits, TR_GRUDGE)) {
            Nemesis_AddStat(STAT_POWER, 1);
        }
        if (Rng_Chance(25)) {
            Nemesis_GainTrait();
        }
        Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
        World_Logf2("You escaped ", name, ". It follows your trail.");
        NEM.travelSecs = 0;
    }
    sPresentLastFrame = false;
    sManifestCooldown = 0;

    // Or ran away from an ordinary enemy that now wants revenge?
    if (fledSpecies >= 0 && NEM.state == NEM_NONE && gMf.settings.nemesisEnabled) {
        s32 chance = gMf.settings.promotionChance / 2;
        if (Curse_Active(CURSE_PROMOTION)) {
            chance += 30;
        }
        if (Rng_Chance(chance)) {
            Nemesis_Promote(play, fledSpecies, 2, NULL);
            // It stays where you left it and starts hunting from there.
            if (gRt.prevSceneId >= 0 && World_ZoneForScene(gRt.prevSceneId) != ZONE_NONE) {
                NEM.zone = World_ZoneForScene(gRt.prevSceneId);
            }
        }
    }
}

void Nemesis_OnPlayerDeath(PlayState* play, Actor* killer) {
    ActorTag* tag = Tag_Get(killer);
    char name[48];
    char buf[LOG_WIDTH];
    s32 stat;

    if (killer == NULL || tag == NULL || !gMf.settings.nemesisEnabled) {
        return;
    }
    if (tag->kind == TAG_NEMESIS && NEM.state != NEM_NONE) {
        stat = Rng_Range(0, STAT_MAX - 1);
        NEM.kills++;
        Nemesis_AddStat(stat, Curse_Active(CURSE_VENDETTA) ? 2 : 1);
        if (Rng_Chance(50)) {
            Nemesis_GainTrait();
        }
        NEM.hpPercent = 100;
        Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
        buf[0] = '\0';
        Str_Cat(buf, name, LOG_WIDTH);
        Str_Cat(buf, " killed you. +", LOG_WIDTH);
        Str_Cat(buf, sStatNames[stat], LOG_WIDTH);
        World_Log(buf);
        return;
    }
    if (NEM.state == NEM_NONE) {
        s32 species = (tag->species < gSpeciesCount) ? tag->species : Roster_FindByActor(killer->id, killer->params);
        if (species >= 0 && (gSpecies[species].flags & SPF_ROAM)) {
            Nemesis_Promote(play, species, 0, NULL);
            Nemesis_AddStat(-1, 1);
            NEM.kills = 1;
        }
    }
}

void Nemesis_OnTaggedKilled(PlayState* play, Actor* actor, ActorTag* tag) {
    char name[48];
    s32 reward;

    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    if (actor->colChkInfo.health > 0) {
        // Despawned (object dropped, room unloaded...). It is still out there.
        return;
    }
    reward = 40 + 15 * NEM.level + 5 * TraitCount(NEM.traits) + NEM.stolenMarks * 2;
    reward = Marks_Scale(reward);
    Hud_Banner("NEMESIS DEFEATED", name);
    Marks_Add(reward, "nemesis defeated");
    NEM.stolenMarks = 0;

    if (Rng_Chance(HAS_TRAIT(NEM.traits, TR_COWARD) ? 60 : 25)) {
        NEM.state = NEM_DORMANT;
        NEM.dormantSecs = 600;
        NEM.scarred = true;
        NEM.hpPercent = 100;
        Nemesis_AddStat(STAT_HEALTH, 1);
        World_Logf2(name, " fell... but its body was never found.", NULL);
    } else {
        NEM.state = NEM_NONE;
        gMf.nemesesSlain++;
        World_Logf2(name, " is dead. Termina breathes easier.", NULL);
    }
    Save_MarkDirty();
}

void Nemesis_WorldTick(PlayState* play) {
    char name[48];
    s32 rate;
    s32 target;

    if (NEM.state == NEM_NONE) {
        return;
    }
    Nemesis_Name(name, sizeof(name), NEM.nameA, NEM.nameB, -1);
    if (NEM.state == NEM_DORMANT) {
        if (NEM.dormantSecs > 45) {
            NEM.dormantSecs -= 45;
        } else {
            NEM.dormantSecs = 0;
            NEM.state = NEM_HUNTING;
            World_Logf2(name, " stirs. It is hunting again.", NULL);
        }
        return;
    }
    if (Actors_FindTagged(TAG_NEMESIS, -1) != NULL) {
        return; // busy fighting you
    }
    // Nemeses prey on bounty targets they cross paths with.
    {
        s32 i;
        for (i = 0; i < MAX_BOUNTIES; i++) {
            BountyRec* b = &gMf.bounties[i];
            if (b->active && !b->claimed && b->zone == NEM.zone && Rng_Chance(20)) {
                char victim[48];
                char buf[LOG_WIDTH];
                Bounty_Name(victim, sizeof(victim), b);
                b->active = false;
                buf[0] = '\0';
                Str_Cat(buf, sNamesA[NEM.nameA % ARRAY_LEN(sNamesA)], LOG_WIDTH);
                Str_Cat(buf, " devoured ", LOG_WIDTH);
                Str_Cat(buf, victim, LOG_WIDTH);
                World_Log(buf);
                if (b->traits != 0 && Rng_Chance(50)) {
                    s32 t;
                    for (t = 0; t < NUM_TRAITS; t++) {
                        if (HAS_TRAIT(b->traits, t) && !HAS_TRAIT(NEM.traits, t)) {
                            NEM.traits |= 1u << t;
                            World_Logf2(sNamesA[NEM.nameA % ARRAY_LEN(sNamesA)], " absorbed the trait ", gTraits[t].name);
                            break;
                        }
                    }
                } else {
                    Nemesis_AddStat(-1, 1);
                }
                Bounty_Generate(b, -1);
                break;
            }
        }
    }
    Save_MarkDirty();
}
