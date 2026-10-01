// ---------------------------------------------------------------------------
// Moonlit Prey (2.1): the moon marks one enemy in the area (spawning one if
// there's nothing suitable). It glows, won't fight back and runs from you.
// Kill it in time for a pile of rupees; let it live and it costs half your
// max health. Included after ev_v2.inc.
// ---------------------------------------------------------------------------

// Melee enemies only: their attacks are colliders we can switch off.
// Wolfos, White Wolfos, Stalchild, Chuchu, Tektite, Dodongo, Snapper, Dinolfos
static const u8 sPreyPool[] = { 3, 4, 5, 8, 9, 13, 24, 32 };
static Actor* sPrey = NULL;
static s32 sPreyValid = false; // a target was marked at some point during this event
static s32 sPreyTimer = 0;
static s32 sPreyTries = 0;
static s32 sPreyLastHp = 0;
static LightNode* sPreyLight = NULL;
static LightInfo sPreyLightInfo;

static s32 Prey_OkType(Actor* actor) {
    s32 pool = V2_PoolOfActor(actor);
    s32 k;

    for (k = 0; k < (s32)ARRAY_COUNT(sPreyPool); k++) {
        if (sPreyPool[k] == pool) {
            return true;
        }
    }
    return false;
}

static s32 Prey_TimeFrames(void) {
    static const u8 sSeconds[INTENSITY_MAX] = { 75, 60, 45 };

    return (s32)(sSeconds[Intensity()] * FPS * Ev_DurationMult());
}

static void Prey_RemoveLight(PlayState* play) {
    if (sPreyLight != NULL) {
        LightContext_RemoveLight(play, &play->lightCtx, sPreyLight);
        sPreyLight = NULL;
    }
}

// Still in the enemy list (dead or alive)? Only enemies count, so a new actor
// that happens to reuse a freed prey's memory is never mistaken for it.
static s32 Prey_Present(PlayState* play, Actor* target) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while (actor != NULL) {
        if (actor == target) {
            return actor->update != NULL;
        }
        actor = actor->next;
    }
    return false;
}

// Is there a marked prey right now?
static s32 Prey_HasTarget(PlayState* play) {
    return (sActive == EV_PREY) && (sPrey != NULL) && Prey_Present(play, sPrey) && (sPrey->colChkInfo.health > 0);
}

static Actor* Prey_FindNative(PlayState* play) {
    Actor* list[12];
    s32 count = 0;
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;

    while ((actor != NULL) && (count < 12)) {
        if ((actor->update != NULL) && (actor->colChkInfo.health > 0) && (Tag_Of(actor) == TAG_NONE) &&
            Prey_OkType(actor) && (actor->xzDistToPlayer < 1500.0f) && (fabsf(actor->playerHeightRel) < 250.0f)) {
            list[count++] = actor;
        }
        actor = actor->next;
    }
    return (count > 0) ? list[V2_Rand(count)] : NULL;
}

static void Prey_OnStart(PlayState* play) {
    if (!sInComboPass) {
        sTotalFrames = sFramesLeft = Prey_TimeFrames();
    }
    sPrey = NULL;
    sPreyValid = false;
    sPreyTimer = FPS;
    sPreyTries = 0;
}

static void Prey_SceneSetup(PlayState* play) {
    sPrey = NULL;
    sPreyTimer = FPS;
    sPreyTries = 0;
}

static void Prey_Slain(PlayState* play) {
    static const u8 sRupees[INTENSITY_MAX] = { 50, 100, 200 };
    static char sMsg[40];
    s32 amount = sRupees[Intensity()];
    s32 n;

    sPrey = NULL;
    sPreyValid = false;
    Rupees_ChangeBy(amount);
    Contract_Add(CT_PREY, 1);
    if (amount > 0) {
        n = Ev_Append(sMsg, 0, "Prey slain! +", sizeof(sMsg));
        n = Ev_AppendNum(sMsg, n, amount, sizeof(sMsg));
        Ev_Append(sMsg, n, " rupees", sizeof(sMsg));
    } else {
        Ev_Append(sMsg, 0, "Prey slain!", sizeof(sMsg));
    }
    Menu_ShowToast(sMsg);
    Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
    Ev_End(play);
}

