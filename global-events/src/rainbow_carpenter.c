#include "common.h"

// ---------------------------------------------------------------------------
// Rainbow Plank Carpenter
//
// The carpenter carrying a plank in South Clock Town (En_Daiku type 3) is made
// rainbow colored and glowing, moves faster, and walks a loop around the Clock
// Tower instead of his normal path. Talking to him opens the mod menu. All of
// his effects can be switched off from the menu's Carpenter page. Everything is
// done with hooks, so it doesn't conflict with other mods.
// ---------------------------------------------------------------------------

// Mirror of the game's EnDaiku struct (z_en_daiku.h). Declared here because the
// real header pulls in a model header that isn't available to mods.
typedef struct {
    /* 0x000 */ Actor actor;
    /* 0x144 */ SkelAnime skelAnime;
    /* 0x188 */ Vec3s jointTable[17];
    /* 0x1EE */ Vec3s morphTable[17];
    /* 0x254 */ void* actionFunc;
    /* 0x258 */ Path* path;
    /* 0x25C */ s16 pathPoint;
    /* 0x25E */ s16 unk_25E;
    /* 0x260 */ s16 unk_260;
    /* 0x262 */ u8 unk_262[0x2];
    /* 0x264 */ s16 unk_264;
    /* 0x266 */ s16 unk_266;
    /* 0x268 */ u8 unk_268[0x4];
    /* 0x26C */ Vec3f targetPos;
    /* 0x278 */ s32 type;
    /* 0x27C */ s16 unk_27C;
    /* 0x27E */ s16 unk_27E;
    /* 0x280 */ s16 unk_280;
    /* 0x282 */ s16 targetYaw;
    /* 0x284 */ f32 animEndFrame;
    /* 0x288 */ s16 pathIndex;
    /* 0x28A */ s16 isTalking;
    /* 0x28C */ s16 unk_28C;
    /* 0x28E */ u8 unk_28E[0xE];
    /* 0x29C */ ColliderCylinder collider;
} EnDaikuMirror; // size = 0x2E8

_Static_assert(__builtin_offsetof(EnDaikuMirror, actionFunc) == 0x254, "EnDaiku layout");
_Static_assert(__builtin_offsetof(EnDaikuMirror, targetPos) == 0x26C, "EnDaiku layout");
_Static_assert(__builtin_offsetof(EnDaikuMirror, isTalking) == 0x28A, "EnDaiku layout");
_Static_assert(__builtin_offsetof(EnDaikuMirror, collider) == 0x29C, "EnDaiku layout");

#define DAIKU_TYPE_PLANK 3

#define BASE_WALK_SPEED 2.0f // the plank carpenter's normal speed, in units per frame
#define TINT_FOG_FAR 1600    // lower = stronger rainbow tint (game's hit flash uses 1700-4500)
#define GLOW_RADIUS 250
#define DEFAULT_RADIUS 420.0f
#define TOWER_MARGIN 70.0f

// ---------------------------------------------------------------------------
// State (only one plank carpenter exists, in South Clock Town)
// ---------------------------------------------------------------------------

static s32 sMeasured = false;
static Vec3f sCircleCenter;
static Vec3f sBoardSpot;
static s16 sBoardYaw = 0;
static s32 sBoardValid = false;

s32 Carpenter_BoardSpot(Vec3f* out, s16* yaw) {
    if (!sBoardValid) {
        return false;
    }
    *out = sBoardSpot;
    *yaw = sBoardYaw;
    return true;
}
static f32 sAutoRadius = DEFAULT_RADIUS;
static s16 sAngle;
static f32 sHeldY;
static s32 sHasFloor;
static f32 sTargetX;
static f32 sTargetZ;

static EnDaikuMirror* sUpdating = NULL;
static PlayState* sUpdatePlay = NULL;
static Actor* sDrawing = NULL;
static PlayState* sDrawPlay = NULL;

static LightInfo sLightInfo;
static LightNode* sLightNode = NULL;
static Actor* sLightOwner = NULL;

