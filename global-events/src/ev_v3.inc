// ===========================================================================
// V3: events brought over from Moonfall Mayhem (ported from its source), the
// Pop Quiz's text questions, and the shared V3 world drawing.
// The ported event code is kept close to the original, running on a small
// compatibility layer below.
// ===========================================================================

#define SEC(x) ((s32)((x) * FPS))
#define MF_CLAMP(v, lo, hi) (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
#define MF_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MF_MAX(a, b) (((a) > (b)) ? (a) : (b))

typedef struct {
    u8 r, g, b, a;
} MfColor;

typedef struct {
    Vec3f pos;
    Vec3f vel;
    s32 t;
    s16 state;
    s16 yaw;
    f32 r;
} Hz;

#define HZ_MAX 16
static Hz sAHz[HZ_MAX];
static s32 sAHzN = 0;
static u8 sASeq[10];
static s32 sASeqLen;
static s32 sASeqPos;

typedef struct {
    s32 id;
    s32 timer;
    s32 duration;
    u8 failed;
    u8 succeeded;
    s32 hits;
    s32 score;
    f32 fa, fb;
    s16 notesAtStart;
} V3Ev;
static V3Ev gEv;
static s32 sV3Notes = 0;
static s32 sV3NoteLast = -1;
static const char* sV3Prompt = NULL;
static Gfx** sV3Gfx = NULL;
static PlayState* s3DPlay = NULL;
static Input* sV3Input = NULL;
static const u16 sReflexBtns[4] = { BTN_A, BTN_B, BTN_R, BTN_Z };
static const char* sReflexNames[4] = { "A", "B", "R", "Z" };

static const MfColor cWarn = { 255, 60, 40, 140 };
static const MfColor cGood = { 90, 255, 150, 170 };
static const MfColor cMoon = { 190, 120, 255, 170 };
static const MfColor cWhite = { 255, 255, 255, 255 };
static const MfColor cText = { 230, 230, 255, 255 };

typedef struct {
    const char* name;
    const char* counter; // how to beat it, one line
    u8 tier;             // 0 Mild .. 3 Brutal
    u8 p[3];             // tuning (fixed in V3)
    u8 longEvent;        // runs until done, up to 60 s
} V3Def;

// Order matches the GlobalEventId entries from EV_STARFALL on.
static const V3Def sV3Defs[] = {
    { "Starfall Bounty", "Stand where the stars land!", 0, { 6, 30, 2 }, 0 },
    { "Dowsing Moon", "Follow the warmth, press A to dig.", 0, { 70, 8, 0 }, 1 },
    { "Giant's Gale", "Hold the stick INTO the wind.", 0, { 9, 30, 0 }, 0 },
    { "Sinking Sands", "Keep moving!", 0, { 10, 1, 0 }, 0 },
    { "Frostbite", "Roll to shake off the frost.", 1, { 3, 35, 6 }, 0 },
    { "Scythe Sweep", "Jump it or stand in the green gap.", 2, { 5, 11, 4 }, 0 },
    { "Moth Swarm", "Spin attack to scatter the moths!", 1, { 4, 6, 0 }, 0 },
    { "Hex of Silence", "Don't press B or the C buttons.", 0, { 2, 0, 0 }, 0 },
    { "Moon Tether", "Stay inside the drifting circle!", 1, { 16, 15, 3 }, 0 },
    { "Moon's Grasp", "When it grabs you, MASH A!", 1, { 12, 6, 0 }, 0 },
    { "Heartbeat Hex", "Press A on every heartbeat.", 1, { 14, 4, 2 }, 0 },
    { "Moon's Reflex Test", "Press the button that flashes up!", 0, { 12, 3, 0 }, 0 },
    { "Gorgon's Gaze", "Keep your camera AWAY from the eye.", 1, { 7, 4, 6 }, 0 },
    { "Dissonance", "Play any 3 ocarina notes.", 0, { 3, 1, 0 }, 0 },
    { "Mirror Madness", "Face the shard, hold Z to break it.", 1, { 15, 5, 1 }, 0 },
    { "Volley of Stars", "Face the star and hold R to guard!", 1, { 15, 4, 4 }, 0 },
    { "Sleeping Giant", "Tiptoe! Running wakes the giant.", 1, { 35, 6, 6 }, 0 },
    { "Mask Salesman's Game", "Watch the arrows, repeat on D-pad.", 1, { 8, 3, 4 }, 1 },
    { "Pressure Seals", "Step on the seals in order.", 1, { 4, 3, 8 }, 1 },
};
_Static_assert(ARRAY_COUNT(sV3Defs) == EV_COUNT - EV_STARFALL, "V3 event table");

static s32 V3_IsEvent(s32 ev) {
    return (ev >= EV_STARFALL) && (ev < EV_COUNT);
}

static const V3Def* V3_Def(s32 ev) {
    return V3_IsEvent(ev) ? &sV3Defs[ev - EV_STARFALL] : NULL;
}

// Tuning value; damage values (index 1 or 2 depending on the event) get the
// Fierce Moon / Empower bump through V3_Dmg.
static s32 V3_Param(s32 i) {
    const V3Def* d = V3_Def(gEv.id);

    return (d != NULL) ? d->p[i] : 0;
}
#define P(i) V3_Param(i)

static Player* Pl(PlayState* play) {
    return GET_PLAYER(play);
}

static s32 Pressed(u16 btn) {
    return (sV3Input != NULL) && ((sV3Input->press.button & btn) != 0);
}

static s32 Held(u16 btn) {
    return (sV3Input != NULL) && ((sV3Input->cur.button & btn) != 0);
}

static s32 Every(s32 frames) {
    return (frames > 0) && ((gEv.timer % frames) == 0);
}

static s32 Rng_Range(s32 lo, s32 hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + ((s32)(Rand_ZeroOne() * (hi - lo + 1)) % (hi - lo + 1));
}

static s32 Str_Len(const char* s) {
    return Ui_StrLen(s);
}

static void Str_Cat(char* dst, const char* src, s32 max) {
    Ev_Append(dst, Ui_StrLen(dst), src, max);
}

static void Str_CatInt(char* dst, s32 value, s32 max) {
    Ev_AppendNum(dst, Ui_StrLen(dst), value, max);
}

static s32 V3_PlayerDead(void) {
    return (gSaveContext.save.saveInfo.playerData.health <= 0);
}

static f32 Mf_DistXZ(Vec3f* a, Vec3f* b) {
    return Ev_DistXZ(a, b);
}

static s16 Mf_YawTo(Vec3f* from, Vec3f* to) {
    return Math_Vec3f_Yaw(from, to);
}

static f32 Mf_FloorY(Vec3f* pos, f32 fallback) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f probe = *pos;
    f32 y;

    if (gPlayState_v2 == NULL) {
        return fallback;
    }
    probe.y += 80.0f;
    y = BgCheck_EntityRaycastFloor3(&gPlayState_v2->colCtx, &poly, &bgId, &probe);
    return (y > BGCHECK_Y_MIN) ? y : fallback;
}

static s32 Mf_PlayerGrounded(void) {
    Player* player = (gPlayState_v2 != NULL) ? GET_PLAYER(gPlayState_v2) : NULL;

    return (player != NULL) && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
}

static f32 Mf_StickMag(void) {
    f32 x = (sV3Input != NULL) ? sV3Input->rel.stick_x : 0;
    f32 y = (sV3Input != NULL) ? sV3Input->rel.stick_y : 0;

    return sqrtf(x * x + y * y);
}

static s16 Mf_StickWorldYaw(PlayState* play) {
    s16 sx = (sV3Input != NULL) ? sV3Input->rel.stick_x : 0;
    s16 sy = (sV3Input != NULL) ? sV3Input->rel.stick_y : 0;

    return Math_Atan2S_XY(sy, -sx) + Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
}

static void Mf_Sfx(u16 sfxId) {
    Audio_PlaySfx(sfxId);
}

// Damage in quarter hearts. Fierce Moon / Empower: +50%.
static void Mf_DamagePlayer(s32 quarterHearts, s32 knockback, s16 fromYaw) {
    PlayState* play = gPlayState_v2;
    Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
    s32 dmg;

    if ((player == NULL) || V3_PlayerDead()) {
        return;
    }
    if (Intensity() >= INTENSITY_BRUTAL) {
        quarterHearts = quarterHearts * 3 / 2;
    }
    dmg = quarterHearts * 4;
    if (dmg <= 0) {
        return;
    }
    if (knockback) {
        func_800B8D10(play, NULL, 7.0f, fromYaw, 6.0f, 2, (u32)MF_MIN(dmg, 255));
    } else {
        Health_ChangeBy(play, -dmg);
        Actor_SetColorFilter(&player->actor, COLORFILTER_COLORFLAG_RED, 180, COLORFILTER_BUFFLAG_OPA, 8);
        Audio_PlaySfx(NA_SE_VO_LI_DAMAGE_S);
    }
}

static void Mf_HealPlayer(s32 quarterHearts) {
    if ((gPlayState_v2 != NULL) && !V3_PlayerDead()) {
        Health_ChangeBy(gPlayState_v2, quarterHearts * 4);
    }
}

static s32 V3_FindSpot(PlayState* play, Vec3f* out, f32 minDist, f32 maxDist, s16 preferYaw) {
    if (Ev_FindSpot(play, minDist, maxDist, preferYaw, 0x7FFF, false, out)) {
        return true;
    }
    if (Ev_FindSpot(play, minDist * 0.5f, maxDist, preferYaw, 0x7FFF, false, out)) {
        return true;
    }
    *out = GET_PLAYER(play)->actor.world.pos;
    out->x += Math_SinS(preferYaw) * minDist;
    out->z += Math_CosS(preferYaw) * minDist;
    out->y = Mf_FloorY(out, GET_PLAYER(play)->actor.world.pos.y);
    return false;
}

static void V3_GiveMarks(s32 amount, const char* why) {
    static char sWhy[32];
    s32 n;

    if (amount <= 0) {
        return;
    }
    n = Ev_Append(sWhy, 0, why, sizeof(sWhy));
    Ev_Append(sWhy, n, ": +", sizeof(sWhy));
    V2_GiveMarks(amount, sWhy);
}

static void V3_Notify(const char* text, MfColor c) {
    Menu_ShowToast(text);
}

static void V3_SetPrompt(const char* text) {
    sV3Prompt = text;
}

