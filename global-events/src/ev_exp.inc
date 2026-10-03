// ===========================================================================
// Experimental friends (Friend Link sandbox, V3.1.5)
//
// A friend the player set to "Experimental" can send "x|op|0|0|k:v,k:v,...|NAME".
// Every op reads its own settings by key; anything left out gets a default.
// Off limits by design: nothing here adds or removes inventory items, upgrades,
// heart pieces/containers, keys or masks, and nothing spawns an NPC or chest
// that could hand out a randomizer check. Rupees, hearts and magic refills are fine.
// The player can undo everything from the Friends tab (Undo Experimental stuff).
// ===========================================================================

ExpFx gExp = { { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, 1.0f, 0.0f, false, false };

enum {
    XOP_SIZE = 1,     // x y z h (head) t
    XOP_MOVE = 2,     // sp (speed) g (gravity) t
    XOP_LAUNCH = 3,   // up out dir (0 random, 1 forward, 2 back)
    XOP_TELE = 4,     // x z (the spot a friend clicked)
    XOP_STATUS = 6,   // inv frz chill flip wob (seconds each)
    XOP_HURT = 7,     // d (quarter hearts) dt (damage type 0-4) kb (knockback)
    XOP_ARROWS = 9,   // t n (bombs per arrow) r (spread) sp (arrow speed) kg (powder kegs)
    XOP_SPAWN = 10,   // p n s hp tr ch (chase) spin boom (bombs on death) frz r x z
    XOP_ALL = 11,     // s hp frz kill boom up ch spin (every enemy in the room)
    XOP_NEM = 12,     // here lv (level 0-255) pt (points to add) st0-st3 (stats 0-50) s ch heal kill tr (traits to add)
    XOP_TIME = 30,    // hh mm ts (time speed) t
    XOP_ENV = 31,     // st (1 dark 2 red 3 orange 4 cold 5 purple 6 green) f (strength 0-1) t
    XOP_CRITTER = 34, // c n s t
    XOP_FOV = 35,     // f t
    XOP_EVENT = 40,   // ev cb d e (empower) force
    XOP_EVCTL = 41,   // ext nx
    XOP_V3P = 42,     // i v clr
    XOP_MARKS = 43,   // m (set) d (add)
    XOP_DIR = 45,     // b (director budget multiplier)
    XOP_BANNER = 50,  // the whole text is the banner
    XOP_SFX = 51,     // id n iv
    XOP_PANIC = 99,
};

// --- reading "k:v,k:v" --------------------------------------------------------

static f32 Exp_Atof(const char* s) {
    f32 v = 0.0f;
    f32 frac = 0.0f;
    f32 div = 1.0f;
    s32 neg = false;

    if (*s == '-') {
        neg = true;
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        v = v * 10.0f + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while ((*s >= '0') && (*s <= '9')) {
            frac = frac * 10.0f + (*s - '0');
            div *= 10.0f;
            s++;
        }
    }
    v += frac / div;
    return neg ? -v : v;
}

static s32 Exp_Has(const char* text, const char* key) {
    const char* p = text;
    s32 n = 0;

    while (key[n] != '\0') {
        n++;
    }
    while (*p != '\0') {
        s32 i = 0;

        while ((i < n) && (p[i] == key[i])) {
            i++;
        }
        if ((i == n) && (p[n] == ':') && ((p == text) || (p[-1] == ','))) {
            return (s32)(p - text) + n + 1;
        }
        p++;
    }
    return -1;
}

static f32 Exp_F(const char* text, const char* key, f32 def) {
    s32 at = Exp_Has(text, key);

    return (at >= 0) ? Exp_Atof(text + at) : def;
}

static s32 Exp_I(const char* text, const char* key, s32 def) {
    return (s32)Exp_F(text, key, (f32)def);
}

#define EXP_SEC(v) ((s32)(CLAMP((v), 0.0f, 300.0f) * FPS)) // (no timer runs past 5 minutes)

// --- what's going on -------------------------------------------------------------

typedef struct {
    Actor* actor;
    f32 scale;  // drawn this much bigger
    f32 chase;  // steps toward Link this fast
    s16 spin;   // turns this much a frame
    s16 frozen; // frames it stands still
    u8 boom;    // bombs when it dies
    u8 spawned; // a friend made it (worth nothing)
    Vec3f last;
} ExpActor;

#define EXP_ACTORS 48
static ExpActor sXA[EXP_ACTORS];

typedef struct {
    Actor* actor;
    Vec3f last;
    u8 fired;
} ExpArrow;

#define EXP_ARROWS 10
static ExpArrow sXArrow[EXP_ARROWS];

static s32 sXSizeT, sXMoveT, sXInvT, sXFlipT, sXWobT;
static s32 sXArrowT, sXArrowN, sXArrowKeg;
static f32 sXArrowR, sXArrowSp;
static s32 sXTimeT, sXTimeOld, sXTimeSet;
static s32 sXEnvT, sXEnvSt;
static f32 sXEnvF;
static s32 sXFovT;
static f32 sXFov;
static s32 sXSfxId, sXSfxN, sXSfxIv, sXSfxT;

static s32 Exp_AnyActive(void) {
    s32 i;

    for (i = 0; i < EXP_ACTORS; i++) {
        if (sXA[i].actor != NULL) {
            return true;
        }
    }
    return (sXSizeT | sXMoveT | sXInvT | sXFlipT | sXWobT | sXArrowT | sXTimeT | sXEnvT | sXFovT | sXSfxN) != 0 ||
           (sFreezeFrames > 0) ||
           (gExp.scale[0] != 1.0f) || (gExp.scale[1] != 1.0f) || (gExp.scale[2] != 1.0f) || (gExp.head != 1.0f) ||
           (gExp.speed != 1.0f) || (gExp.gravity != 1.0f) ||
           (sDirMult != 1.0f) || (sV3OvrMask != 0);
}

static ExpActor* Exp_Track(Actor* actor) {
    s32 i;
    s32 free = -1;

    for (i = 0; i < EXP_ACTORS; i++) {
        if (sXA[i].actor == actor) {
            return &sXA[i];
        }
        if ((sXA[i].actor == NULL) && (free < 0)) {
            free = i;
        }
    }
    if (free < 0) {
        return NULL;
    }
    bzero(&sXA[free], sizeof(ExpActor));
    sXA[free].actor = actor;
    sXA[free].scale = 1.0f;
    sXA[free].last = actor->world.pos;
    return &sXA[free];
}

// Drawn size (Events_BeforeActorDraw), and whether it holds still (Events_ShouldActorUpdate).
static f32 Exp_DrawScale(Actor* actor) {
    s32 i;

    for (i = 0; i < EXP_ACTORS; i++) {
        if (sXA[i].actor == actor) {
            return sXA[i].scale;
        }
    }
    return 1.0f;
}

static s32 Exp_IsTracked(Actor* actor) {
    s32 i;

    for (i = 0; i < EXP_ACTORS; i++) {
        if (sXA[i].actor == actor) {
            return true;
        }
    }
    return false;
}

static void Exp_ForgetColliders(Actor* actor);
static void Exp_RestoreColliders(PlayState* play);

static s32 Exp_Frozen(Actor* actor) {
    s32 i;

    for (i = 0; i < EXP_ACTORS; i++) {
        if ((sXA[i].actor == actor) && (sXA[i].frozen > 0)) {
            return true;
        }
    }
    return false;
}

// --- no pickups from a friend's enemies ---------------------------------------------
// Where one died, pickups that show up nearby in the next 2 seconds are removed.

#define EXP_NODROP 8
static Vec3f sXNoDropAt[EXP_NODROP];
static s16 sXNoDropT[EXP_NODROP];

static void Exp_NoDrops(Vec3f* at) {
    s32 i;
    s32 best = 0;

    for (i = 0; i < EXP_NODROP; i++) {
        if (sXNoDropT[i] < sXNoDropT[best]) {
            best = i;
        }
    }
    sXNoDropAt[best] = *at;
    sXNoDropT[best] = 2 * FPS;
}

static void Exp_NoDropsUpdate(PlayState* play) {
    s32 i;

    for (i = 0; i < EXP_NODROP; i++) {
        Actor* a;

        if (sXNoDropT[i] <= 0) {
            continue;
        }
        sXNoDropT[i]--;
        for (a = play->actorCtx.actorLists[ACTORCAT_MISC].first; a != NULL; a = a->next) {
            if ((a->id == ACTOR_EN_ITEM00) && (a->update != NULL) &&
                (Ev_DistXZ(&a->world.pos, &sXNoDropAt[i]) < 120.0f) &&
                (fabsf(a->world.pos.y - sXNoDropAt[i].y) < 200.0f)) {
                Actor_Kill(a);
            }
        }
    }
}

// --- explosions -------------------------------------------------------------------

static void Exp_Bomb(PlayState* play, Vec3f* at, s32 fuse, s32 keg) {
    Actor* a = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, at->x, at->y, at->z,
                           keg ? BOMB_EXPLOSIVE_TYPE_POWDER_KEG : BOMB_EXPLOSIVE_TYPE_BOMB, 0, 0, BOMB_TYPE_BODY);

    if (a != NULL) {
        ((EnBom*)a)->timer = (s16)CLAMP(fuse, 1, 300);
    }
}