static Path* sOriginalPath = NULL;
static Actor* sPlankCarpenterThisFrame = NULL;
static Actor* sRainbowLast = NULL; // V3: the rainbow carpenter in this area (for the "Press L" prompt)

Actor* Carpenter_RainbowActor(void) {
    return ((sRainbowLast != NULL) && (sRainbowLast->update != NULL)) ? sRainbowLast : NULL;
}
static s32 sWasLooping = false;

static s32 IsPlankCarpenter(Actor* actor, PlayState* play) {
    return (play->sceneId == SCENE_CLOCKTOWER) && (actor->id == ACTOR_EN_DAIKU) &&
           (((EnDaikuMirror*)actor)->type == DAIKU_TYPE_PLANK) && !Events_IsEventActor(actor);
}

// ---------------------------------------------------------------------------
// Rainbow color helper
// ---------------------------------------------------------------------------

void HueToRgb(s32 hue, u8* r, u8* g, u8* b) {
    s32 region;
    s32 t;

    hue %= 360;
    if (hue < 0) {
        hue += 360;
    }
    region = hue / 60;
    t = (hue % 60) * 255 / 60;

    switch (region) {
        case 0:
            *r = 255, *g = t, *b = 0;
            break;
        case 1:
            *r = 255 - t, *g = 255, *b = 0;
            break;
        case 2:
            *r = 0, *g = 255, *b = t;
            break;
        case 3:
            *r = 0, *g = 255 - t, *b = 255;
            break;
        case 4:
            *r = t, *g = 0, *b = 255;
            break;
        default:
            *r = 255, *g = 0, *b = 255 - t;
            break;
    }
}

s32 BaseHue(PlayState* play) {
    return (s32)((play->gameplayFrames * 9) % 360);
}

// ---------------------------------------------------------------------------
// Measuring the Clock Tower
//
// Uses the tower's front doors (or clock face) as a reference point, steps
// inside the tower, and casts rays outward to find its walls. The center and
// size of the loop come from those wall hits.
// ---------------------------------------------------------------------------

static Actor* FindActor(PlayState* play, s32 category, s16 id, s32 wantType, s32 typeMask, s32 typeShift) {
    Actor* actor = play->actorCtx.actorLists[category].first;

    while (actor != NULL) {
        if ((actor->id == id) && (((actor->params & typeMask) >> typeShift) == wantType)) {
            return actor;
        }
        actor = actor->next;
    }
    return NULL;
}

static s32 RayHit(PlayState* play, Vec3f* from, Vec3f* to, Vec3f* hit) {
    CollisionPoly* poly;
    s32 bgId;

    return BgCheck_EntityLineTest1(&play->colCtx, from, to, hit, &poly, true, false, false, false, &bgId);
}