static void Quake(PlayState* play, s32 strength, s32 duration) {
    s16 q = Quake_Request(GET_ACTIVE_CAM(play), QUAKE_TYPE_3);

    Quake_SetSpeed(q, 20000);
    Quake_SetPerturbations(q, strength, 0, 0, 0);
    Quake_SetDuration(q, duration);
}

static void Boom(PlayState* play, Vec3f* pos) {
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    EffectSsBomb2_SpawnLayered(play, pos, &zero, &zero, 100, 19);
    Mf_Sfx(NA_SE_IT_BOMB_EXPLOSION);
    Quake(play, 4, 8);
}

static void Sparkle(PlayState* play, Vec3f* pos, u8 r, u8 g, u8 b) {
    Vec3f vel = { 0.0f, 2.0f, 0.0f };
    Vec3f acc = { 0.0f, -0.1f, 0.0f };
    Color_RGBA8 prim = { 255, 255, 255, 255 };
    Color_RGBA8 env = { r, g, b, 255 };

    vel.x = Rng_Range(-20, 20) / 10.0f;
    vel.z = Rng_Range(-20, 20) / 10.0f;
    EffectSsKirakira_SpawnDispersed(play, pos, &vel, &acc, &prim, &env, 2500, 30);
}

// A moon strike: a hit flash and sparks (the game's lightning effect crashes).
static void Lightning(PlayState* play, Vec3f* pos) {
    Vec3f p = *pos;

    p.y += 30.0f;
    EffectSsHitmark_SpawnFixedScale(play, 0, &p);
    Sparkle(play, &p, 200, 160, 255);
    Mf_Sfx(NA_SE_EV_LIGHTNING);
}

static void InitHz(void) {
    s32 i;

    sAHzN = 0;
    for (i = 0; i < HZ_MAX; i++) {
        sAHz[i].t = 0;
        sAHz[i].state = 0;
    }
}

// --- 2D, into the HUD display list ------------------------------------------

static void Draw2D_Rect(s32 x, s32 y, s32 w, s32 h, MfColor c) {
    if ((sV3Gfx == NULL) || (w <= 0) || (h <= 0)) {
        return;
    }
    x = MF_CLAMP(x, 0, SCREEN_WIDTH);
    y = MF_CLAMP(y, 0, SCREEN_HEIGHT);
    *sV3Gfx = Ui_DrawRect(*sV3Gfx, x, y, MF_MIN(x + w, SCREEN_WIDTH), MF_MIN(y + h, SCREEN_HEIGHT), c.r, c.g, c.b, c.a);
}

static void Draw2D_Text(s32 x, s32 y, MfColor c, const char* text) {
    GfxPrint printer;
    Gfx* gfx;

    if ((sV3Gfx == NULL) || (text == NULL) || (text[0] == '\0')) {
        return;
    }
    gfx = *sV3Gfx;
    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2);
    GfxPrint_SetColor(&printer, c.r, c.g, c.b, c.a);
    Ui_Print(&printer, x, y, text);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    *sV3Gfx = gfx;
}

static void Draw2D_TextCentered(s32 y, MfColor c, const char* text) {
    Draw2D_Text((SCREEN_WIDTH - Str_Len(text) * 8) / 2, y, c, text);
}

static MfColor Mf_Rainbow(s32 t, u8 alpha) {
    MfColor c;
    s32 h = (t % 360 + 360) % 360;
    s32 x = (h % 60) * 255 / 60;

    switch (h / 60) {
        case 0: c.r = 255; c.g = x; c.b = 0; break;
        case 1: c.r = 255 - x; c.g = 255; c.b = 0; break;
        case 2: c.r = 0; c.g = 255; c.b = x; break;
        case 3: c.r = 0; c.g = 255 - x; c.b = 255; break;
        case 4: c.r = x; c.g = 0; c.b = 255; break;
        default: c.r = 255; c.g = 0; c.b = 255 - x; break;
    }
    c.a = alpha;
    return c;
}

static void Bar2D(s32 y, s32 value, s32 max, MfColor c, const char* label) {
    MfColor bg = { 0, 0, 0, 150 };
    s32 w = 120 * MF_CLAMP(value, 0, max) / MF_MAX(1, max);

    Draw2D_Rect(100, y, 120, 8, bg);
    Draw2D_Rect(100, y, w, 8, c);
    Draw2D_Text(100 - (Str_Len(label) + 1) * 8, y, cText, label);
}

static void Draw2D_WorldMarker(PlayState* play, Vec3f* pos, MfColor c, const char* label) {
    Camera* cam = GET_ACTIVE_CAM(play);
    MfColor bg = { 0, 0, 0, 150 };
    Vec3f proj;
    f32 invW;
    s16 rel;

    Actor_GetProjectedPos(play, pos, &proj, &invW);
    if (proj.z > 1.0f) {
        s32 x = (s32)(SCREEN_WIDTH / 2 + proj.x * invW * (SCREEN_WIDTH / 2));
        s32 y = (s32)(SCREEN_HEIGHT / 2 - proj.y * invW * (SCREEN_HEIGHT / 2));

        if ((x >= 12) && (x <= SCREEN_WIDTH - 12)) {
            s32 w = Str_Len(label) * 8 + 6;

            y = MF_CLAMP(y, 40, SCREEN_HEIGHT - 30);
            Draw2D_Rect(x - w / 2, y - 6, w, 12, bg);
            Draw2D_Text(x - w / 2 + 3, y - 4, c, label);
            return;
        }
    }
    rel = Math_Vec3f_Yaw(&cam->eye, pos) - Camera_GetCamDirYaw(cam);
    if (ABS(rel) > 0x6000) {
        Draw2D_Rect(SCREEN_WIDTH / 2 - 40, 196, 80, 12, bg);
        Draw2D_TextCentered(198, c, "v behind v");
    } else if (rel > 0) {
        Draw2D_Rect(4, 112, 22, 12, bg);
        Draw2D_Text(7, 114, c, "<<");
    } else {
        Draw2D_Rect(SCREEN_WIDTH - 26, 112, 22, 12, bg);
        Draw2D_Text(SCREEN_WIDTH - 23, 114, c, ">>");
    }
}

// --- 3D, into the translucent world display list ----------------------------

#define CIRCLE_SEGS 24
#define RING_SEGS 15

static void Draw3D_Begin(PlayState* play) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;

    s3DPlay = play;
    OPEN_DISPS(gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCycleType(POLY_XLU_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_XLU_DISP++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_SHADE, G_CC_SHADE);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    CLOSE_DISPS(gfxCtx);
}

static void Draw3D_End(void) {
    GraphicsContext* gfxCtx;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING | G_FOG);
    CLOSE_DISPS(gfxCtx);
    s3DPlay = NULL;
}

static void V3_SetVtx(Vtx* v, f32 x, f32 y, f32 z, MfColor c) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
    v->v.cn[0] = c.r;
    v->v.cn[1] = c.g;
    v->v.cn[2] = c.b;
    v->v.cn[3] = c.a;
}

static void V3_LoadMatrixAt(Vec3f* pos, s32 billboard) {
    GraphicsContext* gfxCtx = s3DPlay->state.gfxCtx;

    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    if (billboard) {
        Matrix_ReplaceRotation(&s3DPlay->billboardMtxF);
    }
    OPEN_DISPS(gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    CLOSE_DISPS(gfxCtx);
}

static void V3_DrawFan(Vec3f* pos, f32 radius, MfColor center, MfColor rim, s32 billboard, s16 startAngle,
                       s32 sweep) {
    GraphicsContext* gfxCtx;
    Vtx* vtx;
    s32 i;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    vtx = GRAPH_ALLOC(gfxCtx, sizeof(Vtx) * (CIRCLE_SEGS + 2));
    V3_SetVtx(&vtx[0], 0, 0, 0, center);
    for (i = 0; i <= CIRCLE_SEGS; i++) {
        s16 a = startAngle + (s16)(sweep * i / CIRCLE_SEGS);
        f32 sn = Math_SinS(a) * radius;
        f32 cs = Math_CosS(a) * radius;

        if (billboard) {
            V3_SetVtx(&vtx[i + 1], sn, cs, 0, rim);
        } else {
            V3_SetVtx(&vtx[i + 1], sn, 0, cs, rim);
        }
    }
    V3_LoadMatrixAt(pos, billboard);
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, CIRCLE_SEGS + 2, 0);
    for (i = 1; i <= CIRCLE_SEGS; i++) {
        gSP1Triangle(POLY_XLU_DISP++, 0, i, i + 1, 0);
    }
    CLOSE_DISPS(gfxCtx);
}

static void Draw3D_Disk(Vec3f* pos, f32 radius, MfColor c) {
    Vec3f p = *pos;
    MfColor rim = c;

    p.y += 2.0f;
    rim.a = (u8)(c.a / 3);
    V3_DrawFan(&p, radius, c, rim, false, 0, 0x10000);
}

static void Draw3D_Wedge(Vec3f* pos, f32 radius, s16 yaw, s16 halfAngle, MfColor c) {
    Vec3f p = *pos;

    p.y += 3.0f;
    V3_DrawFan(&p, radius, c, c, false, yaw - halfAngle, (s32)halfAngle * 2);
}

static void Draw3D_Orb(Vec3f* pos, f32 radius, MfColor c) {
    MfColor rim = c;

    rim.a = 0;
    V3_DrawFan(pos, radius, c, rim, true, 0, 0x10000);
}

static void Draw3D_Ring(Vec3f* pos, f32 radius, f32 thickness, MfColor c) {
    GraphicsContext* gfxCtx;
    Vtx* vtx;
    Vec3f p = *pos;
    f32 inner = MF_MAX(radius - thickness, 0.0f);
    MfColor ci = c;
    s32 i;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    ci.a = c.a / 4;
    vtx = GRAPH_ALLOC(gfxCtx, sizeof(Vtx) * (RING_SEGS + 1) * 2);
    for (i = 0; i <= RING_SEGS; i++) {
        s16 a = (s16)(0x10000 * i / RING_SEGS);

        V3_SetVtx(&vtx[i * 2], Math_SinS(a) * radius, 0, Math_CosS(a) * radius, c);
        V3_SetVtx(&vtx[i * 2 + 1], Math_SinS(a) * inner, 0, Math_CosS(a) * inner, ci);
    }
    p.y += 3.0f;
    V3_LoadMatrixAt(&p, false);
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, (RING_SEGS + 1) * 2, 0);
    for (i = 0; i < RING_SEGS; i++) {
        s32 o = i * 2;

        gSP2Triangles(POLY_XLU_DISP++, o, o + 2, o + 1, 0, o + 1, o + 2, o + 3, 0);
    }
    CLOSE_DISPS(gfxCtx);
}