static void Exp_Burst(PlayState* play, Vec3f* at, s32 n, f32 r, s32 fuse, s32 keg) {
    s32 i;

    n = CLAMP(n, 1, 24);
    for (i = 0; i < n; i++) {
        Vec3f p = *at;

        if ((i > 0) && (r > 0.0f)) {
            s16 yaw = (s16)(Rand_ZeroOne() * 0xFFFF);
            f32 d = r * (0.3f + 0.7f * Rand_ZeroOne());

            p.x += Math_SinS(yaw) * d;
            p.z += Math_CosS(yaw) * d;
        }
        Exp_Bomb(play, &p, fuse + i, keg && (i == 0));
    }
}

// --- the per-frame part -----------------------------------------------------------

static void Exp_Panic(PlayState* play) {
    s32 i;

    Exp_RestoreColliders(play); // hitboxes back to their own size first

    for (i = 0; i < EXP_ACTORS; i++) {
        if ((sXA[i].actor != NULL) && (play != NULL) && V2_IsLive(play, sXA[i].actor) &&
            ((Tag_Of(sXA[i].actor) == TAG_V2) || sXA[i].spawned)) {
            Ev_KillOne(play, sXA[i].actor); // what friends spawned goes; your own world stays
        }
        sXA[i].actor = NULL;
    }
    bzero(sXArrow, sizeof(sXArrow));
    gChillTimer = 0;
    gExp.scale[0] = gExp.scale[1] = gExp.scale[2] = 1.0f;
    gExp.head = gExp.speed = gExp.gravity = 1.0f;
    sXSizeT = sXMoveT = sXFlipT = sXWobT = 0;
    sXArrowT = sXFovT = sXSfxN = 0;
    // Status holds go too: frozen Link moves again, borrowed invincibility ends.
    if (play != NULL) {
        Freeze_Stop(play);
        if ((sXInvT > 0) && (GET_PLAYER(play) != NULL) && (GET_PLAYER(play)->invincibilityTimer < 0)) {
            GET_PLAYER(play)->invincibilityTimer = 0;
        }
    }
    sXInvT = 0;
    if (sXTimeT > 0) {
        gSaveContext.save.timeSpeedOffset = sXTimeOld;
    }
    sXTimeT = 0;
    if ((sXEnvT > 0) && (play != NULL) && (sActive < 0)) {
        Ev_ClearEnv(play);
    }
    sXEnvT = 0;
    sDirMult = 1.0f;
    sV3OvrMask = 0;
}