static void MeasureTower(PlayState* play, Actor* carpenter) {
    static const f32 sDepths[] = { 120.0f, 180.0f, 250.0f, 330.0f, 420.0f };
    Actor* ref;
    Vec3f refPos;
    s16 refYaw;
    f32 fx, fz, lx, lz;
    s32 d;

    // Fallback: somewhere sensible in front of the carpenter.
    sCircleCenter = carpenter->home.pos;
    sAutoRadius = DEFAULT_RADIUS;

    ref = FindActor(play, ACTORCAT_BG, ACTOR_OBJ_TOKEI_TOBIRA, 0, 1, 0);
    if (ref != NULL) {
        refPos = ref->home.pos;
        refYaw = ref->home.rot.y;
    } else {
        // Clock face (Obj_Tokeidai type 2), at plaza height.
        ref = FindActor(play, ACTORCAT_PROP, ACTOR_OBJ_TOKEIDAI, 2, 0xF000, 12);
        if (ref == NULL) {
            recomp_printf("[Rainbow Carpenter] Couldn't find the Clock Tower, using a default loop.\n");
            return;
        }
        refPos = ref->home.pos;
        refPos.y = carpenter->home.pos.y;
        refYaw = ref->home.rot.y;
    }

    // Forward axis points out of the tower, toward the plaza (where the
    // carpenter starts).
    fx = Math_SinS(refYaw);
    fz = Math_CosS(refYaw);
    if (((carpenter->home.pos.x - refPos.x) * fx + (carpenter->home.pos.z - refPos.z) * fz) < 0.0f) {
        fx = -fx;
        fz = -fz;
    }
    lx = fz;
    lz = -fx;

    // The Bounty Board carpenter stands out in the plaza, off to one side.
    sBoardSpot.x = refPos.x + fx * 260.0f + lx * 200.0f;
    sBoardSpot.y = refPos.y;
    sBoardSpot.z = refPos.z + fz * 260.0f + lz * 200.0f;
    sBoardYaw = Math_Atan2S_XY(-fz, -fx);
    sBoardValid = true;

    // Default guess if measuring fails: behind the reference point.
    sCircleCenter.x = refPos.x - fx * 250.0f;
    sCircleCenter.y = refPos.y;
    sCircleCenter.z = refPos.z - fz * 250.0f;

    for (d = 0; d < ARRAY_COUNT(sDepths); d++) {
        Vec3f inside;
        f32 axisDist[4]; // front, left, back, right (distance from the inside point to that wall)
        s32 axisFound[4];
        s32 axis;

        inside.x = refPos.x - fx * sDepths[d];
        inside.y = refPos.y + 50.0f;
        inside.z = refPos.z - fz * sDepths[d];

        // For each direction, cast three rays 10 degrees apart and take the
        // median distance, so one ray slipping through a doorway or alcove
        // doesn't throw off the result.
        for (axis = 0; axis < 4; axis++) {
            f32 dists[3];
            s32 count = 0;
            s32 k;

            for (k = -1; k <= 1; k++) {
                s16 a = (s16)(axis * 0x4000 + k * 0x071C); // 0x071C = 10 degrees
                f32 cf = Math_CosS(a);
                f32 cl = Math_SinS(a);
                f32 dirX = cf * fx + cl * lx;
                f32 dirZ = cf * fz + cl * lz;
                Vec3f end;
                Vec3f hit;

                end.x = inside.x + dirX * 900.0f;
                end.y = inside.y;
                end.z = inside.z + dirZ * 900.0f;

                if (RayHit(play, &inside, &end, &hit)) {
                    // Distance along the axis itself (projection).
                    f32 ax = Math_CosS(axis * 0x4000) * fx + Math_SinS(axis * 0x4000) * lx;
                    f32 az = Math_CosS(axis * 0x4000) * fz + Math_SinS(axis * 0x4000) * lz;

                    dists[count++] = (hit.x - inside.x) * ax + (hit.z - inside.z) * az;
                }
            }

            axisFound[axis] = (count > 0);
            if (count == 3) {
                f32 lo = MIN(dists[0], MIN(dists[1], dists[2]));
                f32 hi = MAX(dists[0], MAX(dists[1], dists[2]));
                axisDist[axis] = dists[0] + dists[1] + dists[2] - lo - hi;
            } else if (count == 2) {
                axisDist[axis] = MIN(dists[0], dists[1]);
            } else if (count == 1) {
                axisDist[axis] = dists[0];
            }
        }

        // Inside the tower, the front wall should be close by and both side
        // walls should be found.
        if (axisFound[0] && axisFound[1] && axisFound[3] && (axisDist[0] < sDepths[d] + 80.0f)) {
            f32 width = axisDist[1] + axisDist[3];
            f32 depth;
            f32 centerF;
            f32 centerL;

            if ((width < 250.0f) || (width > 1400.0f)) {
                continue;
            }

            centerL = (axisDist[1] - axisDist[3]) * 0.5f;
            depth = axisFound[2] ? (axisDist[0] + axisDist[2]) : width;
            if ((depth < width * 0.5f) || (depth > width * 2.0f)) {
                depth = width; // back wall missing or odd; assume a square tower
            }
            centerF = axisDist[0] - depth * 0.5f;

            sCircleCenter.x = inside.x + fx * centerF + lx * centerL;
            sCircleCenter.y = refPos.y;
            sCircleCenter.z = inside.z + fz * centerF + lz * centerL;
            sAutoRadius = sqrtf(SQ(width * 0.5f) + SQ(depth * 0.5f)) + TOWER_MARGIN;

            recomp_printf("[Rainbow Carpenter] Tower %d x %d, loop radius %d\n", (s32)width, (s32)depth,
                          (s32)sAutoRadius);
            return;
        }
    }

    recomp_printf("[Rainbow Carpenter] Couldn't measure the Clock Tower, using a default loop.\n");
}