static void Draw3D_Pillar(Vec3f* pos, f32 radius, f32 height, MfColor c) {
    GraphicsContext* gfxCtx;
    Vtx* vtx;
    MfColor top = c;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    top.a = 0;
    vtx = GRAPH_ALLOC(gfxCtx, sizeof(Vtx) * 4);
    V3_SetVtx(&vtx[0], -radius, 0, 0, c);
    V3_SetVtx(&vtx[1], radius, 0, 0, c);
    V3_SetVtx(&vtx[2], radius, height, 0, top);
    V3_SetVtx(&vtx[3], -radius, height, 0, top);
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_RotateYS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(s3DPlay)), MTXMODE_APPLY);
    OPEN_DISPS(gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, 4, 0);
    gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    CLOSE_DISPS(gfxCtx);
}

// ===========================================================================
// The ported events
// ===========================================================================

// E2 Starfall Bounty: stand where the stars land to catch them.
static void AStarfall_Start(PlayState* play) {
    InitHz();
}

static void AStarfall_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    s32 interval = SEC(MF_MAX(1, 10 - P(0)) * 0.5f + 1.0f);
    s32 fall = SEC(P(1) / 10.0f);

    if (Every(interval) && sAHzN < HZ_MAX) {
        Hz* h = &sAHz[sAHzN++];
        V3_FindSpot(play, &h->pos, 120.0f, 380.0f, (s16)Rng_Range(0, 0xFFFF));
        h->t = fall;
        h->state = 1;
        Mf_Sfx(NA_SE_SY_KINSTA_MARK_APPEAR);
    }
    for (i = 0; i < sAHzN; i++) {
        Hz* h = &sAHz[i];
        if (h->state != 1) {
            continue;
        }
        if (--h->t <= 0) {
            h->state = 0;
            if (Mf_DistXZ(&h->pos, &p->actor.world.pos) < 60.0f &&
                fabsf(h->pos.y - p->actor.world.pos.y) < 80.0f) {
                gEv.score++;
                Mf_HealPlayer(1);
                Mf_Sfx(NA_SE_SY_GET_ITEM);
                Sparkle(play, &h->pos, 255, 240, 120);
            } else {
                Sparkle(play, &h->pos, 120, 120, 160);
            }
        }
    }
}

static void AStarfall_Draw3D(PlayState* play) {
    s32 i;
    s32 fall = MF_MAX(1, SEC(P(1) / 10.0f));
    for (i = 0; i < sAHzN; i++) {
        Hz* h = &sAHz[i];
        if (h->state == 1) {
            Vec3f star = h->pos;
            MfColor gold = { 255, 230, 120, 220 };
            star.y += 700.0f * h->t / fall + 20.0f;
            Draw3D_Ring(&h->pos, 60.0f, 10.0f, cGood);
            Draw3D_Orb(&star, 35.0f, gold);
        }
    }
}

static void AStarfall_Draw2D(PlayState* play) {
    char buf[24];
    buf[0] = '\0';
    Str_Cat(buf, "Stars caught: ", sizeof(buf));
    Str_CatInt(buf, gEv.score, sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

static void AStarfall_End(PlayState* play, s32 success) {
    if (success && gEv.score > 0) {
        V3_GiveMarks((gEv.score * MF_MAX(1, P(2))), "stars caught");
    }
}

// E3 Dowsing Moon: follow the warmth, then press A to dig up the cache.
static void ADowsing_Start(PlayState* play) {
    Player* p = Pl(play);
    f32 d = P(0) * 10.0f;
    InitHz();
    V3_FindSpot(play, &sAHz[0].pos, d * 0.6f, d, (s16)Rng_Range(0, 0xFFFF));
    gEv.fa = Mf_DistXZ(&sAHz[0].pos, &p->actor.world.pos);
}

static void ADowsing_Update(PlayState* play) {
    Player* p = Pl(play);
    f32 d = Mf_DistXZ(&sAHz[0].pos, &p->actor.world.pos);
    s32 beat = (s32)MF_CLAMP(d / 25.0f, 3, 40);

    gEv.fb = d;
    if (Every(beat)) {
        Mf_Sfx(NA_SE_SY_WARNING_COUNT_N);
        if (d < 200.0f) {
            Rumble_Request(0.0f, 120, 4, 60);
        }
    }
    if (d < 55.0f) {
        V3_SetPrompt("Press A to dig!");
        if (Pressed(BTN_A)) {
            s32 bonus = MF_MAX(0, (gEv.duration - gEv.timer) / FPS);
            gEv.score = P(1) + bonus / 3;
            Sparkle(play, &sAHz[0].pos, 255, 220, 80);
            Mf_Sfx(NA_SE_SY_GET_BOXITEM);
            Mf_HealPlayer(4);
            gEv.succeeded = true;
        }
    }
}

static void ADowsing_Draw2D(PlayState* play) {
    s32 warmth = (s32)MF_CLAMP(100.0f - gEv.fb / 9.0f, 0, 100);
    MfColor c = { (u8)(80 + warmth * 17 / 10), 80, (u8)(255 - warmth * 2), 230 };
    Bar2D(36, warmth, 100, c, "Warmth");
}

static void ADowsing_End(PlayState* play, s32 success) {
    if (gEv.score > 0) {
        V3_GiveMarks((gEv.score), "buried cache");
    } else {
        MfColor c = { 200, 200, 200, 255 };
        V3_Notify("The cache sinks back into the earth.", c);
    }
}

// E5 Giant's Gale: wind shoves you. Counter: push the stick into the wind.
static void AGale_Start(PlayState* play) {
    gEv.hits = Rng_Range(0, 0xFFFF);
}

static void AGale_Update(PlayState* play) {
    Player* p = Pl(play);
    s16 windYaw = (s16)gEv.hits;
    f32 strength = P(0) * 1.0f;
    s32 bracing;
    s16 diff;

    if (Every(SEC(8))) {
        gEv.hits = windYaw + (s16)Rng_Range(0x3000, 0xC000);
        windYaw = (s16)gEv.hits;
        Mf_Sfx(NA_SE_EV_WIND_TRAP);
    }
    diff = Mf_StickWorldYaw(play) - (s16)(windYaw + 0x8000);
    bracing = (Mf_StickMag() > 30.0f) && (ABS(diff) < 0x2800);
    gEv.score = bracing;
    p->pushedYaw = windYaw;
    p->pushedSpeed = bracing ? strength * 0.15f : strength;
    if (!bracing && Every(SEC(P(1) / 10.0f + 0.1f))) {
        Mf_DamagePlayer(1, false, windYaw);
        gEv.failed = true;
    }
    if (Every(3)) {
        Vec3f pos = p->actor.world.pos;
        Vec3f vel;
        Vec3f acc = { 0.0f, 0.0f, 0.0f };
        Color_RGBA8 prim = { 220, 220, 200, 120 };
        Color_RGBA8 env = { 160, 150, 130, 60 };
        pos.x -= Math_SinS(windYaw) * 200.0f + Rng_Range(-150, 150);
        pos.z -= Math_CosS(windYaw) * 200.0f + Rng_Range(-150, 150);
        pos.y += Rng_Range(10, 90);
        vel.x = Math_SinS(windYaw) * 18.0f;
        vel.z = Math_CosS(windYaw) * 18.0f;
        vel.y = 0.5f;
        EffectSsDust_Spawn(play, 0, &pos, &vel, &acc, &prim, &env, 150, 20, 15, 0);
    }
}

static void AGale_Draw2D(PlayState* play) {
    s16 rel = (s16)gEv.hits - Camera_GetCamDirYaw(GET_ACTIVE_CAM(play));
    const char* dir = (ABS(rel) < 0x2000) ? "Wind: blowing AWAY from you"
                      : (ABS(rel) > 0x6000) ? "Wind: blowing IN your face"
                      : (rel > 0) ? "Wind: blowing to the LEFT" : "Wind: blowing to the RIGHT";
    Draw2D_TextCentered(34, gEv.score ? (MfColor){ 120, 255, 140, 255 } : (MfColor){ 255, 160, 120, 255 }, dir);
}

// E7 Sinking Sands: if you stand still, you sink.
static void ASinking_Update(PlayState* p0) {
    Player* p = Pl(p0);
    if (p->speedXZ < 1.5f && Mf_PlayerGrounded()) {
        gEv.hits++;
        if (gEv.hits > SEC(1.0f) && Every(MF_MAX(4, SEC(P(0) / 10.0f)))) {
            Vec3f vel = { 0.0f, 3.0f, 0.0f };
            Vec3f acc = { 0.0f, -0.2f, 0.0f };
            Color_RGBA8 prim = { 170, 140, 90, 200 };
            Color_RGBA8 env = { 120, 90, 50, 120 };
            Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
            EffectSsDust_Spawn(p0, 0, &p->actor.world.pos, &vel, &acc, &prim, &env, 200, 30, 15, 0);
            gEv.failed = true;
        }
    } else {
        gEv.hits = 0;
    }
}

static void ASinking_Draw3D(PlayState* play) {
    Player* p = Pl(play);
    MfColor sand = { 170, 130, 70, (u8)MF_MIN(200, 60 + gEv.hits * 4) };
    Draw3D_Disk(&p->actor.world.pos, 70.0f, sand);
}

// E8 Frostbite: the cold builds. Counter: roll to shake it off.
static void AFrost_Update(PlayState* play) {
    Player* p = Pl(play);
    if (Every(MF_MAX(1, 6 - P(0)))) {
        gEv.hits++;
    }
    if (Pressed(BTN_A) && Mf_PlayerGrounded() && p->speedXZ > 3.0f) {
        gEv.hits = MF_MAX(0, gEv.hits - P(1));
        Sparkle(play, &p->actor.world.pos, 180, 230, 255);
    }
    if (gEv.hits >= 100) {
        EffectSsIcePiece_SpawnBurst(play, &p->actor.world.pos, 1.0f);
        Mf_Sfx(NA_SE_EV_ICE_FREEZE);
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        gChillTimer = SEC(3);
        gEv.hits = 30;
        gEv.failed = true;
    }
}

static void AFrost_Draw2D(PlayState* play) {
    MfColor edge = { 150, 210, 255, (u8)MF_MIN(160, gEv.hits * 3 / 2) };
    Draw2D_Rect(0, 0, 6, SCREEN_HEIGHT, edge);
    Draw2D_Rect(SCREEN_WIDTH - 6, 0, 6, SCREEN_HEIGHT, edge);
    Bar2D(36, gEv.hits, 100, (MfColor){ 150, 210, 255, 230 }, "Frost");
}

// E9 Scythe Sweep: expanding shockwaves. Counter: jump them (or stand in the gap).
static void AScythe_Start(PlayState* play) {
    InitHz();
    sAHz[0].state = 0;
}

static void AScythe_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sAHz[0];

    if (h->state == 0 && Every(SEC(MF_MAX(2, P(0))))) {
        V3_FindSpot(play, &h->pos, 150.0f, 280.0f, (s16)Rng_Range(0, 0xFFFF));
        h->r = 0.0f;
        h->yaw = (s16)Rng_Range(0, 0xFFFF);
        h->state = 1;
        Mf_Sfx(NA_SE_IT_SWORD_SWING_HARD);
    }
    if (h->state == 1) {
        f32 prev = h->r;
        f32 d = Mf_DistXZ(&h->pos, &p->actor.world.pos);
        h->r += P(1);
        if (prev < d && h->r >= d) {
            s16 toPlayer = Mf_YawTo(&h->pos, &p->actor.world.pos);
            s16 gapDiff = toPlayer - h->yaw;
            s32 inGap = ABS(gapDiff) < 0x1555;
            s32 airborne = !Mf_PlayerGrounded();
            if (!inGap && !airborne) {
                Mf_DamagePlayer(MF_MAX(1, P(2)), true, toPlayer);
                gEv.failed = true;
            } else {
                Sparkle(play, &p->actor.world.pos, 200, 255, 200);
            }
        }
        if (h->r > 1000.0f) {
            h->state = 0;
        }
    }
}

static void AScythe_Draw3D(PlayState* play) {
    Hz* h = &sAHz[0];
    if (h->state == 1) {
        MfColor gap = { 40, 255, 120, 90 };
        Draw3D_Ring(&h->pos, h->r, 18.0f, (MfColor){ 255, 40, 200, 200 });
        Draw3D_Wedge(&h->pos, h->r, h->yaw, 0x1555, gap);
    }
}

// E10 Moth Swarm: moon moths cling to you. Counter: a spin attack scatters them.
static void AMoths_Start(PlayState* play) {
    InitHz();
}

static s32 AIsSpinning(Player* p) {
    if (p->meleeWeaponState == 0) {
        return false;
    }
    if (p->meleeWeaponAnimation >= PLAYER_MWA_SPIN_ATTACK_1H && p->meleeWeaponAnimation <= PLAYER_MWA_BIG_SPIN_2H) {
        return true;
    }
    // Non-human forms have no spin attack: their strongest swing counts.
    return (p->transformation != PLAYER_FORM_HUMAN) && (p->transformation != PLAYER_FORM_FIERCE_DEITY);
}

static void AMoths_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    s32 attached = 0;

    if (Every(SEC(MF_MAX(1, 8 - P(0)) * 0.5f)) && sAHzN < HZ_MAX) {
        Hz* h = &sAHz[sAHzN++];
        V3_FindSpot(play, &h->pos, 200.0f, 350.0f, (s16)Rng_Range(0, 0xFFFF));
        h->pos.y += 60.0f;
        h->state = 1;
        h->yaw = (s16)Rng_Range(0, 0xFFFF);
    }
    for (i = 0; i < sAHzN; i++) {
        Hz* h = &sAHz[i];
        if (h->state == 1) {
            Vec3f target = p->actor.world.pos;
            f32 d;
            target.y += 40.0f;
            d = Mf_DistXZ(&h->pos, &target);
            h->pos.x += (target.x - h->pos.x) * 0.04f + Rng_Range(-3, 3);
            h->pos.z += (target.z - h->pos.z) * 0.04f + Rng_Range(-3, 3);
            h->pos.y += (target.y - h->pos.y) * 0.05f;
            if (d < 35.0f) {
                h->state = 2;
            }
        } else if (h->state == 2) {
            attached++;
            h->yaw += 0x900;
            h->pos.x = p->actor.world.pos.x + Math_SinS(h->yaw) * 28.0f;
            h->pos.z = p->actor.world.pos.z + Math_CosS(h->yaw) * 28.0f;
            h->pos.y = p->actor.world.pos.y + 30.0f + (i % 3) * 12.0f;
        }
    }
    gEv.hits = attached;
    if (AIsSpinning(p)) {
        s32 cleared = 0;
        for (i = 0; i < sAHzN; i++) {
            if (sAHz[i].state != 0 && Mf_DistXZ(&sAHz[i].pos, &p->actor.world.pos) < 120.0f) {
                if (p->transformation != PLAYER_FORM_HUMAN && cleared >= 1) {
                    break;
                }
                sAHz[i].state = 0;
                Sparkle(play, &sAHz[i].pos, 200, 200, 255);
                cleared++;
            }
        }
    }
    if (attached > 0 && Every(SEC(MF_MAX(1, P(1)) / 2.0f))) {
        Mf_DamagePlayer(MF_MAX(1, attached / 2), false, 0);
        gEv.failed = true;
    }
}