static void Exp_Update(PlayState* play, s32 playing) {
    Player* player = GET_PLAYER(play);
    Input* in = &play->state.input[0];
    s32 i;

    if ((player == NULL) || !playing) {
        return;
    }
    Exp_NoDropsUpdate(play);
    // Timers that put things back on their own.
    if ((sXSizeT > 0) && (--sXSizeT == 0)) {
        gExp.scale[0] = gExp.scale[1] = gExp.scale[2] = 1.0f;
        gExp.head = 1.0f;
    }
    if ((sXMoveT > 0) && (--sXMoveT == 0)) {
        gExp.speed = gExp.gravity = 1.0f;
    }
    if (sXInvT > 0) {
        sXInvT--;
        if (sXInvT == 0) {
            if (player->invincibilityTimer < 0) {
                player->invincibilityTimer = 0; // (handed back cleanly)
            }
        } else if (player->invincibilityTimer >= 0) {
            player->invincibilityTimer = -2; // (negative: no flashing)
        }
    }
    if (sXFlipT > 0) {
        sXFlipT--;
        in->cur.stick_x = -in->cur.stick_x;
        in->cur.stick_y = -in->cur.stick_y;
        in->rel.stick_x = -in->rel.stick_x;
        in->rel.stick_y = -in->rel.stick_y;
    }
    if (sXWobT > 0) {
        s32 wx = (s32)(Math_SinS((s16)(play->gameplayFrames * 0x0900)) * 45.0f);
        s32 wy = (s32)(Math_CosS((s16)(play->gameplayFrames * 0x0700)) * 45.0f);

        sXWobT--;
        in->cur.stick_x = (s8)CLAMP(in->cur.stick_x + wx, -80, 80);
        in->cur.stick_y = (s8)CLAMP(in->cur.stick_y + wy, -80, 80);
        in->rel.stick_x = in->cur.stick_x;
        in->rel.stick_y = in->cur.stick_y;
    }
    if (sXTimeT > 0) {
        gSaveContext.save.timeSpeedOffset = sXTimeSet;
        if (--sXTimeT == 0) {
            gSaveContext.save.timeSpeedOffset = sXTimeOld;
        }
    }
    if (sXEnvT > 0) {
        Ev_ApplyEnv(play, (EnvStyle)CLAMP(sXEnvSt, ENV_DARK, ENV_GREEN), CLAMP(sXEnvF, 0.0f, 1.0f));
        if ((--sXEnvT == 0) && (sActive < 0)) {
            Ev_ClearEnv(play);
        }
    }
    if (sXFovT > 0) {
        Camera* cam = GET_ACTIVE_CAM(play);

        sXFovT--;
        if (cam != NULL) {
            cam->fov = CLAMP(sXFov, 5.0f, 170.0f);
        }
    }
    if (sXSfxN > 0) {
        if (--sXSfxT <= 0) {
            Audio_PlaySfx((u16)sXSfxId);
            sXSfxN--;
            sXSfxT = MAX(sXSfxIv, 1);
        }
    }
    // Juiced arrows: Link's arrows burst into bombs where they land.
    if (sXArrowT > 0) {
        Actor* a = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].first;

        sXArrowT--;
        for (; a != NULL; a = a->next) {
            if ((a->id != ACTOR_EN_ARROW) || (a->parent != &player->actor) || (a->params >= 6) /* arrows only, not nuts or bubbles */) {
                continue;
            }
            for (i = 0; i < EXP_ARROWS; i++) {
                if (sXArrow[i].actor == a) {
                    break;
                }
            }
            if (i == EXP_ARROWS) {
                for (i = 0; i < EXP_ARROWS; i++) {
                    if (sXArrow[i].actor == NULL) {
                        sXArrow[i].actor = a;
                        sXArrow[i].fired = false;
                        sXArrow[i].last = a->world.pos;
                        break;
                    }
                }
            }
        }
    }
    for (i = 0; i < EXP_ARROWS; i++) {
        ExpArrow* x = &sXArrow[i];

        if (x->actor == NULL) {
            continue;
        }
        if (!Ev_IsLiveActor(play, x->actor)) {
            if (x->fired) {
                Exp_Burst(play, &x->last, sXArrowN, sXArrowR, 1, sXArrowKeg);
            }
            x->actor = NULL;
            continue;
        }
        if (!x->fired && (x->actor->speed > 60.0f)) {
            x->fired = true;
            x->actor->speed *= CLAMP(sXArrowSp, 0.1f, 6.0f);
        } else if (x->fired && (x->actor->speed < 30.0f)) {
            // It hit something (stuck in a wall or an enemy): boom.
            Exp_Burst(play, &x->actor->world.pos, sXArrowN, sXArrowR, 1, sXArrowKeg);
            Actor_Kill(x->actor);
            x->actor = NULL;
            continue;
        }
        x->last = x->actor->world.pos;
    }
    // Tracked enemies: chase, spin, freeze, and bombs when they die.
    for (i = 0; i < EXP_ACTORS; i++) {
        ExpActor* x = &sXA[i];

        if (x->actor == NULL) {
            continue;
        }
        if (!V2_IsLive(play, x->actor)) {
            if (x->boom > 0) {
                Exp_Burst(play, &x->last, x->boom, 80.0f, 1, false);
            }
            if (x->spawned) {
                Exp_NoDrops(&x->last);
            }
            Exp_ForgetColliders(x->actor);
            x->actor = NULL;
            continue;
        }
        if (x->frozen > 0) {
            x->frozen--;
        } else {
            if (x->chase > 0.0f) {
                Ev_StepToward(play, x->actor, &player->actor.world.pos, x->chase);
            }
            if (x->spin != 0) {
                x->actor->shape.rot.y += x->spin;
                x->actor->world.rot.y = x->actor->shape.rot.y;
            }
        }
        x->last = x->actor->world.pos;
    }
}