static void Tick_Prey(PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (player == NULL) {
        return;
    }
    if (sPrey != NULL) {
        if (!Prey_Present(play, sPrey)) {
            if (sPreyLastHp == 0) {
                Prey_Slain(play);
                return;
            }
            // Gone some other way: the moon marks another.
            sPrey = NULL;
            sPreyTimer = FPS;
            Prey_RemoveLight(play);
        } else if (sPrey->colChkInfo.health == 0) {
            Prey_Slain(play);
            return;
        } else {
            Actor* prey = sPrey;

            sPreyLastHp = prey->colChkInfo.health;
            // Moonlight: a pale glow, a light, and sparkles falling on it.
            if (prey->colorFilterTimer == 0) {
                Actor_SetColorFilter(prey, COLORFILTER_COLORFLAG_BLUE, 160, COLORFILTER_BUFFLAG_OPA, 30);
            }
            if (sPreyLight == NULL) {
                sPreyLight = LightContext_InsertLight(play, &play->lightCtx, &sPreyLightInfo);
            }
            Lights_PointGlowSetInfo(&sPreyLightInfo, prey->world.pos.x, prey->world.pos.y + 40.0f, prey->world.pos.z,
                                    170, 200, 255, 300);
            if ((sEventFrames % 3) == 0) {
                static Color_RGBA8 sPrim = { 230, 240, 255, 255 };
                static Color_RGBA8 sEnv = { 110, 150, 255, 0 };
                Vec3f pos;
                Vec3f vel = { 0.0f, -3.0f, 0.0f };
                Vec3f accel = { 0.0f, -0.05f, 0.0f };

                pos.x = prey->world.pos.x + Rand_CenteredFloat(50.0f);
                pos.y = prey->world.pos.y + 120.0f + Rand_ZeroOne() * 100.0f;
                pos.z = prey->world.pos.z + Rand_CenteredFloat(50.0f);
                EffectSsKirakira_SpawnSmall(play, &pos, &vel, &accel, &sPrim, &sEnv);
            }
        }
    }
    if (sPrey == NULL) {
        Actor* actor;

        if (sPreyTimer > 0) {
            sPreyTimer--;
            return;
        }
        sPreyTimer = FPS / 2;
        actor = Prey_FindNative(play);
        if (actor == NULL) {
            // Prefer enemies you have on in the enemy pool (any of them if none are).
            s32 picks[ARRAY_COUNT(sPreyPool)];
            s32 np = 0;
            s32 k;

            for (k = 0; k < (s32)ARRAY_COUNT(sPreyPool); k++) {
                if (gOpt[ID_POOL_FIRST + sPreyPool[k]]) {
                    picks[np++] = sPreyPool[k];
                }
            }
            actor = V2_SpawnEnemy(play, (np > 0) ? picks[V2_Rand(np)] : sPreyPool[V2_Rand(ARRAY_COUNT(sPreyPool))],
                                  NULL, 400.0f, 800.0f, TAG_EVENT);
        }
        if (actor != NULL) {
            sPrey = actor;
            sPreyLastHp = actor->colChkInfo.health;
            sPreyValid = true;
            Audio_PlaySfx(NA_SE_EV_CHINCLE_SPELL_EFFECT);
        } else if ((++sPreyTries > 20) && !sInComboPass) {
            // Nothing can be marked here (small room, cleared area): no penalty.
            Menu_ShowToast(sPreyValid ? "The moon lost its prey." : "The moon found nothing to hunt.");
            Ev_Abort(play);
        }
    }
}

// Can the prey step to `next` without hitting a wall or running off a ledge?
static s32 Prey_CanStep(PlayState* play, Actor* actor, s16 yaw, f32 speed, Vec3f* next) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f a = actor->world.pos;
    Vec3f b;
    Vec3f hit;
    Vec3f probe;
    f32 floorY;

    next->x = actor->world.pos.x + Math_SinS(yaw) * speed;
    next->y = actor->world.pos.y;
    next->z = actor->world.pos.z + Math_CosS(yaw) * speed;
    // Look a little further ahead so it doesn't hug walls.
    a.y += 25.0f;
    b = a;
    b.x += Math_SinS(yaw) * (speed + 30.0f);
    b.z += Math_CosS(yaw) * (speed + 30.0f);
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
        return false;
    }
    probe = *next;
    probe.y += 60.0f;
    floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if ((floorY <= BGCHECK_Y_MIN) || (actor->world.pos.y - floorY > 120.0f)) {
        return false; // a drop or nothing to stand on
    }
    return true;
}