static f32 CurrentRadius(void) {
    f32 configured = (f32)recomp_get_config_double("circle_radius");

    return (configured > 0.0f) ? configured : sAutoRadius;
}

// ---------------------------------------------------------------------------
// Fitting the loop to the ground
//
// Waypoints go around the tower at the loop radius. Where that spot is inside
// a wall or off a ledge, the waypoint moves outward until it's somewhere he
// can actually walk: on the ground, with no wall between it and the previous
// waypoint, and no big height jump. The path is built both ways from where he
// starts. If it can't close all the way around (the back of the tower isn't
// walkable), he walks the arc back and forth instead.
// ---------------------------------------------------------------------------

#define LOOP_POINTS 32
#define LOOP_MAX_STEP 60.0f // biggest height change between neighbouring waypoints

static Vec3f sLoop[LOOP_POINTS];
static s32 sLoopCount = 0;
static s32 sLoopClosed = false;
static s32 sLoopSeg = 0;   // walking from sLoop[sLoopSeg] toward the next waypoint
static s32 sLoopDir = 1;   // +1 or -1 along the waypoint list
static f32 sLoopDist = 0.0f; // distance already walked along the current stretch

static s32 Loop_WalkableBetween(PlayState* play, Vec3f* a, Vec3f* b) {
    Vec3f from = *a;
    Vec3f to = *b;
    Vec3f hit;

    if (fabsf(a->y - b->y) > LOOP_MAX_STEP) {
        return false;
    }
    // Knee height: low curbs and ramps are fine, walls are not.
    from.y += 30.0f;
    to.y += 30.0f;
    return !RayHit(play, &from, &to, &hit);
}

// Finds a walkable spot at this angle, starting at the loop radius and moving
// outward. Returns false if there is none.
static s32 Loop_FindPoint(PlayState* play, s16 angle, Vec3f* prev, Vec3f* out) {
    f32 r0 = CurrentRadius();
    f32 r;

    for (r = r0; r <= r0 + 500.0f; r += 25.0f) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f p;
        f32 floorY;

        p.x = sCircleCenter.x + Math_SinS(angle) * r;
        p.y = prev->y + 150.0f;
        p.z = sCircleCenter.z + Math_CosS(angle) * r;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &p);
        if (floorY <= BGCHECK_Y_MIN) {
            continue;
        }
        p.y = floorY;
        if (Loop_WalkableBetween(play, prev, &p)) {
            *out = p;
            return true;
        }
    }
    return false;
}