// --- the ops -------------------------------------------------------------------

static s32 Exp_SpotFromText(PlayState* play, const char* text, Vec3f* out) {
    if ((Exp_Has(text, "x") < 0) || (Exp_Has(text, "z") < 0)) {
        return false;
    }
    return Link_GroundAt(play, Exp_F(text, "x", 0.0f), Exp_F(text, "z", 0.0f), out);
}

static void Exp_ApplyToActor(ExpActor* x, const char* text) {
    if (x == NULL) {
        return;
    }
    if (Exp_Has(text, "s") >= 0) {
        x->scale = CLAMP(Exp_F(text, "s", 1.0f), 0.05f, 20.0f);
    }
    if (Exp_Has(text, "ch") >= 0) {
        x->chase = CLAMP(Exp_F(text, "ch", 0.0f), 0.0f, 40.0f);
    }
    if (Exp_Has(text, "spin") >= 0) {
        x->spin = (s16)(CLAMP(Exp_F(text, "spin", 0.0f), -64.0f, 64.0f) * 0x100);
    }
    if (Exp_Has(text, "boom") >= 0) {
        x->boom = (u8)CLAMP(Exp_I(text, "boom", 0), 0, 12);
    }
    if (Exp_Has(text, "frz") >= 0) {
        x->frozen = (s16)EXP_SEC(Exp_F(text, "frz", 0.0f));
    }
}

static Actor* Exp_SpawnOne(PlayState* play, s32 pool, Vec3f* at, f32 radius) {
    if (pool == POOL_ODOLWA) {
        return Ev_SpawnOdolwa(play);
    }
    if ((pool < 0) || (pool >= POOL_COUNT)) {
        return NULL;
    }
    return (at != NULL) ? V2_SpawnEnemy(play, pool, at, 0.0f, 0.0f, TAG_V2)
                        : V2_SpawnEnemy(play, pool, NULL, MAX(radius * 0.4f, 80.0f), MAX(radius, 150.0f), TAG_V2);
}

// Which room a spot belongs to, going by the nearest door or room boundary (indoors).
static s32 Exp_RoomAt(PlayState* play, f32 x, f32 z) {
    TransitionActorEntry* best = NULL;
    f32 bestD = 1e9f;
    s32 i;

    for (i = 0; i < play->transitionActors.count; i++) {
        TransitionActorEntry* e = &play->transitionActors.list[i];
        f32 d;

        if ((e->sides[0].room == e->sides[1].room) || (e->sides[0].room < 0) || (e->sides[1].room < 0)) {
            continue; // (a door out of the area, or within one room)
        }
        d = SQ(x - e->pos.x) + SQ(z - e->pos.z);
        if (d < bestD) {
            bestD = d;
            best = e;
        }
    }
    if (best == NULL) {
        return play->roomCtx.curRoom.num;
    }
    // Front (side 0) is the way the door faces.
    return ((x - best->pos.x) * Math_SinS(best->rotY) + (z - best->pos.z) * Math_CosS(best->rotY) >= 0.0f)
               ? best->sides[0].room
               : best->sides[1].room;
}

// Where a click-to-teleport lands, or why it can't ("!" + reason, shown to the friend).
static const char* Exp_TeleSpot(PlayState* play, f32 x, f32 z, Vec3f* out) {
    static const f32 sFrom[3] = { 100.0f, 400.0f, 1500.0f };
    Player* player = GET_PLAYER(play);
    f32 bestY = BGCHECK_Y_MIN;
    s32 found = false;
    s32 i;
    f32 waterY;
    WaterBox* waterBox;

    if ((player->rideActor != NULL) || (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_20000000 | PLAYER_STATE1_2000 | PLAYER_STATE1_200000))) {
        return "!busy (cutscene, climbing or Epona)";
    }
    // Every floor under the spot (one per probe height); the one nearest their height wins,
    // so a click on open ground doesn't land on a roof above it.
    for (i = 0; i < 3; i++) {
        CollisionPoly* poly;
        s32 bgId;
        Vec3f probe = { x, player->actor.world.pos.y + sFrom[i], z };
        f32 y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);

        if ((y > BGCHECK_Y_MIN) && (!found || (fabsf(y - player->actor.world.pos.y) < fabsf(bestY - player->actor.world.pos.y)))) {
            Vec3f at = { x, y, z };
            CollisionPoly* cPoly;
            s32 cBgId;
            f32 ceilY;

            // Room to stand: no ceiling right on top of that floor.
            if (BgCheck_EntityCheckCeiling(&play->colCtx, &ceilY, &at, 60.0f, &cPoly, &cBgId, &player->actor)) {
                continue;
            }
            bestY = y;
            found = true;
        }
    }
    if (!found) {
        return "!no ground at that spot";
    }
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) && (waterY > bestY + 40.0f)) {
        return "!that spot is under water";
    }
    if ((play->roomList.count > 1) && (Exp_RoomAt(play, x, z) != play->roomCtx.curRoom.num)) {
        return "!that spot is in another room";
    }
    out->x = x;
    out->y = bestY;
    out->z = z;
    return NULL;
}