static void AMoths_Draw3D(PlayState* play) {
    s32 i;
    for (i = 0; i < sAHzN; i++) {
        if (sAHz[i].state != 0) {
            MfColor c = (sAHz[i].state == 2) ? (MfColor){ 255, 120, 255, 230 } : (MfColor){ 200, 200, 255, 200 };
            Draw3D_Orb(&sAHz[i].pos, 12.0f, c);
        }
    }
}

static void AMoths_Draw2D(PlayState* play) {
    char buf[32];
    buf[0] = '\0';
    Str_Cat(buf, "Moths clinging: ", sizeof(buf));
    Str_CatInt(buf, gEv.hits, sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

// E12 Hex of Silence: every B or C press hurts. Counter: don't attack or use items.
static void ASilence_Update(PlayState* play) {
    if (Pressed(BTN_B | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN) && gEv.hits <= 0) {
        Player* p = Pl(play);
        Mf_DamagePlayer(MF_MAX(1, P(0)), false, 0);
        Lightning(play, &p->actor.world.pos);
        gEv.hits = 10;
        gEv.failed = true;
    }
    if (gEv.hits > 0) {
        gEv.hits--;
    }
}

static void ASilence_Draw2D(PlayState* play) {
    MfColor c = { 150, 60, 200, (u8)(50 + ((s32)gPlayState_v2->gameplayFrames % 20) * 3) };
    Draw2D_Rect(0, 0, SCREEN_WIDTH, 2, c);
    Draw2D_Rect(0, SCREEN_HEIGHT - 2, SCREEN_WIDTH, 2, c);
}

// E14 Moon Tether: stay inside the drifting circle.
static void ATether_Start(PlayState* play) {
    InitHz();
    V3_FindSpot(play, &sAHz[0].pos, 150.0f, 260.0f, (s16)Rng_Range(0, 0xFFFF));
    sAHz[0].yaw = (s16)Rng_Range(0, 0xFFFF);
}

static void ATether_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sAHz[0];
    f32 radius = P(0) * 10.0f;
    Vec3f next = h->pos;

    if (Every(SEC(4))) {
        h->yaw += (s16)Rng_Range(-0x5000, 0x5000);
    }
    next.x += Math_SinS(h->yaw) * (P(1) / 10.0f);
    next.z += Math_CosS(h->yaw) * (P(1) / 10.0f);
    next.y = Mf_FloorY(&(Vec3f){ next.x, next.y + 80.0f, next.z }, BGCHECK_Y_MIN);
    if (next.y > BGCHECK_Y_MIN + 1.0f && fabsf(next.y - h->pos.y) < 60.0f) {
        h->pos = next;
    } else {
        h->yaw += 0x8000;
    }
    if (gEv.timer > SEC(5) && Mf_DistXZ(&h->pos, &p->actor.world.pos) > radius) {
        if (Every(SEC(1))) {
            Mf_DamagePlayer(MF_MAX(1, P(2)), true, Mf_YawTo(&p->actor.world.pos, &h->pos));
            Lightning(play, &p->actor.world.pos);
            gEv.failed = true;
        }
    }
}

static void ATether_Draw3D(PlayState* play) {
    Hz* h = &sAHz[0];
    Player* p = Pl(play);
    s32 inside = Mf_DistXZ(&h->pos, &p->actor.world.pos) <= P(0) * 10.0f;
    Draw3D_Ring(&h->pos, P(0) * 10.0f, 14.0f, inside ? cGood : cWarn);
    Draw3D_Pillar(&h->pos, 8.0f, 200.0f, cMoon);
}

static void ATether_Draw2D(PlayState* play) {
    if (gEv.timer < SEC(5)) {
        Draw2D_TextCentered(34, cText, "Get inside the circle!");
    }
}

// E15 Moon's Grasp: a hand grabs you. Counter: mash A to break free.
static void AGrasp_Start(PlayState* play) {
    gEv.hits = SEC(3);  // countdown
    gEv.score = 0;      // 0 idle, 1 shadow, 2 grabbed
}

static void AGrasp_Update(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    Player* p = Pl(play);

    gEv.hits--;
    if (gEv.score == 0 && gEv.hits <= 0) {
        gEv.score = 1;
        gEv.hits = SEC(1.5f);
        Mf_Sfx(NA_SE_EN_STALKIDS_APPEAR);
    } else if (gEv.score == 1 && gEv.hits <= 0) {
        gEv.score = 2;
        gEv.hits = SEC(3);
        gEv.fa = 0.0f;
        Mf_Sfx(NA_SE_EN_BUBLE_BITE);
    } else if (gEv.score == 2) {
        if (Pressed(BTN_A)) {
            gEv.fa += 1.0f;
        }
        in->cur.button &= ~(BTN_A | BTN_B);
        in->press.button &= ~(BTN_A | BTN_B);
        in->rel.stick_x = in->rel.stick_y = 0;
        in->cur.stick_x = in->cur.stick_y = 0;
        if (gEv.fa >= P(0)) {
            Sparkle(play, &p->actor.world.pos, 255, 255, 255);
            Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
            gEv.score = 0;
            gEv.hits = SEC(Rng_Range(4, 7));
        } else if (gEv.hits <= 0) {
            Mf_DamagePlayer(MF_MAX(1, P(1)), true, (s16)Rng_Range(0, 0xFFFF));
            gEv.failed = true;
            gEv.score = 0;
            gEv.hits = SEC(Rng_Range(4, 7));
        }
    }
}

static void AGrasp_Draw3D(PlayState* play) {
    Player* p = Pl(play);
    if (gEv.score >= 1) {
        MfColor shadow = { 30, 0, 50, 200 };
        Draw3D_Disk(&p->actor.world.pos, gEv.score == 1 ? 90.0f - gEv.hits * 2.0f : 60.0f, shadow);
    }
}

static void AGrasp_Draw2D(PlayState* play) {
    if (gEv.score == 2) {
        Bar2D(100, (s32)gEv.fa, P(0), (MfColor){ 255, 220, 90, 240 }, "MASH A");
    } else if (gEv.score == 1) {
        Draw2D_TextCentered(100, (MfColor){ 255, 120, 120, 255 }, "Something reaches up from below...");
    }
}

// E16 Heartbeat Hex: press A exactly on the beat.
static void AHeartbeat_Start(PlayState* play) {
    gEv.hits = 0;  // frames into current beat
    gEv.score = 0; // pressed during this beat
}

static void AHeartbeat_Update(PlayState* play) {
    s32 period = MF_MAX(10, SEC(P(0) / 10.0f));
    s32 window = MF_MAX(2, P(1));
    s32 t = gEv.timer % period;
    s32 distToBeat = MF_MIN(t, period - t);

    if (t == 0) {
        Mf_Sfx(NA_SE_SY_HITPOINT_ALARM);
    }
    if (Pressed(BTN_A)) {
        if (distToBeat <= window) {
            gEv.score = 1;
            Mf_Sfx(NA_SE_SY_DECIDE);
        } else {
            Mf_DamagePlayer(1, false, 0);
            gEv.failed = true;
        }
    }
    if (t == window + 1) {
        if (!gEv.score && gEv.timer > period) {
            Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
            gEv.failed = true;
        }
        gEv.score = 0;
    }
}

static void AHeartbeat_Draw2D(PlayState* play) {
    s32 period = MF_MAX(10, SEC(P(0) / 10.0f));
    s32 t = gEv.timer % period;
    s32 size = 12 + ((t < 4) ? (4 - t) * 4 : 0);
    s32 ring = 12 + (period - t) * 40 / period;
    MfColor heart = { 230, 30, 60, 240 };
    MfColor guide = { 255, 200, 200, 120 };
    Draw2D_Rect(160 - ring, 200 - 1, ring * 2, 2, guide);
    Draw2D_Rect(160 - size / 2, 200 - size / 2, size, size, heart);
    Draw2D_TextCentered(214, cText, "Press A when the heart beats");
}

static void AReflex_Start(PlayState* play) {
    gEv.hits = SEC(2);  // countdown to the next prompt
    gEv.score = -1;     // active prompt index, -1 = none
}

static void AReflex_Update(PlayState* play) {
    s32 window = MF_MAX(4, SEC(P(0) / 10.0f));
    s32 i;

    gEv.hits--;
    if (gEv.score < 0) {
        if (gEv.hits <= 0) {
            gEv.score = Rng_Range(0, 3);
            gEv.hits = window;
            Mf_Sfx(NA_SE_SY_WARNING_COUNT_E);
        }
        return;
    }
    for (i = 0; i < 4; i++) {
        if (Pressed(sReflexBtns[i])) {
            if (i == gEv.score) {
                Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
            } else {
                Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
                Mf_Sfx(NA_SE_SY_ERROR);
                gEv.failed = true;
            }
            gEv.score = -1;
            gEv.hits = SEC(Rng_Range(20, 45) / 10.0f);
            return;
        }
    }
    if (gEv.hits <= 0) {
        Player* p = Pl(play);
        Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
        Lightning(play, &p->actor.world.pos);
        gEv.failed = true;
        gEv.score = -1;
        gEv.hits = SEC(Rng_Range(20, 45) / 10.0f);
    }
}

static void AReflex_Draw2D(PlayState* play) {
    if (gEv.score >= 0) {
        MfColor bg = { 60, 0, 30, 220 };
        char buf[16];
        buf[0] = '\0';
        Str_Cat(buf, "PRESS ", sizeof(buf));
        Str_Cat(buf, sReflexNames[gEv.score], sizeof(buf));
        Draw2D_Rect(110, 96, 100, 22, bg);
        Draw2D_TextCentered(103, Mf_Rainbow((s32)gPlayState_v2->gameplayFrames * 20, 255), buf);
    }
}

// E19 Gorgon's Gaze: an eye hangs in the air. Counter: keep your camera off it.
static void AGorgon_Place(PlayState* play) {
    Player* p = Pl(play);
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    sAHz[0].pos = p->actor.world.pos;
    sAHz[0].pos.x += Math_SinS(yaw) * 500.0f;
    sAHz[0].pos.z += Math_CosS(yaw) * 500.0f;
    sAHz[0].pos.y += 160.0f;
}

static void AGorgon_Start(PlayState* play) {
    InitHz();
    AGorgon_Place(play);
}

static void AGorgon_Update(PlayState* play) {
    Camera* cam = GET_ACTIVE_CAM(play);
    Vec3f look;
    Vec3f toEye;
    f32 ll;
    f32 le;
    f32 dot;

    if (Every(SEC(MF_MAX(2, P(0))))) {
        AGorgon_Place(play);
        Mf_Sfx(NA_SE_EN_WIZ_LAUGH);
    }
    look.x = cam->at.x - cam->eye.x;
    look.y = cam->at.y - cam->eye.y;
    look.z = cam->at.z - cam->eye.z;
    toEye.x = sAHz[0].pos.x - cam->eye.x;
    toEye.y = sAHz[0].pos.y - cam->eye.y;
    toEye.z = sAHz[0].pos.z - cam->eye.z;
    ll = sqrtf(look.x * look.x + look.y * look.y + look.z * look.z);
    le = sqrtf(toEye.x * toEye.x + toEye.y * toEye.y + toEye.z * toEye.z);
    dot = (look.x * toEye.x + look.y * toEye.y + look.z * toEye.z) / MF_MAX(1.0f, ll * le);
    if (dot > 0.82f) { // within ~35 degrees of the centre of the screen
        gEv.hits += P(1);
    } else {
        gEv.hits = MF_MAX(0, gEv.hits - 2);
    }
    if (gEv.hits >= 100) {
        Player* p = Pl(play);
        gEv.hits = 0;
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        gChillTimer = SEC(2);
        EffectSsIcePiece_SpawnBurst(play, &p->actor.world.pos, 0.8f);
        gEv.failed = true;
    }
}

static void AGorgon_Draw3D(PlayState* play) {
    Vec3f base = sAHz[0].pos;
    base.y -= 160.0f;
    Draw3D_Orb(&sAHz[0].pos, 120.0f, (MfColor){ 255, 245, 230, 255 });
    Draw3D_Orb(&sAHz[0].pos, 50.0f, (MfColor){ 220, 0, 30, 255 });
    Draw3D_Pillar(&base, 10.0f, 160.0f, (MfColor){ 200, 0, 40, 200 });
}

static void AGorgon_Draw2D(PlayState* play) {
    Draw2D_WorldMarker(play, &sAHz[0].pos, (MfColor){ 255, 60, 60, 255 }, "(O)");
    Bar2D(36, gEv.hits, 100, (MfColor){ 170, 170, 170, 230 }, "Stone");
}

// E21 Dissonance: a sour melody saps your heart. Counter: answer with 3 ocarina notes.
static s32 ADissonance_Eligible(PlayState* play) {
    return INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME;
}

static void ADissonance_Update(PlayState* play) {
    if (Every(SEC(MF_MAX(1, P(0))))) {
        Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
        Mf_Sfx(NA_SE_SY_OCARINA_ERROR);
        gEv.failed = true;
    }
}

static void ADissonance_Draw2D(PlayState* play) {
    s32 i;
    for (i = 0; i < 6; i++) {
        s32 y = 60 + i * 20 + (s32)(Math_SinS((s32)gPlayState_v2->gameplayFrames * 1500 + i * 9000) * 6.0f);
        MfColor c = { 120, 60, 160, 90 };
        Draw2D_Rect(0, y, SCREEN_WIDTH, 1, c);
    }
    {
        char buf[32];
        buf[0] = '\0';
        Str_Cat(buf, "Notes played: ", sizeof(buf));
        Str_CatInt(buf, MF_CLAMP(sV3Notes - gEv.notesAtStart, 0, 3), sizeof(buf));
        Str_Cat(buf, "/3", sizeof(buf));
        Draw2D_TextCentered(34, cText, buf);
    }
}

// E22 Mirror Madness: controls invert. Counter: face the mirror shard and hold Z to shatter it.
static void AMirror_Start(PlayState* play) {
    Player* p = Pl(play);
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    InitHz();
    sAHz[0].pos = p->actor.world.pos;
    sAHz[0].pos.x += Math_SinS(yaw) * 300.0f;
    sAHz[0].pos.z += Math_CosS(yaw) * 300.0f;
    sAHz[0].pos.y += 110.0f;
    gEv.hits = 0;
}

static void AMirror_Update(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    Player* p = Pl(play);
    s16 diff = Mf_YawTo(&p->actor.world.pos, &sAHz[0].pos) - p->actor.shape.rot.y;

    in->rel.stick_x = -in->rel.stick_x;
    in->rel.stick_y = -in->rel.stick_y;
    in->cur.stick_x = -in->cur.stick_x;
    in->cur.stick_y = -in->cur.stick_y;

    if (Held(BTN_Z) && ABS(diff) < 0x1200) {
        gEv.hits++;
        if (gEv.hits >= SEC(P(0) / 10.0f)) {
            EffectSsIcePiece_SpawnBurst(play, &sAHz[0].pos, 1.2f);
            Mf_Sfx(NA_SE_EN_SLIME_BREAK);
            gEv.succeeded = true;
        }
    } else {
        gEv.hits = MF_MAX(0, gEv.hits - 1);
    }
    if (Every(SEC(MF_MAX(1, P(1))))) {
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        gEv.failed = true;
    }
}

static void AMirror_Draw3D(PlayState* play) {
    MfColor glass = { 200, 240, 255, (u8)(150 + ((s32)gPlayState_v2->gameplayFrames % 10) * 8) };
    Draw3D_Orb(&sAHz[0].pos, 45.0f, glass);
    Draw3D_Pillar(&(Vec3f){ sAHz[0].pos.x, sAHz[0].pos.y - 110.0f, sAHz[0].pos.z }, 4.0f, 110.0f, glass);
}

static void AMirror_Draw2D(PlayState* play) {
    Bar2D(36, gEv.hits, MF_MAX(1, SEC(P(0) / 10.0f)), (MfColor){ 200, 240, 255, 230 }, "Shatter");
}

// E23 Volley of Stars: star volleys from a marked direction. Counter: face them and guard (hold R).
static void AVolley_Start(PlayState* play) {
    InitHz();
    gEv.hits = SEC(2);
    gEv.score = 0;
}

static void AVolley_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sAHz[0];

    if (--gEv.hits == SEC(P(0) / 10.0f)) {
        s16 yaw = (s16)Rng_Range(0, 0xFFFF);
        h->yaw = yaw;
        h->pos = p->actor.world.pos;
        h->pos.x += Math_SinS(yaw) * 450.0f;
        h->pos.z += Math_CosS(yaw) * 450.0f;
        h->pos.y += 70.0f;
        h->state = 1;
        Mf_Sfx(NA_SE_SY_WARNING_COUNT_N);
    }
    if (gEv.hits <= 0) {
        if (h->state == 1) {
            s16 toVolley = Mf_YawTo(&p->actor.world.pos, &h->pos);
            s16 diff = toVolley - p->actor.shape.rot.y;
            if (Held(BTN_R) && ABS(diff) < 0x2000) {
                Mf_Sfx(NA_SE_IT_SHIELD_REFLECT_SW);
                Sparkle(play, &p->actor.focus.pos, 255, 255, 180);
            } else {
                Mf_DamagePlayer(MF_MAX(1, P(1)), true, toVolley + 0x8000);
                gEv.failed = true;
            }
        }
        h->state = 0;
        gEv.hits = SEC(MF_MAX(2, P(2)));
    }
}