static void Loop_Build(PlayState* play, Actor* carpenter) {
    Vec3f back[LOOP_POINTS];
    Vec3f fwd[LOOP_POINTS];
    s32 nBack = 0;
    s32 nFwd = 0;
    s16 start = Math_Vec3f_Yaw(&sCircleCenter, &carpenter->world.pos);
    s16 step = 0x10000 / LOOP_POINTS;
    Vec3f prev;
    s32 i;

    // First waypoint: at his angle, starting from where he stands.
    prev = carpenter->world.pos;
    if (!Loop_FindPoint(play, start, &prev, &fwd[0])) {
        sLoopCount = 0;
        return;
    }
    nFwd = 1;

    // Forward around the tower until the way is blocked.
    prev = fwd[0];
    for (i = 1; i < LOOP_POINTS; i++) {
        if (!Loop_FindPoint(play, start + i * step, &prev, &fwd[nFwd])) {
            break;
        }
        prev = fwd[nFwd++];
    }
    // And backward from the start, for the waypoints the forward pass didn't reach.
    prev = fwd[0];
    for (i = 1; (i < LOOP_POINTS) && (nFwd + nBack < LOOP_POINTS); i++) {
        if (nFwd == LOOP_POINTS) {
            break;
        }
        if (!Loop_FindPoint(play, start - i * step, &prev, &back[nBack])) {
            break;
        }
        prev = back[nBack++];
        if (nFwd + nBack >= LOOP_POINTS) {
            break;
        }
    }

    // Back waypoints (farthest first), then the forward ones.
    sLoopCount = 0;
    for (i = nBack - 1; i >= 0; i--) {
        sLoop[sLoopCount++] = back[i];
    }
    for (i = 0; i < nFwd; i++) {
        sLoop[sLoopCount++] = fwd[i];
    }
    sLoopClosed = (sLoopCount >= 3) && Loop_WalkableBetween(play, &sLoop[sLoopCount - 1], &sLoop[0]);

    recomp_printf("[Rainbow Carpenter] Loop: %d waypoints, %s\n", sLoopCount, sLoopClosed ? "full circle" : "back and forth");
}

// Picks up the loop at the waypoint nearest to him.
static void Loop_Join(Actor* thisx) {
    f32 best = 0.0f;
    s32 i;

    sLoopSeg = 0;
    for (i = 0; i < sLoopCount; i++) {
        f32 d = Math_Vec3f_DistXZ(&sLoop[i], &thisx->world.pos);

        if ((i == 0) || (d < best)) {
            best = d;
            sLoopSeg = i;
        }
    }
    sLoopDist = 0.0f;
    if (!sLoopClosed && (sLoopSeg == sLoopCount - 1)) {
        sLoopDir = -1;
    } else if (!sLoopClosed && (sLoopSeg == 0)) {
        sLoopDir = 1;
    }
}

static s32 Loop_Next(s32 index) {
    s32 next = index + sLoopDir;

    if (sLoopClosed) {
        return (next + sLoopCount) % sLoopCount;
    }
    return next;
}

