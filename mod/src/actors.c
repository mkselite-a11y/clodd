#include "mf.h"

// Every actor in the game gets a small ActorTag attached through the recomp
// actor extension API. Tags mark our own spawns (nemesis, bounty targets,
// ambushers) and also track ordinary enemies so we can tell when one has
// earned a promotion.

static ActorExtensionId sTagExt;
static u8 sExtReady = false;

// Objects we added as persistent entries for this scene (tail of the list).
static s16 sPreloaded[3];
static s32 sPreloadedCount = 0;
static u8 sPreloadStart = 0;

// The most dangerous enemy the player is currently tangled up with. Used to
// detect "you ran from me" promotions when the player leaves the area.
static s32 sEngagedSpecies = -1;
static s32 sEngagedFrames = 0;

static s32 sSiphonCooldown = 0;

void Actors_OnInit(void) {
    sTagExt = z64recomp_extend_actor_all(sizeof(ActorTag));
    sExtReady = true;
}

ActorTag* Tag_Get(Actor* actor) {
    if (!sExtReady || actor == NULL) {
        return NULL;
    }
    return (ActorTag*)z64recomp_get_extended_actor_data(actor, sTagExt);
}

s32 Actors_IsEnemyLike(Actor* a) {
    return (a != NULL) && (a->category == ACTORCAT_ENEMY || (a->flags & ACTOR_FLAG_HOSTILE));
}

s32 Actors_SpeciesObjectReady(PlayState* play, s32 species) {
    s32 slot;
    if (species < 0 || species >= gSpeciesCount) {
        return false;
    }
    slot = Object_GetSlot(&play->objectCtx, gSpecies[species].objectId);
    return (slot > OBJECT_SLOT_NONE) && Object_IsLoaded(&play->objectCtx, slot);
}

Actor* Actors_SpawnSpecies(PlayState* play, s32 species, Vec3f* pos, s16 yaw, u8 kind, u8 index) {
    const Species* sp;
    Actor* actor;
    ActorTag* tag;

    if (species < 0 || species >= gSpeciesCount || !Actors_SpeciesObjectReady(play, species)) {
        return NULL;
    }
    sp = &gSpecies[species];
    actor = Actor_Spawn(&play->actorCtx, play, sp->actorId, pos->x, pos->y + sp->spawnYOffset, pos->z, 0, yaw, 0,
                        sp->params);
    if (actor == NULL) {
        return NULL;
    }
    tag = Tag_Get(actor);
    if (tag != NULL) {
        tag->kind = kind;
        tag->index = index;
        tag->species = species;
        tag->applied = false;
    }
    if (kind == TAG_NEMESIS || kind == TAG_BOUNTY) {
        actor->room = -1; // keep it around across room changes
    }
    return actor;
}

s32 Actors_CountTagged(u8 kind, s32 index) {
    PlayState* play = gRt.play;
    s32 cat;
    s32 n = 0;
    if (play == NULL) {
        return 0;
    }
    for (cat = 0; cat < ACTORCAT_MAX; cat++) {
        Actor* a = play->actorCtx.actorLists[cat].first;
        while (a != NULL) {
            ActorTag* tag = Tag_Get(a);
            if (tag != NULL && tag->kind == kind && (index < 0 || tag->index == index) && a->update != NULL) {
                n++;
            }
            a = a->next;
        }
    }
    return n;
}

Actor* Actors_FindTagged(u8 kind, s32 index) {
    PlayState* play = gRt.play;
    s32 cat;
    if (play == NULL) {
        return NULL;
    }
    for (cat = 0; cat < ACTORCAT_MAX; cat++) {
        Actor* a = play->actorCtx.actorLists[cat].first;
        while (a != NULL) {
            ActorTag* tag = Tag_Get(a);
            if (tag != NULL && tag->kind == kind && (index < 0 || tag->index == index) && a->update != NULL) {
                return a;
            }
            a = a->next;
        }
    }
    return NULL;
}

void Actors_KillTagged(u8 kind) {
    PlayState* play = gRt.play;
    s32 cat;
    if (play == NULL) {
        return;
    }
    for (cat = 0; cat < ACTORCAT_MAX; cat++) {
        Actor* a = play->actorCtx.actorLists[cat].first;
        while (a != NULL) {
            ActorTag* tag = Tag_Get(a);
            if (tag != NULL && tag->kind == kind && a->update != NULL) {
                tag->kind = TAG_NONE; // vanishing, not dying
                Actor_Kill(a);
            }
            a = a->next;
        }
    }
}

// ---------------------------------------------------------------------------
// Object preloading
// ---------------------------------------------------------------------------