static void AVolley_Draw3D(PlayState* play) {
    Hz* h = &sAHz[0];
    if (h->state == 1) {
        Player* p = Pl(play);
        s32 total = MF_MAX(1, SEC(P(0) / 10.0f));
        f32 t = 1.0f - (f32)gEv.hits / total;
        s32 i;
        Draw3D_Orb(&h->pos, 50.0f, (MfColor){ 255, 230, 120, 220 });
        for (i = 0; i < 3; i++) {
            Vec3f s;
            s.x = h->pos.x + (p->actor.world.pos.x - h->pos.x) * t + (i - 1) * 25.0f;
            s.y = h->pos.y + (p->actor.world.pos.y + 40.0f - h->pos.y) * t;
            s.z = h->pos.z + (p->actor.world.pos.z - h->pos.z) * t;
            Draw3D_Orb(&s, 18.0f, (MfColor){ 255, 255, 200, 230 });
        }
    }
}

// E25 Sleeping Giant: running wakes the giant. Counter: walk softly.
static void AGiant_Update(PlayState* play) {
    Player* p = Pl(play);
    f32 limit = P(0) / 10.0f;
    if (p->speedXZ > limit && Mf_PlayerGrounded()) {
        gEv.hits += (s32)((p->speedXZ - limit) * P(1) / 4.0f) + 1;
    } else if (gEv.hits > 0) {
        gEv.hits--;
    }
    if (gEv.hits >= 100) {
        gEv.hits = 0;
        Quake(play, 12, 30);
        Mf_Sfx(NA_SE_EV_EARTHQUAKE);
        Mf_DamagePlayer(MF_MAX(1, P(2)), true, (s16)Rng_Range(0, 0xFFFF));
        gEv.failed = true;
    }
}