static const char* Exp_Run(PlayState* play, s32 op, const char* t) {
    Player* player = GET_PLAYER(play);
    s32 i;

    if (player == NULL) {
        return NULL;
    }
    switch (op) {
        case XOP_SIZE:
            gExp.scale[0] = CLAMP(Exp_F(t, "x", 1.0f), 0.1f, 15.0f);
            gExp.scale[1] = CLAMP(Exp_F(t, "y", 1.0f), 0.1f, 15.0f);
            gExp.scale[2] = CLAMP(Exp_F(t, "z", 1.0f), 0.1f, 15.0f);
            gExp.head = CLAMP(Exp_F(t, "h", 1.0f), 0.1f, 15.0f);
            sXSizeT = EXP_SEC(Exp_F(t, "t", 30.0f));
            return "Size changed";
        case XOP_MOVE:
            gExp.speed = CLAMP(Exp_F(t, "sp", 1.0f), 0.0f, 8.0f);
            gExp.gravity = CLAMP(Exp_F(t, "g", 1.0f), -1.0f, 6.0f);
            sXMoveT = EXP_SEC(Exp_F(t, "t", 30.0f));
            return "Movement changed";
        case XOP_LAUNCH: {
            s32 dir = Exp_I(t, "dir", 0);
            s16 yaw = (dir == 1)   ? player->actor.shape.rot.y
                      : (dir == 2) ? (s16)(player->actor.shape.rot.y + 0x8000)
                                   : (s16)(Rand_ZeroOne() * 0xFFFF);
            f32 out = CLAMP(Exp_F(t, "out", 10.0f), 0.0f, 60.0f);

            player->actor.velocity.y = CLAMP(Exp_F(t, "up", 25.0f), -40.0f, 80.0f);
            player->actor.world.rot.y = yaw;
            player->speedXZ = out;
            player->actor.speed = out;
            return "Launched";
        }
        case XOP_TELE: {
            Vec3f to;
            const char* why;

            if ((Exp_Has(t, "x") < 0) || (Exp_Has(t, "z") < 0)) {
                return "!no spot picked";
            }
            why = Exp_TeleSpot(play, Exp_F(t, "x", 0.0f), Exp_F(t, "z", 0.0f), &to);
            if (why != NULL) {
                return why;
            }
            player->actor.world.pos = to;
            player->actor.prevPos = to;
            player->actor.velocity.x = player->actor.velocity.y = player->actor.velocity.z = 0.0f;
            player->actor.speed = 0.0f;
            player->speedXZ = 0.0f;
            return "Teleported";
        }
        case XOP_STATUS:
            if (Exp_Has(t, "inv") >= 0) {
                sXInvT = EXP_SEC(Exp_F(t, "inv", 0.0f));
            }
            if (Exp_Has(t, "frz") >= 0) {
                // Short holds only: a frozen Link stops the clock too.
                Freeze_Start(play, (s32)(CLAMP(Exp_F(t, "frz", 0.0f), 0.0f, 20.0f) * FPS));
            }
            if (Exp_Has(t, "chill") >= 0) {
                gChillTimer = EXP_SEC(Exp_F(t, "chill", 0.0f));
            }
            if (Exp_Has(t, "flip") >= 0) {
                sXFlipT = EXP_SEC(Exp_F(t, "flip", 0.0f));
            }
            if (Exp_Has(t, "wob") >= 0) {
                sXWobT = EXP_SEC(Exp_F(t, "wob", 0.0f));
            }
            return "Status changed";
        case XOP_HURT: {
            s32 d = CLAMP(Exp_I(t, "d", 4), 0, 80);
            s32 dt = CLAMP(Exp_I(t, "dt", 0), 0, 4);
            f32 kb = CLAMP(Exp_F(t, "kb", 0.0f), 0.0f, 30.0f);

            if ((kb > 0.0f) || (dt > 0)) {
                func_800B8D10(play, NULL, kb, (s16)(Rand_ZeroOne() * 0xFFFF), kb * 0.6f, dt, d * 4);
            } else {
                Health_ChangeBy(play, (s16)(-d * 4));
                Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 180, COLORFILTER_BUFFLAG_OPA, 8);
                Audio_PlaySfx(NA_SE_VO_LI_DAMAGE_S);
            }
            return "Hurt";
        }
        case XOP_ARROWS:
            sXArrowT = EXP_SEC(Exp_F(t, "t", 60.0f));
            sXArrowN = CLAMP(Exp_I(t, "n", 3), 1, 24);
            sXArrowR = CLAMP(Exp_F(t, "r", 100.0f), 0.0f, 800.0f);
            sXArrowSp = CLAMP(Exp_F(t, "sp", 1.0f), 0.1f, 6.0f);
            sXArrowKeg = Exp_I(t, "kg", 0) != 0;
            return "Arrows juiced";
        case XOP_SPAWN: {
            s32 pool = Exp_I(t, "p", 3);
            s32 n = CLAMP(Exp_I(t, "n", 1), 1, 20);
            f32 hp = CLAMP(Exp_F(t, "hp", 1.0f), 0.05f, 50.0f);
            u16 tr = (u16)CLAMP(Exp_I(t, "tr", 0), 0, 0xFFFF);
            f32 r = CLAMP(Exp_F(t, "r", 400.0f), 100.0f, 3000.0f);
            Vec3f at;
            s32 haveAt = Exp_SpotFromText(play, t, &at);
            s32 made = 0;
            s32 here = 0;
            Actor* e0;

            for (e0 = play->actorCtx.actorLists[ACTORCAT_ENEMY].first; e0 != NULL; e0 = e0->next) {
                here += (e0->update != NULL); // every enemy, friends' spawns included
            }

            if (here >= 40) {
                return "Too many enemies here (40)";
            }
            n = MIN(n, 40 - here);

            for (i = 0; i < n; i++) {
                Actor* e = Exp_SpawnOne(play, pool, haveAt ? &at : NULL, r);

                if (e == NULL) {
                    continue;
                }
                made++;
                if (Tag_Get(e) != NULL) {
                    V2_ApplyTraits(e, tr & ~(1u << TR_RETIRED), hp);
                    Tag_Get(e)->v3flags |= TAGF3_EXP;
                }
                e->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
                {
                    ExpActor* xa = Exp_Track(e);

                    Exp_ApplyToActor(xa, t);
                    if (xa != NULL) {
                        xa->spawned = true;
                    }
                }
                if (haveAt) {
                    at.x += 40.0f; // (don't stack them all on one spot)
                }
            }
            return (made > 0) ? "Spawned" : "Couldn't spawn that here";
        }
        case XOP_ALL: {
            s32 count = 0;
            Actor* a = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

            if (Exp_I(t, "kill", 0) != 0) {
                // The menu's own Kill All (it knows which bosses must be left alone).
                return (Events_KillAllEnemies(play) > 0) ? "Killed every enemy" : "No enemies here";
            }

            while (a != NULL) {
                Actor* next = a->next;

                if ((a->update != NULL) && (a->colChkInfo.health > 0)) {
                    count++;
                    if (Exp_I(t, "kill", 0) != 0) {
                        Ev_KillOne(play, a);
                        a = next;
                        continue;
                    }
                    if (Exp_Has(t, "hp") >= 0) {
                        a->colChkInfo.health = (u8)CLAMP(Exp_I(t, "hp", 1), 1, 255);
                    }
                    if (Exp_Has(t, "up") >= 0) {
                        a->velocity.y = CLAMP(Exp_F(t, "up", 15.0f), -40.0f, 60.0f);
                    }
                    if (Exp_I(t, "bnow", 0) != 0) {
                        Exp_Bomb(play, &a->world.pos, 1, false);
                    }
                    Exp_ApplyToActor(Exp_Track(a), t);
                }
                a = next;
            }
            return (count > 0) ? "Changed every enemy" : "No enemies here";
        }
        case XOP_NEM:
            // The sender's own Nemesis (the first friend's is the default one).
            sNemCur = MAX(Nem_SlotForFriend(sLinkFrom), 0);
            if (sNem.pool < 0) {
                sNemCur = 0;
                return "No Nemesis";
            }
            for (i = 0; i < NST_COUNT; i++) {
                static const char* sKeys[NST_COUNT] = { "st0", "st1", "st2", "st3" }; // Health, Power, Speed, Tracking

                if (Exp_Has(t, sKeys[i]) >= 0) {
                    sNem.stat[i] = (u8)CLAMP(Exp_I(t, sKeys[i], 0), 0, NEM_STAT_MAX);
                }
            }
            if (Exp_Has(t, "lv") >= 0) { // its level (traits owed follow it)
                sNem.level = (u8)CLAMP(Exp_I(t, "lv", 0), 0, NEM_LEVEL_MAX);
                sNem.picks = (u8)CLAMP(sNem.level / 5 - sNem.traitCount, 0, MAX(Nem_TraitsTotal() - sNem.traitCount, 0));
                sNem.pickTimer = (sNem.picks > 0) ? NEM_DECIDE : 0;
            }
            if (Exp_I(t, "pt", 0) > 0) {
                Nem_GainPoints(CLAMP(Exp_I(t, "pt", 0), 0, 50), NULL); // unspent: friends or the game spend them
            }
            for (i = 0; i < CLAMP(Exp_I(t, "tr", 0), 0, 6); i++) {
                Nem_GiftTrait(); // random traits on top of its own
            }
            if (Exp_I(t, "here", 0) != 0) {
                Link_TrySpawnNemNear(play);
            }
            Nem_RefreshLive(play);
            if ((sNem.actor != NULL) && V2_IsLive(play, sNem.actor)) {
                if (Exp_I(t, "kill", 0) != 0) {
                    Ev_KillOne(play, sNem.actor);
                } else {
                    if (Exp_I(t, "heal", 0) != 0) {
                        TagData* td = Tag_Get(sNem.actor);

                        if ((td != NULL) && (td->maxHp > 0)) {
                            sNem.actor->colChkInfo.health = (u8)MIN(td->maxHp, 255);
                        }
                    }
                    Exp_ApplyToActor(Exp_Track(sNem.actor), t);
                }
            }
            sNemCur = 0;
            return "Nemesis changed";
        case XOP_TIME:
            if ((Exp_Has(t, "hh") >= 0) || (Exp_Has(t, "mm") >= 0)) {
                gSaveContext.save.time =
                    (u16)CLOCK_TIME(CLAMP(Exp_I(t, "hh", 12), 0, 23), CLAMP(Exp_I(t, "mm", 0), 0, 59));
            }
            if (Exp_Has(t, "ts") >= 0) {
                if (sXTimeT == 0) {
                    sXTimeOld = gSaveContext.save.timeSpeedOffset;
                }
                sXTimeSet = CLAMP(Exp_I(t, "ts", 0), -3, 60);
                sXTimeT = MAX(EXP_SEC(Exp_F(t, "t", 30.0f)), 1);
            }
            return "Time changed";
        case XOP_ENV:
            sXEnvSt = CLAMP(Exp_I(t, "st", 2), 1, 6);
            sXEnvF = CLAMP(Exp_F(t, "f", 0.8f), 0.0f, 1.0f);
            sXEnvT = EXP_SEC(Exp_F(t, "t", 20.0f));
            return "Sky changed";
        case XOP_CRITTER: {
            s32 c = CLAMP(Exp_I(t, "c", 0), 0, CRIT_COUNT - 1);
            s32 n = CLAMP(Exp_I(t, "n", 5), 1, 20);
            s32 life = EXP_SEC(Exp_F(t, "t", 30.0f));
            const SpawnDef* def = &sCritters[c];

            for (i = 0; i < n; i++) {
                Vec3f spot;
                Actor* a;

                if (!Ev_FindSpot(play, 60.0f, 300.0f, player->actor.shape.rot.y, 0x7FFF, false, &spot)) {
                    continue;
                }
                spot.y += def->yOffset;
                a = Ev_Spawn(play, def->actorId, def->objectId, &spot, 0, (s16)(Rand_ZeroOne() * 0xFFFF), def->rotZ,
                             def->params, TAG_EVENT);
                if (a != NULL) {
                    if (Tag_Get(a) != NULL) {
                        Tag_Get(a)->v3flags |= TAGF3_EXP;
                    }
                    Linger_Add(a, MAX(life, FPS));
                    {
                        ExpActor* xa = Exp_Track(a);

                        Exp_ApplyToActor(xa, t);
                        if (xa != NULL) {
                            xa->spawned = true; // (undo removes it too)
                        }
                    }
                }
            }
            return "Critters";
        }
        case XOP_FOV:
            sXFov = Exp_F(t, "f", 60.0f);
            sXFovT = EXP_SEC(Exp_F(t, "t", 15.0f));
            return "Camera changed";
        case XOP_EVENT: {
            s32 ev = Exp_I(t, "ev", 0);
            s32 cb = Exp_I(t, "cb", -1);
            s32 d = Exp_I(t, "d", 0);

            if ((ev < 0) || (ev >= EV_COUNT) || (ev == EV_BULLETHELL) || (ev == EV_INFIGHT) || (ev == EV_LEVIATHAN)) {
                return "No such event"; // (Bullet Hell, Infighting and Sky Leviathan were removed)
            }
            if ((cb == ev) && Ev_CanDouble(ev)) {
                // doubled (X combo X): runs once, twice as fierce
            } else if ((cb < 0) || (cb >= EV_COUNT) || (cb == ev) || (cb == EV_BULLETHELL) || (cb == EV_INFIGHT) ||
                       (cb == EV_LEVIATHAN) || (V3_IsEvent(ev) && V3_IsEvent(cb))) {
                cb = -1; // (two of the newer events can't share their one set of state)
            }
            if (!Exp_I(t, "force", 0) && !Ev_CanRun(play, ev)) {
                return "That event can't run here";
            }
            if (Exp_I(t, "e", 0) != 0) {
                sFriendEmpowerNext = true;
            }
            sRouletteTimer = 0;
            sStartByFriend = true;
            sExpStartPending = true; // no Marks, no contracts
            Ev_StartFull(play, ev, cb, false);
            if (d > 0) {
                sTotalFrames = sFramesLeft = CLAMP(d, 1, 300) * FPS;
            }
            return "Event started";
        }
        case XOP_EVCTL:
            if (Exp_Has(t, "nx") >= 0) {
                sUntilNext = CLAMP(Exp_I(t, "nx", 10), 1, 300) * FPS;
            }
            if (sActive < 0) {
                return "Next event moved";
            }
            if (Exp_Has(t, "ext") >= 0) {
                sFramesLeft = CLAMP(sFramesLeft + Exp_I(t, "ext", 30) * FPS, 1, 300 * FPS);
                sTotalFrames = MAX(sTotalFrames, sFramesLeft);
            }
            return "Event changed";
        case XOP_V3P:
            if (Exp_I(t, "clr", 0) != 0) {
                sV3OvrMask = 0;
                return "Event tuning cleared";
            }
            i = CLAMP(Exp_I(t, "i", 0), 0, 2);
            sV3Ovr[i] = (s16)CLAMP(Exp_I(t, "v", 0), -999, 9999);
            sV3OvrMask |= (1 << i);
            return "Event tuning set";
        case XOP_MARKS:
            if (Exp_Has(t, "m") >= 0) {
                sMarks = CLAMP(Exp_I(t, "m", 0), 0, MARKS_MAX);
            }
            if (Exp_Has(t, "d") >= 0) {
                sMarks = CLAMP(sMarks + Exp_I(t, "d", 0), 0, MARKS_MAX);
            }
            return "Moon Marks changed";
        case XOP_DIR:
            sDirMult = CLAMP(Exp_F(t, "b", 1.0f), 0.0f, 10.0f);
            return "Director changed";
        case XOP_BANNER:
            V2_Banner(sLinkFrom, t);
            return "Banner shown";
        case XOP_SFX:
            sXSfxId = CLAMP(Exp_I(t, "id", NA_SE_SY_ERROR), 0, 0xFFFF);
            sXSfxN = CLAMP(Exp_I(t, "n", 1), 1, 100);
            sXSfxIv = CLAMP(Exp_I(t, "iv", 10), 1, 600);
            sXSfxN = MIN(sXSfxN, MAX(300 * FPS / sXSfxIv, 1)); // (5 minutes at most)
            sXSfxT = 0;
            return "Sound";
        case XOP_PANIC:
            Exp_Panic(play);
            return "Undid everything";
        default:
            return NULL;
    }
}