// Walks `dist` units along the path and returns the new spot and facing.
static void Loop_Walk(f32 dist, Vec3f* pos, s16* yaw) {
    Vec3f* a;
    Vec3f* b;
    f32 len;
    f32 t;
    s32 guard = 0;

    while (guard++ < 2 * LOOP_POINTS + 2) {
        s32 next = Loop_Next(sLoopSeg);

        if (!sLoopClosed && ((next < 0) || (next >= sLoopCount))) {
            sLoopDir = -sLoopDir; // end of the arc: turn around
            next = Loop_Next(sLoopSeg);
        }
        a = &sLoop[sLoopSeg];
        b = &sLoop[next];
        len = Math_Vec3f_DistXZ(a, b);
        if (sLoopDist + dist < len) {
            sLoopDist += dist;
            break;
        }
        dist -= len - sLoopDist;
        sLoopDist = 0.0f;
        sLoopSeg = next;
        if (dist <= 0.0f) {
            next = Loop_Next(sLoopSeg);
            if (!sLoopClosed && ((next < 0) || (next >= sLoopCount))) {
                sLoopDir = -sLoopDir;
                next = Loop_Next(sLoopSeg);
            }
            a = &sLoop[sLoopSeg];
            b = &sLoop[next];
            len = Math_Vec3f_DistXZ(a, b);
            break;
        }
    }
    t = (len > 0.01f) ? (sLoopDist / len) : 0.0f;
    pos->x = a->x + (b->x - a->x) * t;
    pos->y = a->y + (b->y - a->y) * t;
    pos->z = a->z + (b->z - a->z) * t;
    *yaw = Math_Vec3f_Yaw(a, b);
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

static f32 SpeedMultiplier(void) {
    switch (gOpt[ID_C_SPEED]) {
        case CSPEED_1X:
            return 1.0f;
        case CSPEED_4X:
            return 4.0f;
        default:
            return 2.0f;
    }
}

RECOMP_HOOK("EnDaiku_Update") void RainbowCarpenter_BeforeUpdate(Actor* thisx, PlayState* play) {
    EnDaikuMirror* this = (EnDaikuMirror*)thisx;
    f32 radius;
    f32 walkSpeed;
    Vec3f probe;
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY;

    sUpdating = NULL;
    sUpdatePlay = play;
    sPlankCarpenterThisFrame = NULL;
    if (!IsPlankCarpenter(thisx, play)) {
        return;
    }
    sPlankCarpenterThisFrame = thisx;
    sRainbowLast = thisx;

    if (!sMeasured) {
        sOriginalPath = this->path;
        MeasureTower(play, thisx);
        Loop_Build(play, thisx);
        sAngle = Math_Vec3f_Yaw(&sCircleCenter, &thisx->world.pos);
        sHeldY = thisx->world.pos.y;
        sWasLooping = false;
        sMeasured = true;
    }

    // Talking to him opens the mod menu instead of his normal dialogue. His
    // text ID is 0xFFFF (set after each update), which makes Link stop and face
    // him without any textbox. Consuming the talk flag here keeps his own code
    // from switching into its talking state.
    if (thisx->flags & ACTOR_FLAG_TALK) {
        thisx->flags &= ~ACTOR_FLAG_TALK;
        if (!gMenuOpen) {
            Menu_Open(play);
        }
    }

    if (!gOpt[ID_C_LOOP]) {
        // Back to his normal route, at his normal pace.
        this->skelAnime.playSpeed = 1.0f;
        if (sWasLooping) {
            this->targetPos = thisx->world.pos;
            sWasLooping = false;
        }
        if (gMenuOpen) {
            // Hold still and look at Link while the menu is up.
            this->path = NULL;
            this->targetPos = thisx->world.pos;
            thisx->world.rot.y = thisx->yawTowardsPlayer;
            this->targetYaw = thisx->world.rot.y;
        } else {
            this->path = sOriginalPath;
        }
        return;
    }

    this->skelAnime.playSpeed = SpeedMultiplier();

    // Keep updating even when the camera can't see him (e.g. behind the tower).
    thisx->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;

    if (!sWasLooping) {
        // (Re)joining the loop: start at whatever angle he's currently at.
        sAngle = Math_Vec3f_Yaw(&sCircleCenter, &thisx->world.pos);
        sHeldY = thisx->world.pos.y;
        if (sLoopCount >= 2) {
            Loop_Join(thisx);
        }
        sWasLooping = true;
    }

    if (sLoopCount >= 2) {
        // Walk the path fitted to the ground (see Loop_Build).
        Vec3f pos;
        s16 yaw;

        this->path = NULL;
        Loop_Walk((!gMenuOpen && !this->isTalking) ? BASE_WALK_SPEED * SpeedMultiplier() : 0.0f, &pos, &yaw);
        sTargetX = pos.x;
        sTargetZ = pos.z;
        sHeldY = pos.y;
        sHasFloor = true; // his own ground check follows ramps between waypoints
        thisx->world.pos.x = pos.x;
        thisx->world.pos.z = pos.z;
        this->targetPos = thisx->world.pos;
        if (gMenuOpen) {
            thisx->world.rot.y = thisx->yawTowardsPlayer;
        } else if (!this->isTalking) {
            thisx->world.rot.y = yaw;
        }
        this->targetYaw = thisx->world.rot.y;
        sUpdating = this;
        return;
    }

    // Stop following his normal path.
    this->path = NULL;

    radius = CurrentRadius();
    walkSpeed = BASE_WALK_SPEED * SpeedMultiplier();

    if (!gMenuOpen && !this->isTalking) {
        s16 step = (s16)((walkSpeed / radius) * (0x8000 / M_PI));

        sAngle += gOpt[ID_C_REVERSE] ? -step : step;
    }

    sTargetX = sCircleCenter.x + Math_SinS(sAngle) * radius;
    sTargetZ = sCircleCenter.z + Math_CosS(sAngle) * radius;

    // Follow the ground where there is some; otherwise keep his height (for
    // the stretch behind the tower that isn't part of South Clock Town).
    probe.x = sTargetX;
    probe.y = sHeldY + 60.0f;
    probe.z = sTargetZ;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    sHasFloor = (floorY > BGCHECK_Y_MIN) && (fabsf(floorY - sHeldY) < 60.0f);
    if (!sHasFloor) {
        // Second chance for ledges and raised platforms: hop up or down to
        // floor within 150 units instead of walking inside it.
        probe.y = sHeldY + 150.0f;
        floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        sHasFloor = (floorY > BGCHECK_Y_MIN) && (fabsf(floorY - sHeldY) < 150.0f);
    }
    if (sHasFloor) {
        sHeldY = floorY;
    }

    thisx->world.pos.x = sTargetX;
    thisx->world.pos.z = sTargetZ;
    thisx->world.pos.y = sHeldY;
    this->targetPos = thisx->world.pos;

    if (gMenuOpen) {
        // Look at Link while the menu is up.
        thisx->world.rot.y = thisx->yawTowardsPlayer;
        this->targetYaw = thisx->world.rot.y;
    } else if (!this->isTalking) {
        // Face along the loop.
        thisx->world.rot.y = sAngle + (gOpt[ID_C_REVERSE] ? -0x4000 : 0x4000);
        this->targetYaw = thisx->world.rot.y;
    }

    sUpdating = this;
}

static void RainbowCarpenter_RemoveLight(PlayState* play) {
    if (sLightNode != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sLightNode);
        sLightNode = NULL;
        sLightOwner = NULL;
    }
}