static void AGiant_Draw2D(PlayState* play) {
    Bar2D(36, gEv.hits, 100, (MfColor){ 255, 170, 60, 230 }, "Noise");
}

static void ASimon_NewRound(void) {
    s32 i;
    sASeqLen = MF_MIN(10, 3 + gEv.score);
    for (i = 0; i < sASeqLen; i++) {
        sASeq[i] = Rng_Range(0, 3);
    }
    sASeqPos = 0;
    gEv.hits = 0; // show phase timer
    gEv.fa = 0.0f; // 0 = showing, 1 = input
}

static void ASimon_Start(PlayState* play) {
    gEv.score = 0;
    ASimon_NewRound();
}

static void ASimon_Update(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    static const u16 dirs[4] = { BTN_DUP, BTN_DRIGHT, BTN_DDOWN, BTN_DLEFT };
    s32 step = MF_MAX(6, SEC(P(0) / 10.0f));
    s32 i;

    in->cur.button &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT);
    in->press.button &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT);
    if (gEv.fa == 0.0f) {
        gEv.hits++;
        if (gEv.hits % step == 1) {
            Mf_Sfx(NA_SE_SY_CURSOR);
        }
        if (gEv.hits >= step * sASeqLen) {
            gEv.fa = 1.0f;
            gEv.hits = SEC(8);
        }
        return;
    }
    gEv.hits--;
    for (i = 0; i < 4; i++) {
        if (Pressed(dirs[i])) {
            if (sASeq[sASeqPos] == i) {
                sASeqPos++;
                Mf_Sfx(NA_SE_SY_DECIDE);
                if (sASeqPos >= sASeqLen) {
                    gEv.score++;
                    Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
                    if (gEv.score >= MF_MAX(1, P(1))) {
                        gEv.succeeded = true;
                        return;
                    }
                    ASimon_NewRound();
                }
            } else {
                Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
                Mf_Sfx(NA_SE_SY_ERROR);
                gEv.failed = true;
                ASimon_NewRound();
            }
            return;
        }
    }
    if (gEv.hits <= 0) {
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        gEv.failed = true;
        ASimon_NewRound();
    }
}

static void ASimon_Draw2D(PlayState* play) {
    static const char* arrows[4] = { "^", ">", "v", "<" };
    MfColor bg = { 40, 20, 0, 200 };
    MfColor c = { 255, 220, 120, 255 };
    s32 step = MF_MAX(6, SEC(P(0) / 10.0f));
    char buf[32];
    s32 i;

    Draw2D_Rect(60, 70, 200, 44, bg);
    if (gEv.fa == 0.0f) {
        s32 idx = gEv.hits / step;
        Draw2D_TextCentered(74, c, "Watch closely...");
        if (idx < sASeqLen && (gEv.hits % step) < step - 2) {
            Draw2D_TextCentered(92, Mf_Rainbow(idx * 60, 255), arrows[sASeq[idx]]);
        }
    } else {
        Draw2D_TextCentered(74, c, "Repeat it with the D-pad!");
        buf[0] = '\0';
        for (i = 0; i < sASeqLen; i++) {
            Str_Cat(buf, i < sASeqPos ? arrows[sASeq[i]] : "_", sizeof(buf));
            Str_Cat(buf, " ", sizeof(buf));
        }
        Draw2D_TextCentered(92, cText, buf);
    }
}

// E31 Pressure Seals: step on the seals in numbered order.
static void ASeals_Start(PlayState* play) {
    s32 i;
    InitHz();
    sAHzN = MF_CLAMP(P(0), 2, 6);
    for (i = 0; i < sAHzN; i++) {
        V3_FindSpot(play, &sAHz[i].pos, 180.0f, 450.0f, (s16)(0x10000 * i / sAHzN + Rng_Range(-0x1000, 0x1000)));
        sAHz[i].state = 1;
    }
    gEv.score = 0; // next seal to press
    gEv.hits = 0;  // wrong-step cooldown
}

static void ASeals_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    if (gEv.hits > 0) {
        gEv.hits--;
    }
    for (i = 0; i < sAHzN; i++) {
        Hz* h = &sAHz[i];
        if (h->state != 1 || Mf_DistXZ(&h->pos, &p->actor.world.pos) > 55.0f ||
            fabsf(h->pos.y - p->actor.world.pos.y) > 60.0f) {
            continue;
        }
        if (i == gEv.score) {
            h->state = 0;
            gEv.score++;
            Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
            Sparkle(play, &h->pos, 120, 255, 180);
            if (gEv.score >= sAHzN) {
                gEv.succeeded = true;
            }
        } else if (gEv.hits == 0) {
            Mf_DamagePlayer(MF_MAX(1, P(1)), true, Mf_YawTo(&h->pos, &p->actor.world.pos));
            Mf_Sfx(NA_SE_SY_ERROR);
            gEv.hits = SEC(1.5f);
            gEv.failed = true;
        }
    }
}

static void ASeals_Draw3D(PlayState* play) {
    s32 i;
    for (i = 0; i < sAHzN; i++) {
        if (sAHz[i].state == 1) {
            MfColor c = Mf_Rainbow(i * 60, 200);
            Draw3D_Ring(&sAHz[i].pos, 55.0f, 12.0f, c);
            if (i == gEv.score) {
                Draw3D_Pillar(&sAHz[i].pos, 10.0f, 160.0f, c);
            }
        }
    }
}

static void ASeals_Draw2D(PlayState* play) {
    char buf[40];
    MfColor c = Mf_Rainbow(gEv.score * 60, 255);
    buf[0] = '\0';
    Str_Cat(buf, "Next seal: the one with the light beam (", sizeof(buf));
    Str_CatInt(buf, gEv.score + 1, sizeof(buf));
    Str_Cat(buf, ")", sizeof(buf));
    Draw2D_TextCentered(34, c, buf);
}

static void ASeals_End(PlayState* play, s32 success) {
    if (!gEv.succeeded && !V3_PlayerDead() && success) {
        Player* p = Pl(play);
        Boom(play, &p->actor.world.pos);
        Mf_DamagePlayer(MF_MAX(1, P(2)), true, p->actor.shape.rot.y);
        gEv.failed = true;
    }
}

// ===========================================================================
// Wiring the ported events into the event flow
// ===========================================================================

typedef void (*V3PlayFn)(PlayState* play);
typedef void (*V3EndFn)(PlayState* play, s32 success);

typedef struct {
    V3PlayFn start;
    V3PlayFn update;
    V3PlayFn draw3d;
    V3PlayFn draw2d;
    V3EndFn end;
} V3Fns;