// --- hitboxes grow with the size a friend gave ------------------------------------
// Right before a collider is registered for this frame's hit checks, it's sized to
// match: cylinders from their original size (kept here, so nothing compounds),
// joint spheres from their model radius (which the game recomputes from anyway).
// Link's own cylinder follows his Experimental size too.

typedef struct {
    Collider* col;
    Actor* actor;
    s16 radius;
    s16 height;
    s16 yShift;
} ExpCyl;

#define EXP_CYLS 64
static ExpCyl sXCyl[EXP_CYLS];

static s16 Exp_S16(f32 v) {
    return (s16)CLAMP(v, -32000.0f, 32000.0f);
}

static void Exp_GrowCollider(PlayState* play, Collider* col) {
    Actor* actor;
    f32 sxz;
    f32 sy;
    s32 i;

    if ((col == NULL) || (col->actor == NULL) || (play == NULL)) {
        return;
    }
    actor = col->actor;
    if (actor == &GET_PLAYER(play)->actor) {
        sxz = (gExp.scale[0] + gExp.scale[2]) * 0.5f;
        sy = gExp.scale[1];
    } else {
        sxz = sy = Exp_DrawScale(actor);
    }
    if (col->shape == COLSHAPE_CYLINDER) {
        ColliderCylinder* cyl = (ColliderCylinder*)col;
        ExpCyl* e = NULL;
        s32 free = -1;

        for (i = 0; i < EXP_CYLS; i++) {
            if ((sXCyl[i].col == col) && (sXCyl[i].actor == actor)) {
                e = &sXCyl[i];
                break;
            }
            if ((sXCyl[i].col == NULL) && (free < 0)) {
                free = i;
            }
        }
        if ((sxz == 1.0f) && (sy == 1.0f)) {
            // Back to normal: put the original size back (only for something we still
            // know is the same actor) and forget it.
            if ((e != NULL) && ((actor == &GET_PLAYER(play)->actor) || Exp_IsTracked(actor))) {
                cyl->dim.radius = e->radius;
                cyl->dim.height = e->height;
                cyl->dim.yShift = e->yShift;
            }
            if (e != NULL) {
                e->col = NULL;
            }
            return;
        }
        if (e == NULL) {
            if (free < 0) {
                return;
            }
            e = &sXCyl[free];
            e->col = col;
            e->actor = actor;
            e->radius = cyl->dim.radius;
            e->height = cyl->dim.height;
            e->yShift = cyl->dim.yShift;
        }
        cyl->dim.radius = Exp_S16(e->radius * sxz);
        cyl->dim.height = Exp_S16(e->height * sy);
        cyl->dim.yShift = Exp_S16(e->yShift * sy);
    } else if ((col->shape == COLSHAPE_JNTSPH) && ((sxz != 1.0f) || (sy != 1.0f))) {
        ColliderJntSph* jnt = (ColliderJntSph*)col;
        f32 s = MAX(sxz, sy);

        for (i = 0; i < jnt->count; i++) {
            ColliderJntSphElementDim* d = &jnt->elements[i].dim;

            d->worldSphere.radius = Exp_S16(d->modelSphere.radius * d->scale * s);
        }
    }
}