static s32 ObjectSize(s16 id) {
    return (s32)(gObjectTable[id].vromEnd - gObjectTable[id].vromStart);
}

char gPreloadNote[64];

static void PreloadNote(const char* what, s16 objectId, s32 freeKB) {
    s32 i;
    const char* name = "?";
    for (i = 0; i < gSpeciesCount; i++) {
        if (gSpecies[i].objectId == objectId) {
            name = gSpecies[i].name;
            break;
        }
    }
    gPreloadNote[0] = '\0';
    Str_Cat(gPreloadNote, name, sizeof(gPreloadNote));
    Str_Cat(gPreloadNote, what, sizeof(gPreloadNote));
    Str_CatInt(gPreloadNote, freeKB, sizeof(gPreloadNote));
    Str_Cat(gPreloadNote, "KB free", sizeof(gPreloadNote));
}

// maxKB: largest model we're willing to add for this purpose.
static void TryPreload(PlayState* play, s16 objectId, s32 maxKB) {
    ObjectContext* ctx = &play->objectCtx;
    s32 size;
    s32 i;
    s32 freeKB;
    uintptr_t next;

    if (objectId <= 0 || Object_GetSlot(ctx, objectId) > OBJECT_SLOT_NONE) {
        return; // already part of this area
    }
    for (i = 0; i < sPreloadedCount; i++) {
        if (sPreloaded[i] == objectId) {
            return;
        }
    }
    if (sPreloadedCount >= 3 || ctx->numEntries >= ARRAY_COUNT(ctx->slots) - 8) {
        return;
    }
    size = ObjectSize(objectId);
    next = (uintptr_t)ctx->slots[ctx->numEntries].segment;
    freeKB = (next == 0) ? 0 : (s32)(((uintptr_t)ctx->spaceEnd - next) / 1024);
    if (size <= 0 || size > maxKB * 1024) {
        PreloadNote(" too big to add, ", objectId, freeKB);
        return;
    }
    // Keep room for the area's own models. If a room still needs more, the
    // object-list hook below drops our extras before it loads.
    if (next == 0 || freeKB * 1024 < size + 320 * 1024) {
        PreloadNote(" skipped, only ", objectId, freeKB);
        return;
    }
    if (sPreloadedCount == 0) {
        sPreloadStart = ctx->numEntries;
    }
    Object_SpawnPersistent(ctx, objectId);
    sPreloaded[sPreloadedCount++] = objectId;
    PreloadNote(" model loaded, ", objectId, freeKB - size / 1024);
}

void Actors_PreloadForScene(PlayState* play) {
    s32 zone = World_ZoneForScene(play->sceneId);
    s32 i;
    s32 pass;

    sPreloadedCount = 0;
    if (!gMf.settings.crossZoneObjects || zone == ZONE_NONE) {
        return;
    }
    // Nemesis first: it is here, or one step away and likely to arrive.
    if (gMf.settings.nemesisEnabled && gMf.nemesis.state == NEM_HUNTING &&
        World_Distance(gMf.nemesis.zone, zone) <= 1) {
        TryPreload(play, gSpecies[gMf.nemesis.species].objectId, 400);
    }
    // Claimed bounty targets here, then ones next door that may wander in.
    if (gMf.settings.bountiesEnabled) {
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < MAX_BOUNTIES; i++) {
                BountyRec* b = &gMf.bounties[i];
                s32 d;
                if (!b->active || !b->claimed) {
                    continue;
                }
                d = World_Distance(b->zone, zone);
                if (d == pass) {
                    TryPreload(play, gSpecies[b->species].objectId, 400);
                }
            }
        }
    }
    // One roaming monster for the Ambush event, so it can happen in areas
    // that have no enemies of their own. Harder monsters in wilder zones.
    if (gMf.settings.eventsEnabled && gMf.events[20].enabled) {
        s32 species = Roster_RandomRoaming(MF_CLAMP(2 + gZones[zone].danger, 2, 6));
        TryPreload(play, gSpecies[species].objectId, gMf.settings.objectBudgetKB);
    }
}