static const V3Fns sV3Fns[] = {
    { AStarfall_Start, AStarfall_Update, AStarfall_Draw3D, AStarfall_Draw2D, AStarfall_End },
    { ADowsing_Start, ADowsing_Update, NULL, ADowsing_Draw2D, ADowsing_End },
    { AGale_Start, AGale_Update, NULL, AGale_Draw2D, NULL },
    { NULL, ASinking_Update, ASinking_Draw3D, NULL, NULL },
    { NULL, AFrost_Update, NULL, AFrost_Draw2D, NULL },
    { AScythe_Start, AScythe_Update, AScythe_Draw3D, NULL, NULL },
    { AMoths_Start, AMoths_Update, AMoths_Draw3D, AMoths_Draw2D, NULL },
    { NULL, ASilence_Update, NULL, ASilence_Draw2D, NULL },
    { ATether_Start, ATether_Update, ATether_Draw3D, ATether_Draw2D, NULL },
    { AGrasp_Start, AGrasp_Update, AGrasp_Draw3D, AGrasp_Draw2D, NULL },
    { AHeartbeat_Start, AHeartbeat_Update, NULL, AHeartbeat_Draw2D, NULL },
    { AReflex_Start, AReflex_Update, NULL, AReflex_Draw2D, NULL },
    { AGorgon_Start, AGorgon_Update, AGorgon_Draw3D, AGorgon_Draw2D, NULL },
    { NULL, ADissonance_Update, NULL, ADissonance_Draw2D, NULL },
    { AMirror_Start, AMirror_Update, AMirror_Draw3D, AMirror_Draw2D, NULL },
    { AVolley_Start, AVolley_Update, AVolley_Draw3D, NULL, NULL },
    { NULL, AGiant_Update, NULL, AGiant_Draw2D, NULL },
    { ASimon_Start, ASimon_Update, NULL, ASimon_Draw2D, NULL },
    { ASeals_Start, ASeals_Update, ASeals_Draw3D, ASeals_Draw2D, ASeals_End },
};
_Static_assert(ARRAY_COUNT(sV3Fns) == ARRAY_COUNT(sV3Defs), "V3 event functions");

static const V3Fns* V3_Fns(s32 ev) {
    return V3_IsEvent(ev) ? &sV3Fns[ev - EV_STARFALL] : NULL;
}

static s32 sV3Running = -1; // which ported event gEv belongs to

static s32 V3_CanRun(PlayState* play, s32 ev) {
    if (ev == EV_DISSONANCE) {
        return INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME;
    }
    return true;
}

static void V3_OnStart(PlayState* play, s32 ev) {
    const V3Def* d = V3_Def(ev);

    if (d == NULL) {
        return;
    }
    bzero(&gEv, sizeof(gEv));
    gEv.id = ev;
    gEv.notesAtStart = sV3Notes;
    sV3Running = -1; // set up once the area is ready
    sV3Prompt = NULL;
    if (d->longEvent) {
        sTotalFrames = sFramesLeft = 60 * FPS; // runs until you finish, up to a minute
    }
}

static void V3_SceneSetup(PlayState* play) {
    const V3Fns* f = V3_Fns(sActive);

    if (f == NULL) {
        return;
    }
    sV3Input = &play->state.input[0];
    gEv.id = sActive;
    gEv.timer = 0;
    gEv.duration = sTotalFrames;
    sV3Running = sActive;
    if (f->start != NULL) {
        f->start(play);
    }
}

static void V3_Tick(PlayState* play) {
    const V3Fns* f = V3_Fns(sActive);

    if ((f == NULL) || (sV3Running != sActive)) {
        return;
    }
    sV3Input = &play->state.input[0];
    gEv.id = sActive;
    gEv.timer = sEventFrames;
    gEv.duration = sTotalFrames;
    sV3Prompt = NULL;
    if (f->update != NULL) {
        f->update(play);
    }
    if (gEv.succeeded && (sFramesLeft > 1)) {
        sFramesLeft = 1; // beaten: ends as survived
    }
}

static void V3_OnEnd(PlayState* play, s32 success) {
    const V3Fns* f = V3_Fns(sV3Running);

    if ((f != NULL) && (f->end != NULL)) {
        f->end(play, success);
    }
    sV3Running = -1;
    sV3Prompt = NULL;
}

// Did the player slip up on the running ported event? (halves the pay)
static s32 V3_Failed(void) {
    return V3_IsEvent(sActive) && gEv.failed;
}

// Every frame: ocarina notes (the event itself doesn't run while you play).
static void V3_Always(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;

    if (gChillTimer > 0) {
        Input* in = &play->state.input[0];

        gChillTimer--;
        in->cur.stick_x /= 2;
        in->cur.stick_y /= 2;
        in->rel.stick_x /= 2;
        in->rel.stick_y /= 2;
    }

    if ((msgCtx->msgMode != MSGMODE_OCARINA_PLAYING) || (msgCtx->ocarinaStaff == NULL)) {
        sV3NoteLast = -1;
    } else if (sV3NoteLast < 0) {
        sV3NoteLast = msgCtx->ocarinaStaff->pos;
    } else if ((msgCtx->ocarinaStaff->pos != sV3NoteLast) && (msgCtx->ocarinaStaff->pos != 0)) {
        sV3NoteLast = msgCtx->ocarinaStaff->pos;
        sV3Notes++;
    }
    if ((sActive == EV_DISSONANCE) && (sV3Notes - gEv.notesAtStart >= 3) && !gEv.succeeded) {
        gEv.succeeded = true;
        Audio_PlaySfx(NA_SE_SY_CORRECT_CHIME);
        Menu_ShowToast("The melody is answered.");
        if (sFramesLeft > 1) {
            sFramesLeft = 1;
        }
    }
}

static void V3_DrawWorld(PlayState* play) {
    const V3Fns* f = V3_Fns(sActive);

    if ((f == NULL) || (f->draw3d == NULL) || !sSceneReady || (sV3Running != sActive)) {
        return;
    }
    Draw3D_Begin(play);
    f->draw3d(play);
    Draw3D_End();
}

static Gfx* V3_Draw2D(PlayState* play, Gfx* gfx) {
    const V3Fns* f = V3_Fns(sActive);

    if ((f == NULL) || !sSceneReady || (sV3Running != sActive) || (sPauseReason == 1)) {
        return gfx;
    }
    sV3Gfx = &gfx;
    if (f->draw2d != NULL) {
        f->draw2d(play);
    }
    if (sV3Prompt != NULL) {
        MfColor bg = { 0, 0, 0, 160 };
        MfColor fg = { 255, 240, 120, 255 };
        s32 w = Str_Len(sV3Prompt) * 8 + 8;

        Draw2D_Rect((SCREEN_WIDTH - w) / 2, 174, w, 13, bg);
        Draw2D_TextCentered(177, fg, sV3Prompt);
    }
    sV3Gfx = NULL;
    return gfx;
}

static const char* V3_Name(s32 ev) {
    const V3Def* d = V3_Def(ev);

    return (d != NULL) ? d->name : "???";
}

static const char* V3_Counter(s32 ev) {
    const V3Def* d = V3_Def(ev);

    return (d != NULL) ? d->counter : "";
}

static s32 V3_Tier(s32 ev) {
    const V3Def* d = V3_Def(ev);

    return (d != NULL) ? d->tier : 1;
}

// ===========================================================================
// Pop Quiz: trivia and questions about where you are right now
// ===========================================================================

typedef struct {
    const char* q;
    const char* a[4]; // a[0] is always the right answer; shuffled at runtime
} V3Riddle;

static const V3Riddle sV3Riddles[] = {
    { "How many days until the moon falls?", { "3", "2", "4", "7" } },
    { "Who sells masks in Clock Town?", { "Mask Salesman", "Tingle", "Kafei", "Anju" } },
    { "Which song slows time down?", { "Inverted Time", "Soaring", "Oath", "Elegy" } },
    { "7 x 6 = ?", { "42", "36", "48", "56" } },
    { "Which mask lets you roll fast?", { "Goron", "Zora", "Deku", "Bunny" } },
    { "What does Tatl shout the most?", { "Hey!", "Listen!", "Watch out!", "Hi!" } },
    { "13 + 29 = ?", { "42", "41", "43", "32" } },
    { "Where is Romani Ranch?", { "Milk Road", "Ikana", "The Swamp", "Snowhead" } },
    { "What can a Zora do?", { "Swim fast", "Fly", "Roll", "Burrow" } },
    { "Kafei's fiancee is...", { "Anju", "Romani", "Cremia", "Lulu" } },
    { "100 - 37 = ?", { "63", "73", "67", "53" } },
    { "Skull Kid wears which mask?", { "Majora's", "Keaton", "Bremen", "Blast" } },
    { "9 x 9 = ?", { "81", "72", "91", "99" } },
    { "The Clock Tower is in...", { "South Clock Town", "North Clock Town", "East Clock Town", "Laundry Pool" } },
    { "A heart holds how many quarters?", { "4", "3", "5", "2" } },
    { "Four giants guard four...", { "Directions", "Seasons", "Oceans", "Moons" } },
    { "Deku Link can fly using...", { "Flowers", "Feathers", "Bubbles", "Wind" } },
    { "12 x 12 = ?", { "144", "124", "132", "164" } },
    { "Who runs the Stock Pot Inn?", { "Anju", "Cremia", "Madame Aroma", "Romani" } },
    { "Tingle sells what?", { "Maps", "Masks", "Bombs", "Milk" } },
    { "Who teaches the Song of Healing?", { "Mask Salesman", "Tatl", "Kaepora", "The Moon" } },
    { "Which boss guards Woodfall Temple?", { "Odolwa", "Goht", "Gyorg", "Twinmold" } },
    { "Which boss guards Snowhead Temple?", { "Goht", "Odolwa", "Gyorg", "Majora" } },
    { "Which boss guards Great Bay Temple?", { "Gyorg", "Goht", "Twinmold", "Odolwa" } },
    { "Which boss guards Stone Tower?", { "Twinmold", "Gyorg", "Odolwa", "Goht" } },
    { "Which song calls Epona?", { "Epona's Song", "Song of Storms", "Sonata", "Elegy" } },
    { "Who runs the Swordsman's School?", { "Swordsman", "Kafei", "Gorman", "Mutoh" } },
    { "What falls from the observatory?", { "Moon's Tear", "A star", "Rupees", "A mask" } },
    { "Who is the head carpenter?", { "Mutoh", "Gorman", "Toto", "Tingle" } },
    { "Where is Zora Hall?", { "Zora Cape", "Ikana", "The Swamp", "Milk Road" } },
    { "What sings in the Goron shrine?", { "The Goron baby", "A Deku", "A Zora", "Tatl" } },
    { "Which mask makes you tiny and wooden?", { "Deku", "Goron", "Zora", "Bunny" } },
    { "How many masks exist in total?", { "24", "20", "12", "30" } },
    { "How many heart pieces make a heart?", { "4", "2", "5", "3" } },
    { "What does the Postman fear most?", { "Being late", "Ghosts", "Dogs", "Moons" } },
    { "Which song opens the Great Bay Temple?", { "New Wave Bossa Nova", "Oath to Order", "Sonata", "Elegy" } },
    { "Which song opens Woodfall Temple?", { "Sonata of Awakening", "Goron Lullaby", "Elegy", "Oath" } },
    { "Which song calls the four giants?", { "Oath to Order", "Song of Time", "Soaring", "Healing" } },
    { "Which song makes empty shells?", { "Elegy of Emptiness", "Oath to Order", "Sonata", "Storms" } },
    { "The Song of Soaring warps you to...", { "Owl statues", "Fairy fountains", "Clock Town only", "Shops" } },
    { "Great Fairies were scattered into...", { "Stray Fairies", "Rupees", "Masks", "Hearts" } },
    { "Who stole Kafei's mask?", { "Sakon", "Skull Kid", "Tingle", "Gorman" } },
    { "Where do the Gorman brothers live?", { "Milk Road", "Ikana", "Clock Town", "Snowhead" } },
    { "What attacks Romani Ranch at night?", { "Aliens", "Wolves", "Keese", "Skull Kid" } },
    { "15 x 4 = ?", { "60", "45", "64", "54" } },
    { "81 / 9 = ?", { "9", "8", "7", "11" } },
    { "256 / 4 = ?", { "64", "54", "46", "62" } },
    { "17 + 26 = ?", { "43", "42", "33", "53" } },
    { "3 cubed = ?", { "27", "9", "18", "36" } },
    { "What comes after Tuesday?", { "Wednesday", "Thursday", "Monday", "Friday" } },
    { "A dozen plus a half-dozen = ?", { "18", "16", "24", "12" } },
    { "How many sides does a hexagon have?", { "6", "5", "7", "8" } },
    { "A blue rupee is worth...", { "5 rupees", "1 rupee", "20 rupees", "10 rupees" } },
    { "A red rupee is worth...", { "20 rupees", "5 rupees", "50 rupees", "10 rupees" } },
    { "Which dungeon is upside-down?", { "Stone Tower", "Woodfall", "Snowhead", "Great Bay" } },
    { "Who owns the Curiosity Shop?", { "Curiosity Shop man", "Tingle", "Anju", "Mutoh" } },
    { "The Bombers want you to find...", { "Hidden kids", "Bombs", "Masks", "Fairies" } },
    { "Which item lets you see the invisible?", { "Lens of Truth", "Mirror Shield", "Hookshot", "Bow" } },
    { "What does the Keaton mask summon?", { "Keaton quizzes", "Rain", "Horses", "Guards" } },
    { "Which Moon Mark item delays events?", { "Omen Scroll", "Lunar Tonic", "Stasis Orb", "Smoke Veil" } },
    { "Nemeses gain a stat when they...", { "Kill you", "Sleep", "Swim", "Eat bounties" } },
};
#define QZ_LEN 30

