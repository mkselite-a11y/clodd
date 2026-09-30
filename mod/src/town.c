#include "mf.h"

// From z_en_daiku.h (not included: it depends on extracted ROM assets).
#define ENDAIKU_GET_FF(thisx) ((thisx)->params & 0xFF)
#define ENDAIKU_PARAM_FF_3 3

void Town_SetNear(s32 which);

static Actor* sCarpenter = NULL;
static Actor* sOwl = NULL;

// The plank-carrying carpenter is the path-walking EnDaiku variant (type 3):
// it is the only one that draws the plank on its shoulder limb.
static Actor* FindCarpenter(PlayState* play) {
    Actor* best = NULL;
    Actor* a = play->actorCtx.actorLists[ACTORCAT_NPC].first;

    while (a != NULL) {
        if (a->id == ACTOR_EN_DAIKU && a->update != NULL) {
            if (ENDAIKU_GET_FF(a) == ENDAIKU_PARAM_FF_3) {
                return a;
            }
            if (best == NULL) {
                best = a;
            }
        }
        a = a->next;
    }
    return best;
}

static Actor* FindOwl(PlayState* play) {
    s32 cat;
    for (cat = 0; cat < ACTORCAT_MAX; cat++) {
        Actor* a = play->actorCtx.actorLists[cat].first;
        while (a != NULL) {
            if (a->id == ACTOR_OBJ_WARPSTONE && a->update != NULL) {
                return a;
            }
            a = a->next;
        }
    }
    return NULL;
}

static s32 IsNear(Actor* a, Player* player, f32 range) {
    return (a != NULL) && (Actor_WorldDistXZToActor(a, &player->actor) < range) &&
           (fabsf(a->world.pos.y - player->actor.world.pos.y) < 90.0f);
}

void Town_Update(PlayState* play) {
    Player* player = GET_PLAYER(play);

    Town_SetNear(0);
    if (play->sceneId != SCENE_CLOCKTOWER) {
        sCarpenter = NULL;
        sOwl = NULL;
        return;
    }
    if ((gRt.frame % 20) == 0 || sCarpenter == NULL || sCarpenter->update == NULL) {
        sCarpenter = FindCarpenter(play);
    }
    if ((gRt.frame % 20) == 0 || sOwl == NULL || sOwl->update == NULL) {
        sOwl = FindOwl(play);
    }
    if (!gRt.gameplayOk) {
        return;
    }
    if (IsNear(sCarpenter, player, 120.0f)) {
        Town_SetNear(1);
        Hud_SetPrompt("Press L: Moon Menu");
    } else if (IsNear(sOwl, player, 130.0f)) {
        Town_SetNear(2);
        Hud_SetPrompt("Press L: Bounty Board");
    }
}

// Rainbow tint: set the fog colour/position right before the carpenter draws,
// the same way the game tints actors that were just hit, then restore it.
static PlayState* sTinted = NULL;

static void TintBegin(PlayState* play, MfColor c, s32 strength) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, c.r, c.g, c.b, 255);
    gSPFogPosition(POLY_OPA_DISP++, 0, strength);
    CLOSE_DISPS(play->state.gfxCtx);
    sTinted = play;
}

static void TintEnd(void) {
    if (sTinted != NULL) {
        func_800AE5A0(sTinted);
        sTinted = NULL;
    }
}

RECOMP_HOOK("EnDaiku_Draw") void Mf_BeforeDaikuDraw(Actor* thisx, PlayState* play) {
    if (play->sceneId == SCENE_CLOCKTOWER && thisx == sCarpenter) {
        TintBegin(play, Mf_Rainbow(gRt.frame * 9, 255), 1500);
    }
}

// Return hooks get no arguments; the tint remembers which PlayState it used.
RECOMP_HOOK_RETURN("EnDaiku_Draw") void Mf_AfterDaikuDraw(void) {
    TintEnd();
}

RECOMP_HOOK("ObjWarpstone_Draw") void Mf_BeforeOwlDraw(Actor* thisx, PlayState* play) {
    if (play->sceneId == SCENE_CLOCKTOWER && gMf.settings.bountiesEnabled) {
        // Pulsing moon-purple glow marks the owl statue as the Bounty Board.
        s32 pulse = (gRt.frame * 8) & 0xFF;
        MfColor c = { 150, 60, 230, 255 };
        if (pulse > 127) {
            pulse = 255 - pulse;
        }
        TintBegin(play, c, 2400 - pulse * 4);
    }
}

RECOMP_HOOK_RETURN("ObjWarpstone_Draw") void Mf_AfterOwlDraw(void) {
    TintEnd();
}