// Safety valve: if a room's object list would not fit next to our extra
// objects, drop ours before the room loads. Our actors using them simply
// vanish (they are treated as having slipped away, not killed).
RECOMP_HOOK("Scene_CommandObjectList") void Mf_BeforeObjectList(PlayState* play, SceneCmd* cmd) {
    ObjectContext* ctx = &play->objectCtx;
    s16* list;
    s32 needed = 0;
    s32 i;
    uintptr_t start;
    uintptr_t freeBytes;

    if (sPreloadedCount == 0 || ctx->numPersistentEntries != sPreloadStart + sPreloadedCount) {
        return;
    }
    list = Lib_SegmentedToVirtual(cmd->objectList.segment);
    for (i = 0; i < cmd->objectList.num; i++) {
        needed += ALIGN16(ObjectSize(list[i]));
    }
    start = (uintptr_t)ctx->slots[ctx->numPersistentEntries].segment;
    freeBytes = (uintptr_t)ctx->spaceEnd - start;
    if ((uintptr_t)needed + 16 * 1024 <= freeBytes) {
        return;
    }
    ctx->numPersistentEntries = sPreloadStart;
    sPreloadedCount = 0;
    Str_Copy(gPreloadNote, "Extra models dropped: a room needed the space", sizeof(gPreloadNote));
    recomp_printf("[moonfall] dropped preloaded objects to fit room objects\n");
}

// ---------------------------------------------------------------------------
// Per-actor hooks
// ---------------------------------------------------------------------------

RECOMP_CALLBACK("*", recomp_after_actor_init) void Mf_AfterActorInit(PlayState* play, Actor* actor) {
    ActorTag* tag = Tag_Get(actor);
    if (tag == NULL) {
        return;
    }
    if (tag->kind == TAG_NEMESIS) {
        Nemesis_ApplyStats(actor, tag);
    } else if (tag->kind == TAG_BOUNTY) {
        Bounty_ApplyStats(actor, tag);
    }
    tag->maxHealth = actor->colChkInfo.health;
    tag->lastHealth = actor->colChkInfo.health;
    tag->applied = true;
}

static s16 sPlayerHealthPreUpdate;

RECOMP_CALLBACK("*", recomp_should_actor_update) void Mf_ShouldActorUpdate(PlayState* play, Actor* actor, bool* should) {
    if (actor->id == ACTOR_PLAYER) {
        sPlayerHealthPreUpdate = gSaveContext.save.saveInfo.playerData.health;
        return;
    }
    // Stasis Orb: every hostile actor in the area holds still.
    if (Pouch_BuffActive(PI_STASIS_ORB) && actor->category == ACTORCAT_ENEMY) {
        *should = false;
    }
}

static void OnPlayerUpdated(PlayState* play, Player* player) {
    s32 lost = sPlayerHealthPreUpdate - gSaveContext.save.saveInfo.playerData.health;
    Actor* ac = player->cylinder.base.ac;

    if (lost <= 0) {
        return;
    }
    if (ac != NULL && ac != &player->actor && Actors_IsEnemyLike(ac)) {
        ActorTag* tag = Tag_Get(ac);
        gRt.lastAttacker = ac;
        if (tag != NULL && tag->hitsOnPlayer < 255) {
            tag->hitsOnPlayer++;
        }
        Nemesis_OnPlayerHit(play, ac, lost);
        // Riposte Charm: return part of the blow.
        if (Pouch_BuffActive(PI_RIPOSTE_CHARM) && ac->colChkInfo.health > 1) {
            ac->colChkInfo.health = MF_MAX(1, ac->colChkInfo.health - 2);
            Actor_SetColorFilter(ac, COLORFILTER_COLORFLAG_RED, 255, COLORFILTER_BUFFLAG_OPA, 8);
        }
    }
}

// Health lost by a tracked enemy since last frame (i.e. the player hit it).
static void OnEnemyHealthChange(PlayState* play, Actor* actor, ActorTag* tag) {
    s32 hp = actor->colChkInfo.health;
    s32 drop = tag->lastHealth - hp;

    if (drop > 0) {
        tag->damagedByPlayer = true;
        if ((tag->kind == TAG_NEMESIS || tag->kind == TAG_BOUNTY) && hp > 0) {
            // Trait defences (Ironhide, Moonblessed) only soften non-lethal hits.
            s32 kept = Nemesis_FilterDamage(actor, tag, drop);
            hp = MF_CLAMP(tag->lastHealth - kept, 1, tag->maxHealth);
            if (Pouch_BuffActive(PI_HUNTERS_WHETSTONE) && hp > 1) {
                hp = MF_MAX(1, hp - (drop + 1) / 2);
            }
            actor->colChkInfo.health = hp;
        }
        if (gPromoteNextHit && tag->kind == TAG_NONE && hp > 0) {
            s32 species = Roster_FindByActor(actor->id, actor->params);
            if (species >= 0 && (gSpecies[species].flags & SPF_ROAM)) {
                gPromoteNextHit = false;
                Nemesis_Promote(play, species, 4, actor);
            }
        }
        if (Pouch_BuffActive(PI_HEART_SIPHON) && sSiphonCooldown <= 0) {
            Mf_HealPlayer(1);
            sSiphonCooldown = 10;
        }
    }
    tag->lastHealth = actor->colChkInfo.health;
}