// After the prey's own update: it doesn't attack, and it runs from you.
static void Prey_AfterActorUpdate(PlayState* play, Actor* actor) {
    static const f32 sFlee[INTENSITY_MAX] = { 0.65f, 0.8f, 0.9f };
    static const s16 sTurns[] = { 0, 0x1800, -0x1800, 0x3000, -0x3000, 0x4800, -0x4800, 0x6000, -0x6000 };
    CollisionCheckContext* cc = &play->colChkCtx;
    Player* player = GET_PLAYER(play);
    s32 i;

    if ((actor != sPrey) || (sActive != EV_PREY) || (actor->colChkInfo.health == 0) || (player == NULL) ||
        (actor->category != ACTORCAT_ENEMY)) {
        return;
    }
    // Passive: whatever it swung this frame hits nothing.
    for (i = 0; i < cc->colATCount; i++) {
        if ((cc->colAT[i] != NULL) && (cc->colAT[i]->actor == actor)) {
            cc->colAT[i] = NULL;
        }
    }
    // Its own steps (usually toward you) are undone; we move it instead.
    actor->world.pos.x = actor->prevPos.x;
    actor->world.pos.z = actor->prevPos.z;
    if (actor->xzDistToPlayer < 400.0f) {
        f32 speed = CLAMP(player->actor.speed * sFlee[Intensity()], 3.0f, 9.0f);
        s16 away = Math_Vec3f_Yaw(&player->actor.world.pos, &actor->world.pos);
        Vec3f next;

        for (i = 0; i < (s32)ARRAY_COUNT(sTurns); i++) {
            s16 yaw = away + sTurns[i];

            if (Prey_CanStep(play, actor, yaw, speed, &next)) {
                actor->world.pos.x = next.x;
                actor->world.pos.z = next.z;
                actor->shape.rot.y = actor->world.rot.y = yaw;
                break;
            }
        }
        // Cornered: it stays put.
    }
}

static void Prey_OnTimeout(PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 loss = gSaveContext.save.saveInfo.playerData.healthCapacity / 2;
    s32 hp = gSaveContext.save.saveInfo.playerData.health;

    if (!Prey_HasTarget(play)) {
        return; // nothing was marked when time ran out: no penalty
    }
    sPreyValid = false;
    sEventFailed = true; // not a survival
    gSaveContext.save.saveInfo.playerData.health = MAX(hp - loss, 0);
    Audio_PlaySfx(NA_SE_SY_ERROR);
    if (player != NULL) {
        Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 200, COLORFILTER_BUFFLAG_OPA, 12);
    }
    Menu_ShowToast("The prey escaped! It cost you dearly.");
}

static void Prey_OnEnd(PlayState* play) {
    Prey_RemoveLight(play);
    sPrey = NULL;
    sPreyValid = false;
}

static void Prey_ResetForPlayInit(void) {
    sPreyLight = NULL; // the new scene has a fresh light list
    sPrey = NULL;
}

static void Prey_OnRoomChange(PlayState* play) {
    sPrey = NULL;
    sPreyTries = 0;
    Prey_RemoveLight(play);
}

static void Prey_DrawWorld(PlayState* play) {
    if ((sPrey != NULL) && (sActive == EV_PREY) && Prey_Present(play, sPrey) && (sPrey->colChkInfo.health > 0)) {
        u8 a = 140 + (u8)(Math_SinS((s16)(play->gameplayFrames * 0x900)) * 60.0f);

        Ev_DrawGroundCircle(play, &sPrey->world.pos, NULL, 0.8f, 180, 210, 255, a);
    }
}

static s32 Prey_SubLine(PlayState* play, char* line, s32 size, u8* r, u8* g, u8* b) {
    if (sActive != EV_PREY) {
        return false;
    }
    if ((sPrey != NULL) && (V2_PoolOfActor(sPrey) >= 0)) {
        s32 n = Ev_Append(line, 0, "Slay the moonlit ", size);

        n = Ev_Append(line, n, Pool_Name(V2_PoolOfActor(sPrey)), size);
        Ev_Append(line, n, "!", size);
    } else {
        Ev_Append(line, 0, "The moon seeks its prey...", size);
    }
    *r = 170;
    *g = 210;
    *b = 255;
    return true;
}