// Fills opts with the right answer first and three different wrong ones
// taken from a list of names. Returns false if there aren't enough.
static s32 V3_QuizFromList(char opts[4][30], const char* right, const char** pool, s32 poolCount) {
    s32 n = 1;
    s32 tries;

    Ev_Append(opts[0], 0, right, QZ_LEN);
    for (tries = 0; (tries < 80) && (n < 4); tries++) {
        const char* pick = pool[(s32)(Rand_ZeroOne() * poolCount) % poolCount];
        s32 k;
        s32 dup = false;

        for (k = 0; k < n; k++) {
            const char* a = opts[k];
            s32 m = 0;

            while ((a[m] != '\0') && (a[m] == pick[m])) {
                m++;
            }
            if ((a[m] == '\0') && (pick[m] == '\0')) {
                dup = true;
            }
        }
        if (!dup) {
            Ev_Append(opts[n++], 0, pick, QZ_LEN);
        }
    }
    return n == 4;
}

static const char* sV3MaskNames[] = {
    "No mask",        "Mask of Truth", "Kafei's Mask",   "All-Night Mask", "Bunny Hood",
    "Keaton Mask",    "Garo's Mask",   "Romani's Mask",  "Circus Leader",  "Postman's Hat",
    "Couple's Mask",  "Great Fairy",   "Gibdo Mask",     "Don Gero's Mask", "Kamaro's Mask",
    "Captain's Hat",  "Stone Mask",    "Bremen Mask",    "Blast Mask",     "Mask of Scents",
    "Giant's Mask",   "Fierce Deity",  "Goron Mask",     "Zora Mask",      "Deku Mask",
};

static s32 V3_QuizQuestion(PlayState* play, char* q, char opts[4][30]) {
    Player* player = GET_PLAYER(play);
    s32 kind;
    s32 tries;
    s32 i;

    for (i = 0; i < 4; i++) {
        opts[i][0] = '\0';
    }
    for (tries = 0; tries < 8; tries++) {
        kind = (s32)(Rand_ZeroOne() * 7) % 7;
        switch (kind) {
            case 0:
            case 1: { // trivia
                const V3Riddle* r = &sV3Riddles[(s32)(Rand_ZeroOne() * ARRAY_COUNT(sV3Riddles)) % ARRAY_COUNT(sV3Riddles)];

                Ev_Append(q, 0, r->q, 40);
                for (i = 0; i < 4; i++) {
                    Ev_Append(opts[i], 0, r->a[i], QZ_LEN);
                }
                return true;
            }
            case 2: { // which zone connects to yours
                s32 here = V2_ZoneNow(play);
                const char* pool[ZONE_COUNT];
                s32 n = 0;
                s32 right = -1;

                if (here < 0) {
                    break;
                }
                for (i = 0; i < ZONE_OUTDOOR; i++) {
                    if ((i != here) && V2_Adjacent(here, i)) {
                        right = ((right < 0) || (Rand_ZeroOne() < 0.5f)) ? i : right;
                    } else if (i != here) {
                        pool[n++] = sZoneNames[i];
                    }
                }
                if ((right < 0) || !V3_QuizFromList(opts, sZoneNames[right], pool, n)) {
                    break;
                }
                Ev_Append(q, 0, "Which area connects to this one?", 40);
                return true;
            }
            case 3: { // which of these is around right now
                static const char* sAll[] = { "Wolfos", "Stalchild", "Keese", "Chuchu", "Tektite", "Deku Baba",
                                              "Leever", "Guay", "Dodongo", "Peahat", "Bubble", "Cucco",
                                              "Dog", "Cow", "Carpenter", "Skulltula", "Octorok", "Mad Scrub" };
                const char* present = NULL;
                Actor* a = play->actorCtx.actorLists[ACTORCAT_ENEMY].first;
                const char* pool[ARRAY_COUNT(sAll)];
                s32 n = 0;

                while ((a != NULL) && (present == NULL)) {
                    if (a->update != NULL) {
                        s32 p = Dir_PoolOfActorId(a->id);

                        if (a->id == ACTOR_EN_ST) {
                            present = "Skulltula";
                        } else if (a->id == ACTOR_EN_OKUTA) {
                            present = "Octorok";
                        } else if ((p >= 0) && (p != 1) && (p != 2) && (p != 11)) {
                            present = (p == 0) ? "Keese" : (p == 10) ? "Bubble" : Pool_Name(p);
                        }
                    }
                    a = a->next;
                }
                for (a = play->actorCtx.actorLists[ACTORCAT_NPC].first; (a != NULL) && (present == NULL); a = a->next) {
                    if (a->id == ACTOR_EN_NIW) {
                        present = "Cucco";
                    } else if (a->id == ACTOR_EN_DG) {
                        present = "Dog";
                    } else if (a->id == ACTOR_EN_COW) {
                        present = "Cow";
                    } else if (a->id == ACTOR_EN_DAIKU) {
                        present = "Carpenter";
                    }
                }
                if (present == NULL) {
                    break;
                }
                for (i = 0; i < (s32)ARRAY_COUNT(sAll); i++) {
                    s32 found = false;

                    // Leave out anything that's also here.
                    for (a = play->actorCtx.actorLists[ACTORCAT_ENEMY].first; a != NULL; a = a->next) {
                        s32 p = Dir_PoolOfActorId(a->id);

                        if ((p >= 0) && (Pool_Name(p)[0] == sAll[i][0]) && (Pool_Name(p)[1] == sAll[i][1])) {
                            found = true;
                        }
                    }
                    if (!found && (sAll[i] != present)) {
                        pool[n++] = sAll[i];
                    }
                }
                if (!V3_QuizFromList(opts, present, pool, n)) {
                    break;
                }
                Ev_Append(q, 0, "Which of these is here right now?", 40);
                return true;
            }
            case 4: { // where's your bounty
                const char* pool[ZONE_COUNT];
                s32 n = 0;
                s32 b = -1;

                for (i = 0; i < V2_BOUNTY_SLOTS; i++) {
                    if ((sBounties[i].pool >= 0) && sBounties[i].accepted) {
                        b = i;
                    }
                }
                if (b < 0) {
                    break;
                }
                for (i = 0; i < ZONE_OUTDOOR; i++) {
                    if (i != sBounties[b].zone) {
                        pool[n++] = sZoneNames[i];
                    }
                }
                if (!V3_QuizFromList(opts, sZoneNames[sBounties[b].zone % ZONE_COUNT], pool, n)) {
                    break;
                }
                Ev_Append(q, 0, "Where is your bounty right now?", 40);
                return true;
            }
            case 5: { // your Nemesis
                const char* pool[ARRAY_COUNT(sHuntPool)];
                s32 n = 0;

                if (sNem.pool < 0) {
                    break;
                }
                for (i = 0; i < (s32)ARRAY_COUNT(sHuntPool); i++) {
                    if (sHuntPool[i] != sNem.pool) {
                        pool[n++] = Pool_Name(sHuntPool[i]);
                    }
                }
                if (!V3_QuizFromList(opts, Pool_Name(sNem.pool), pool, n)) {
                    break;
                }
                Ev_Append(q, 0, "What kind of beast is your Nemesis?", 40);
                return true;
            }
            case 6: { // mask
                s32 m = (player != NULL) ? player->currentMask : 0;

                if ((m < 0) || (m >= (s32)ARRAY_COUNT(sV3MaskNames))) {
                    break;
                }
                if (!V3_QuizFromList(opts, sV3MaskNames[m], sV3MaskNames, ARRAY_COUNT(sV3MaskNames))) {
                    break;
                }
                Ev_Append(q, 0, "Which mask are you wearing?", 40);
                return true;
            }
        }
    }
    return false;
}

#undef P
#undef SEC