RECOMP_CALLBACK("*", recomp_after_actor_update) void Mf_AfterActorUpdate(PlayState* play, Actor* actor) {
    ActorTag* tag;

    if (!gRt.loaded) {
        return;
    }
    if (actor->id == ACTOR_PLAYER) {
        OnPlayerUpdated(play, (Player*)actor);
        return;
    }
    if (!Actors_IsEnemyLike(actor)) {
        return;
    }
    tag = Tag_Get(actor);
    if (tag == NULL || !tag->applied) {
        if (tag != NULL && tag->kind == TAG_NONE) {
            tag->applied = true;
            tag->maxHealth = actor->colChkInfo.health;
            tag->lastHealth = actor->colChkInfo.health;
            tag->species = 0xFF;
        }
        return;
    }
    tag->aliveFrames++;
    if (actor->xzDistToPlayer < 350.0f) {
        tag->aggroFrames++;
    }
    OnEnemyHealthChange(play, actor, tag);

    switch (tag->kind) {
        case TAG_NEMESIS:
            Nemesis_ActorUpdate(play, actor, tag);
            break;
        case TAG_BOUNTY:
            // Bounty targets use the same trait engine as nemeses.
            Nemesis_ActorUpdate(play, actor, tag);
            break;
        default:
            break;
    }
}

// Deaths (health 0) and despawns (health left) both end in Actor_Kill.
RECOMP_HOOK("Actor_Kill") void Mf_OnActorKill(Actor* actor) {
    ActorTag* tag = Tag_Get(actor);
    PlayState* play = gRt.play;

    if (tag == NULL || play == NULL || actor->update == NULL) {
        return;
    }
    switch (tag->kind) {
        case TAG_NEMESIS:
            Nemesis_OnTaggedKilled(play, actor, tag);
            break;
        case TAG_BOUNTY:
            Bounty_OnTaggedKilled(play, actor, tag);
            break;
        default:
            break;
    }
    tag->kind = TAG_NONE;
}

// ---------------------------------------------------------------------------
// Promotion watch: ordinary enemies earning a nemesis title
// ---------------------------------------------------------------------------

void Actors_Update(PlayState* play) {
    Actor* a;
    s32 bestSpecies = -1;
    s32 bestDifficulty = -1;

    if (sSiphonCooldown > 0) {
        sSiphonCooldown--;
    }
    a = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
    while (a != NULL) {
        ActorTag* tag = Tag_Get(a);
        if (tag != NULL && tag->applied && a->update != NULL && tag->kind == TAG_NONE) {
            s32 species = Roster_FindByActor(a->id, a->params);
            if (species >= 0 && (gSpecies[species].flags & SPF_ROAM)) {
                tag->species = species;
                if (a->xzDistToPlayer < 350.0f && (tag->hitsOnPlayer > 0 || tag->damagedByPlayer) &&
                    gSpecies[species].difficulty > bestDifficulty) {
                    bestDifficulty = gSpecies[species].difficulty;
                    bestSpecies = species;
                }
                if (gMf.nemesis.state == NEM_NONE && gMf.settings.nemesisEnabled) {
                    s32 chance = gMf.settings.promotionChance;
                    if (Curse_Active(CURSE_PROMOTION)) {
                        chance = MF_MIN(100, chance + 30);
                    }
                    // Hit the player three times and lived: humiliation promotes.
                    if (tag->hitsOnPlayer >= (Curse_Active(CURSE_PROMOTION) ? 2 : 3)) {
                        tag->hitsOnPlayer = 0;
                        if (Rng_Chance(chance)) {
                            Nemesis_Promote(play, species, 1, a);
                        }
                    }
                    // Beaten down to a quarter health, but still standing after 20 seconds.
                    else if (tag->damagedByPlayer && tag->maxHealth >= 2 &&
                             a->colChkInfo.health * 4 <= tag->maxHealth && a->colChkInfo.health > 0) {
                        tag->aggroFrames++;
                        if (tag->aggroFrames > SEC(20)) {
                            tag->damagedByPlayer = false;
                            if (Rng_Chance(chance / 2)) {
                                Nemesis_Promote(play, species, 3, a);
                            }
                        }
                    }
                }
            }
        }
        a = a->next;
    }
    if (bestSpecies >= 0) {
        sEngagedSpecies = bestSpecies;
        sEngagedFrames = SEC(3);
    } else if (sEngagedFrames > 0) {
        sEngagedFrames--;
    }
}

// Called by the nemesis module when the scene changes.
s32 Actors_TakeFledSpecies(void) {
    s32 s = (sEngagedFrames > 0) ? sEngagedSpecies : -1;
    sEngagedSpecies = -1;
    sEngagedFrames = 0;
    return s;
}