// A tracked actor that's gone: forget its cylinder (the memory may be reused).
static void Exp_ForgetColliders(Actor* actor) {
    s32 i;

    for (i = 0; i < EXP_CYLS; i++) {
        if ((sXCyl[i].col != NULL) && (sXCyl[i].actor == actor)) {
            sXCyl[i].col = NULL;
        }
    }
}

RECOMP_HOOK("CollisionCheck_SetAT") void Exp_BeforeSetAT(PlayState* play, CollisionCheckContext* ctx, Collider* col) {
    Exp_GrowCollider(play, col);
}

RECOMP_HOOK("CollisionCheck_SetAC") void Exp_BeforeSetAC(PlayState* play, CollisionCheckContext* ctx, Collider* col) {
    Exp_GrowCollider(play, col);
}

RECOMP_HOOK("CollisionCheck_SetOC") void Exp_BeforeSetOC(PlayState* play, CollisionCheckContext* ctx, Collider* col) {
    Exp_GrowCollider(play, col);
}

// Undo: every grown cylinder whose actor is still around gets its own size back.
static void Exp_RestoreColliders(PlayState* play) {
    s32 i;

    for (i = 0; i < EXP_CYLS; i++) {
        ExpCyl* e = &sXCyl[i];

        if ((e->col != NULL) && (play != NULL) &&
            ((e->actor == &GET_PLAYER(play)->actor) || V2_IsLive(play, e->actor)) && (e->col->actor == e->actor)) {
            ColliderCylinder* cyl = (ColliderCylinder*)e->col;

            cyl->dim.radius = e->radius;
            cyl->dim.height = e->height;
            cyl->dim.yShift = e->yShift;
        }
        e->col = NULL;
    }
}