static void RainbowCarpenter_UpdateLight(PlayState* play, Actor* thisx) {
    u8 r, g, b;

    if (!gOpt[ID_C_GLOW]) {
        RainbowCarpenter_RemoveLight(play);
        return;
    }

    HueToRgb(BaseHue(play), &r, &g, &b);
    Lights_PointGlowSetInfo(&sLightInfo, thisx->world.pos.x, thisx->world.pos.y + 50.0f, thisx->world.pos.z, r, g, b,
                            GLOW_RADIUS);

    if (sLightNode == NULL) {
        sLightNode = LightContext_InsertLight(play, &play->lightCtx, &sLightInfo);
        sLightOwner = thisx;
    }
}

RECOMP_HOOK_RETURN("EnDaiku_Update") void RainbowCarpenter_AfterUpdate(void) {
    EnDaikuMirror* this = sUpdating;
    PlayState* play = sUpdatePlay;
    Actor* thisx;

    sUpdating = NULL;
    thisx = sPlankCarpenterThisFrame;
    sPlankCarpenterThisFrame = NULL;

    if ((thisx == NULL) || (play == NULL) || (thisx->update == NULL)) {
        return; // not him, or killed this frame
    }

    // Silent talk: Link stops and faces him, no textbox. See BeforeUpdate.
    thisx->textId = 0xFFFF;

    RainbowCarpenter_UpdateLight(play, thisx);

    if (this == NULL) {
        return; // not looping this frame
    }

    // Undo any wall push-out so he stays on the loop.
    thisx->world.pos.x = sTargetX;
    thisx->world.pos.z = sTargetZ;
    if (!sHasFloor) {
        thisx->world.pos.y = sHeldY;
        thisx->velocity.y = 0.0f;
    } else {
        sHeldY = thisx->world.pos.y;
    }
    thisx->shape.rot.y = thisx->world.rot.y;
}

RECOMP_HOOK("EnDaiku_Destroy") void RainbowCarpenter_OnDestroy(Actor* thisx, PlayState* play) {
    if ((thisx == sLightOwner) && (sLightNode != NULL)) {
        LightContext_RemoveLight(play, &play->lightCtx, sLightNode);
        sLightNode = NULL;
        sLightOwner = NULL;
    }
}

void Carpenter_OnPlayInit(PlayState* play) {
    sRainbowLast = NULL;
    sMeasured = false;
    sLightNode = NULL;
    sLightOwner = NULL;
    sUpdating = NULL;
    sDrawing = NULL;
    sOriginalPath = NULL;
    sWasLooping = false;
    sPlankCarpenterThisFrame = NULL;
}

void Carpenter_OnOptionChanged(PlayState* play, s32 id) {
    if ((id == ID_C_GLOW) && !gOpt[ID_C_GLOW]) {
        RainbowCarpenter_RemoveLight(play);
    }
}

// ---------------------------------------------------------------------------
// Rainbow tint
//
// Uses fog as a color overlay, the same way the game flashes enemies red when
// hit. Each body part gets its own hue, and the hues cycle over time.
// ---------------------------------------------------------------------------

RECOMP_HOOK("EnDaiku_Draw") void RainbowCarpenter_BeforeDraw(Actor* thisx, PlayState* play) {
    u8 r, g, b;

    sDrawing = NULL;
    if (!(IsPlankCarpenter(thisx, play) || Events_IsRainbowCarpenter(thisx)) || !gOpt[ID_C_RAINBOW]) {
        return;
    }

    HueToRgb(BaseHue(play), &r, &g, &b);

    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, r, g, b, 255);
    gSPFogPosition(POLY_OPA_DISP++, 0, TINT_FOG_FAR);
    CLOSE_DISPS(play->state.gfxCtx);

    sDrawing = thisx;
    sDrawPlay = play;
}

RECOMP_HOOK("EnDaiku_OverrideLimbDraw") void RainbowCarpenter_BeforeLimb(PlayState* play, s32 limbIndex, Gfx** dList,
                                                                        Vec3f* pos, Vec3s* rot, Actor* thisx) {
    u8 r, g, b;

    if ((sDrawing == NULL) || (thisx != sDrawing)) {
        return;
    }

    HueToRgb(BaseHue(play) + limbIndex * 22, &r, &g, &b);

    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, r, g, b, 255);
    CLOSE_DISPS(play->state.gfxCtx);
}

RECOMP_HOOK_RETURN("EnDaiku_Draw") void RainbowCarpenter_AfterDraw(void) {
    PlayState* play = sDrawPlay;

    if ((sDrawing == NULL) || (play == NULL)) {
        return;
    }
    sDrawing = NULL;

    // Restore the area's normal fog for everything drawn after him.
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------
// V3: every owl statue glows a slow, pulsing purple (press L by one to open
// the Moon Menu). Drawing only: the statue itself is untouched.
// ---------------------------------------------------------------------------

static PlayState* sOwlPlay = NULL;
static s32 sOwlTinted = false;

RECOMP_HOOK("ObjWarpstone_Draw") void Owl_BeforeDraw(Actor* thisx, PlayState* play) {
    s32 pulse = (s32)((Math_SinS((s16)(play->gameplayFrames * 0x500)) + 1.0f) * 400.0f);

    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetFogColor(POLY_OPA_DISP++, 170, 70, 255, 255);
    gSPFogPosition(POLY_OPA_DISP++, 0, 1900 + pulse);
    CLOSE_DISPS(play->state.gfxCtx);
    sOwlPlay = play;
    sOwlTinted = true;
}

RECOMP_HOOK_RETURN("ObjWarpstone_Draw") void Owl_AfterDraw(void) {
    PlayState* play = sOwlPlay;

    if (!sOwlTinted || (play == NULL)) {
        return;
    }
    sOwlTinted = false;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
    CLOSE_DISPS(play->state.gfxCtx);
}
