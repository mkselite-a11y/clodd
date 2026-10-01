#include "mf.h"

// ---------------------------------------------------------------------------
// The 32 Global Events. Every dangerous event has exactly one counter, and no
// two events share the same counter, so each one asks something different
// of the player. Events never spawn real bombs or fire: explosions and flames
// are visual only and the damage is computed here, so nothing in the world
// (bombable walls, webs, ice) can be broken open by an event.
// ---------------------------------------------------------------------------

typedef struct {
    Vec3f pos;
    Vec3f vel;
    s32 t;
    s16 state;
    s16 yaw;
    f32 r;
} Hz;

#define HZ_MAX 16
static Hz sHz[HZ_MAX];
static s32 sHzN = 0;

#define P(i) Events_Param(gEv.id, i)

static const MfColor cWarn = { 255, 60, 40, 140 };
static const MfColor cGood = { 90, 255, 150, 170 };
static const MfColor cMoon = { 190, 120, 255, 170 };
static const MfColor cWhite = { 255, 255, 255, 255 };
static const MfColor cText = { 230, 230, 255, 255 };

static Player* Pl(PlayState* play) {
    return GET_PLAYER(play);
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

// A moon strike. (The game's own lightning effect is an unused leftover that
// crashes, so this uses a hit flash, a shockwave and sparks instead.)
static void Lightning(PlayState* play, Vec3f* pos) {
    Vec3f p = *pos;
    p.y += 30.0f;
    EffectSsHitmark_SpawnFixedScale(play, 0, &p);
    Sparkle(play, &p, 200, 160, 255);
    Mf_Sfx(NA_SE_EV_LIGHTNING);
}

static s32 Pressed(u16 btn) {
    return (gRt.btnPress & btn) != 0;
}

static s32 Held(u16 btn) {
    return (gRt.btnCur & btn) != 0;
}

static s32 Every(s32 frames) {
    return frames > 0 && (gEv.timer % frames) == 0;
}

static void Bar2D(s32 y, s32 value, s32 max, MfColor c, const char* label) {
    MfColor bg = { 0, 0, 0, 150 };
    s32 w = 120 * MF_CLAMP(value, 0, max) / MF_MAX(1, max);
    Draw2D_Rect(100, y, 120, 8, bg);
    Draw2D_Rect(100, y, w, 8, c);
    Draw2D_Text(100 - (Str_Len(label) + 1) * 8, y, cText, label);
}

static void InitHz(void) {
    s32 i;
    sHzN = 0;
    for (i = 0; i < HZ_MAX; i++) {
        sHz[i].t = 0;
        sHz[i].state = 0;
    }
}

static s32 AlwaysEligible(PlayState* play) {
    return true;
}

// ===========================================================================
// HARMLESS
// ===========================================================================

// E0 Festival of Masks: a burst of carnival colour. Nothing to do but enjoy it.
static void Festival_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 density = MF_MAX(1, P(0));
    if (Every(MF_MAX(1, 12 - density))) {
        Vec3f pos = p->actor.world.pos;
        MfColor c = Mf_Rainbow(Rng_Range(0, 359), 255);
        pos.x += Rng_Range(-250, 250);
        pos.z += Rng_Range(-250, 250);
        pos.y += Rng_Range(80, 200);
        Sparkle(play, &pos, c.r, c.g, c.b);
    }
    if (P(2) && Every(SEC(4))) {
        Mf_Sfx(NA_SE_EV_FIVE_COUNT_LUPY);
    }
}

static void Festival_Draw2D(PlayState* play) {
    if (P(1)) {
        MfColor c = Mf_Rainbow(gRt.frame * 4, 40);
        Draw2D_Rect(0, 0, SCREEN_WIDTH, 3, c);
        Draw2D_Rect(0, SCREEN_HEIGHT - 3, SCREEN_WIDTH, 3, c);
    }
}

// E1 Moon's Whisper: the moon lets slip a few secrets. Nothing to fear.
static char sWhisper[3][48];

static void Whisper_Start(PlayState* play) {
    s32 i;
    gEv.duration = SEC(MF_MAX(6, P(0)));
    sWhisper[0][0] = sWhisper[1][0] = sWhisper[2][0] = '\0';
    Str_Cat(sWhisper[0], "Next omen: ", 48);
    Str_Cat(sWhisper[0], (gEv.omenId >= 0) ? gEvents[gEv.omenId].name : "unclear...", 48);
    if (gMf.nemesis.state != NEM_NONE) {
        Str_Cat(sWhisper[1], "Your nemesis lurks in ", 48);
        Str_Cat(sWhisper[1], gZones[gMf.nemesis.zone % gZoneCount].name, 48);
    } else {
        Str_Cat(sWhisper[1], "No nemesis hunts you. Yet.", 48);
    }
    for (i = 0; i < MAX_BOUNTIES; i++) {
        BountyRec* b = &gMf.bounties[(i + gRt.frame) % MAX_BOUNTIES];
        if (b->active && (b->claimed || i == MAX_BOUNTIES - 1)) {
            char name[40];
            Bounty_Name(name, sizeof(name), b);
            Str_Copy(sWhisper[2], name, 48);
            Str_Cat(sWhisper[2], " is in ", 48);
            Str_Cat(sWhisper[2], gZones[b->zone % gZoneCount].name, 48);
            break;
        }
    }
    for (i = 0; i < 3; i++) {
        if (sWhisper[i][0] != '\0') {
            World_Log(sWhisper[i]);
        }
    }
}

static void Whisper_Update(PlayState* play) {
    Player* p = Pl(play);
    if (Every(MF_MAX(1, 10 - P(1)))) {
        Vec3f pos = p->actor.world.pos;
        pos.x += Rng_Range(-200, 200);
        pos.z += Rng_Range(-200, 200);
        pos.y += 150.0f;
        Sparkle(play, &pos, 170, 170, 255);
    }
}

static void Whisper_Draw2D(PlayState* play) {
    MfColor bg = { 20, 10, 50, 170 };
    s32 i;
    Draw2D_Rect(24, 150, SCREEN_WIDTH - 48, 40, bg);
    for (i = 0; i < 3; i++) {
        Draw2D_TextCentered(154 + i * 12, (MfColor){ 210, 200, 255, 255 }, sWhisper[i]);
    }
}

// ===========================================================================
// BENEFICIAL
// ===========================================================================

// E2 Starfall Bounty: stand where the stars land to catch them.
static void Starfall_Start(PlayState* play) {
    InitHz();
}

static void Starfall_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    s32 interval = SEC(MF_MAX(1, 10 - P(0)) * 0.5f + 1.0f);
    s32 fall = SEC(P(1) / 10.0f);

    if (Every(interval) && sHzN < HZ_MAX) {
        Hz* h = &sHz[sHzN++];
        Mf_FindSpawnPoint(play, &h->pos, 120.0f, 380.0f, (s16)Rng_Range(0, 0xFFFF));
        h->t = fall;
        h->state = 1;
        Mf_Sfx(NA_SE_SY_KINSTA_MARK_APPEAR);
    }
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
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

static void Starfall_Draw3D(PlayState* play) {
    s32 i;
    s32 fall = MF_MAX(1, SEC(P(1) / 10.0f));
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
        if (h->state == 1) {
            Vec3f star = h->pos;
            MfColor gold = { 255, 230, 120, 220 };
            star.y += 700.0f * h->t / fall + 20.0f;
            Draw3D_Ring(&h->pos, 60.0f, 10.0f, cGood);
            Draw3D_Orb(&star, 35.0f, gold);
        }
    }
}

static void Starfall_Draw2D(PlayState* play) {
    char buf[24];
    buf[0] = '\0';
    Str_Cat(buf, "Stars caught: ", sizeof(buf));
    Str_CatInt(buf, gEv.score, sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

static void Starfall_End(PlayState* play, s32 success) {
    if (success && gEv.score > 0) {
        Marks_Add(Marks_Scale(gEv.score * MF_MAX(1, P(2))), "stars caught");
    }
}

// E3 Dowsing Moon: follow the warmth, then press A to dig up the cache.
static void Dowsing_Start(PlayState* play) {
    Player* p = Pl(play);
    f32 d = P(0) * 10.0f;
    InitHz();
    Mf_FindSpawnPoint(play, &sHz[0].pos, d * 0.6f, d, (s16)Rng_Range(0, 0xFFFF));
    gEv.fa = Mf_DistXZ(&sHz[0].pos, &p->actor.world.pos);
}

static void Dowsing_Update(PlayState* play) {
    Player* p = Pl(play);
    f32 d = Mf_DistXZ(&sHz[0].pos, &p->actor.world.pos);
    s32 beat = (s32)MF_CLAMP(d / 25.0f, 3, 40);

    gEv.fb = d;
    if (Every(beat)) {
        Mf_Sfx(NA_SE_SY_WARNING_COUNT_N);
        if (d < 200.0f) {
            Rumble_Request(0.0f, 120, 4, 60);
        }
    }
    if (d < 55.0f) {
        Hud_SetPrompt("Press A to dig!");
        if (Pressed(BTN_A)) {
            s32 bonus = MF_MAX(0, (gEv.duration - gEv.timer) / FPS);
            gEv.score = P(1) + bonus / 3;
            Sparkle(play, &sHz[0].pos, 255, 220, 80);
            Mf_Sfx(NA_SE_SY_GET_BOXITEM);
            Mf_HealPlayer(4);
            gEv.succeeded = true;
        }
    }
}

static void Dowsing_Draw2D(PlayState* play) {
    s32 warmth = (s32)MF_CLAMP(100.0f - gEv.fb / 9.0f, 0, 100);
    MfColor c = { (u8)(80 + warmth * 17 / 10), 80, (u8)(255 - warmth * 2), 230 };
    Bar2D(36, warmth, 100, c, "Warmth");
}

static void Dowsing_End(PlayState* play, s32 success) {
    if (gEv.score > 0) {
        Marks_Add(Marks_Scale(gEv.score), "buried cache");
    } else {
        MfColor c = { 200, 200, 200, 255 };
        Hud_Notify("The cache sinks back into the earth.", c);
    }
}

// ===========================================================================
// DANGEROUS
// ===========================================================================

// E4 Moonfall: telegraphed impacts. Counter: step out of the marked circles.
static void Moonfall_Start(PlayState* play) {
    InitHz();
}

static void Moonfall_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 interval = MF_MAX(4, SEC(10) / MF_MAX(1, P(0)));
    s32 warn = MF_MAX(6, SEC(P(2) / 10.0f));
    s32 i;

    if (Every(interval)) {
        for (i = 0; i < HZ_MAX; i++) {
            Hz* h = &sHz[i];
            if (h->state == 0) {
                // Aim where the player is heading.
                h->pos = p->actor.world.pos;
                h->pos.x += p->actor.velocity.x * 15.0f + Rng_Range(-60, 60);
                h->pos.z += p->actor.velocity.z * 15.0f + Rng_Range(-60, 60);
                h->pos.y = Mf_FloorY(&h->pos, p->actor.world.pos.y);
                h->t = warn;
                h->r = 85.0f;
                h->state = 1;
                sHzN = MF_MAX(sHzN, i + 1);
                break;
            }
        }
    }
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
        if (h->state != 1) {
            continue;
        }
        if (--h->t <= 0) {
            f32 d = Mf_DistXZ(&h->pos, &p->actor.world.pos);
            h->state = 0;
            Boom(play, &h->pos);
            if (d < h->r && fabsf(h->pos.y - p->actor.world.pos.y) < 100.0f) {
                Mf_DamagePlayer(P(1), true, Mf_YawTo(&h->pos, &p->actor.world.pos));
                gEv.failed = true;
            }
        }
    }
}

static void Moonfall_Draw3D(PlayState* play) {
    s32 i;
    s32 warn = MF_MAX(6, SEC(P(2) / 10.0f));
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
        if (h->state == 1) {
            MfColor c = cWarn;
            Vec3f rock = h->pos;
            c.a = (u8)(90 + 150 * (warn - h->t) / warn);
            Draw3D_Disk(&h->pos, h->r * (1.0f - 0.3f * h->t / warn), c);
            Draw3D_Ring(&h->pos, h->r, 8.0f, c);
            rock.y += 900.0f * h->t / warn + 30.0f;
            Draw3D_Orb(&rock, 45.0f, (MfColor){ 255, 140, 60, 230 });
        }
    }
}

// E5 Giant's Gale: wind shoves you. Counter: push the stick into the wind.
static void Gale_Start(PlayState* play) {
    gEv.hits = Rng_Range(0, 0xFFFF);
}

static void Gale_Update(PlayState* play) {
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

static void Gale_Draw2D(PlayState* play) {
    s16 rel = (s16)gEv.hits - Camera_GetCamDirYaw(GET_ACTIVE_CAM(play));
    const char* dir = (ABS(rel) < 0x2000) ? "Wind: blowing AWAY from you"
                      : (ABS(rel) > 0x6000) ? "Wind: blowing IN your face"
                      : (rel > 0) ? "Wind: blowing to the LEFT" : "Wind: blowing to the RIGHT";
    Draw2D_TextCentered(34, gEv.score ? (MfColor){ 120, 255, 140, 255 } : (MfColor){ 255, 160, 120, 255 }, dir);
}

// E6 Eye of the Moon: when the eye opens, freeze.
static void Eye_Start(PlayState* play) {
    gEv.hits = SEC(3); // countdown to next phase
    gEv.score = 0;     // 0 closed, 1 opening, 2 open
}

static void Eye_Update(PlayState* play) {
    Player* p = Pl(play);
    if (--gEv.hits <= 0) {
        gEv.score = (gEv.score + 1) % 3;
        if (gEv.score == 0) {
            gEv.hits = SEC(Rng_Range(25, 45) / 10.0f);
        } else if (gEv.score == 1) {
            gEv.hits = SEC(P(0) / 10.0f);
            Mf_Sfx(NA_SE_SY_WARNING_COUNT_E);
        } else {
            gEv.hits = SEC(P(1) / 10.0f);
            Mf_Sfx(NA_SE_EN_STALKIDS_APPEAR);
        }
    }
    if (gEv.score == 2 && (Mf_StickMag() > 12.0f || p->speedXZ > 1.0f || Pressed(BTN_A | BTN_B))) {
        if (Every(8)) {
            Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
            Lightning(play, &p->actor.world.pos);
            gEv.failed = true;
        }
    }
}

static void Eye_Draw2D(PlayState* play) {
    s32 cx = SCREEN_WIDTH / 2;
    s32 cy = 60;
    MfColor white = { 255, 240, 220, 230 };
    MfColor red = { 220, 20, 20, 255 };
    MfColor lid = { 40, 0, 60, 230 };
    s32 open = (gEv.score == 0) ? 0 : (gEv.score == 1) ? 6 : 16;
    Draw2D_Rect(cx - 40, cy - 2, 80, 4, lid);
    if (open > 0) {
        Draw2D_Rect(cx - 36, cy - open / 2, 72, open, white);
        Draw2D_Rect(cx - 7, cy - open / 2, 14, open, red);
    }
    if (gEv.score == 1) {
        Draw2D_TextCentered(cy + 14, (MfColor){ 255, 200, 80, 255 }, "It's opening... get ready to freeze!");
    } else if (gEv.score == 2) {
        Draw2D_TextCentered(cy + 14, (MfColor){ 255, 60, 60, 255 }, "DON'T MOVE");
    }
}

// E7 Sinking Sands: if you stand still, you sink.
static void Sinking_Update(PlayState* p0) {
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

static void Sinking_Draw3D(PlayState* play) {
    Player* p = Pl(play);
    MfColor sand = { 170, 130, 70, (u8)MF_MIN(200, 60 + gEv.hits * 4) };
    Draw3D_Disk(&p->actor.world.pos, 70.0f, sand);
}

// E8 Frostbite: the cold builds. Counter: roll to shake it off.
static void Frost_Update(PlayState* play) {
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

static void Frost_Draw2D(PlayState* play) {
    MfColor edge = { 150, 210, 255, (u8)MF_MIN(160, gEv.hits * 3 / 2) };
    Draw2D_Rect(0, 0, 6, SCREEN_HEIGHT, edge);
    Draw2D_Rect(SCREEN_WIDTH - 6, 0, 6, SCREEN_HEIGHT, edge);
    Bar2D(36, gEv.hits, 100, (MfColor){ 150, 210, 255, 230 }, "Frost");
}

// E9 Scythe Sweep: expanding shockwaves. Counter: jump them (or stand in the gap).
static void Scythe_Start(PlayState* play) {
    InitHz();
    sHz[0].state = 0;
}

static void Scythe_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sHz[0];

    if (h->state == 0 && Every(SEC(MF_MAX(2, P(0))))) {
        Mf_FindSpawnPoint(play, &h->pos, 150.0f, 280.0f, (s16)Rng_Range(0, 0xFFFF));
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

static void Scythe_Draw3D(PlayState* play) {
    Hz* h = &sHz[0];
    if (h->state == 1) {
        MfColor gap = { 40, 255, 120, 90 };
        Draw3D_Ring(&h->pos, h->r, 18.0f, (MfColor){ 255, 40, 200, 200 });
        Draw3D_Wedge(&h->pos, h->r, h->yaw, 0x1555, gap);
    }
}

// E10 Moth Swarm: moon moths cling to you. Counter: a spin attack scatters them.
static void Moths_Start(PlayState* play) {
    InitHz();
}

static s32 IsSpinning(Player* p) {
    if (p->meleeWeaponState == 0) {
        return false;
    }
    if (p->meleeWeaponAnimation >= PLAYER_MWA_SPIN_ATTACK_1H && p->meleeWeaponAnimation <= PLAYER_MWA_BIG_SPIN_2H) {
        return true;
    }
    // Non-human forms have no spin attack: their strongest swing counts.
    return (p->transformation != PLAYER_FORM_HUMAN) && (p->transformation != PLAYER_FORM_FIERCE_DEITY);
}

static void Moths_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    s32 attached = 0;

    if (Every(SEC(MF_MAX(1, 8 - P(0)) * 0.5f)) && sHzN < HZ_MAX) {
        Hz* h = &sHz[sHzN++];
        Mf_FindSpawnPoint(play, &h->pos, 200.0f, 350.0f, (s16)Rng_Range(0, 0xFFFF));
        h->pos.y += 60.0f;
        h->state = 1;
        h->yaw = (s16)Rng_Range(0, 0xFFFF);
    }
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
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
    if (IsSpinning(p)) {
        s32 cleared = 0;
        for (i = 0; i < sHzN; i++) {
            if (sHz[i].state != 0 && Mf_DistXZ(&sHz[i].pos, &p->actor.world.pos) < 120.0f) {
                if (p->transformation != PLAYER_FORM_HUMAN && cleared >= 1) {
                    break;
                }
                sHz[i].state = 0;
                Sparkle(play, &sHz[i].pos, 200, 200, 255);
                cleared++;
            }
        }
    }
    if (attached > 0 && Every(SEC(MF_MAX(1, P(1)) / 2.0f))) {
        Mf_DamagePlayer(MF_MAX(1, attached / 2), false, 0);
        gEv.failed = true;
    }
}

static void Moths_Draw3D(PlayState* play) {
    s32 i;
    for (i = 0; i < sHzN; i++) {
        if (sHz[i].state != 0) {
            MfColor c = (sHz[i].state == 2) ? (MfColor){ 255, 120, 255, 230 } : (MfColor){ 200, 200, 255, 200 };
            Draw3D_Orb(&sHz[i].pos, 12.0f, c);
        }
    }
}

static void Moths_Draw2D(PlayState* play) {
    char buf[32];
    buf[0] = '\0';
    Str_Cat(buf, "Moths clinging: ", sizeof(buf));
    Str_CatInt(buf, gEv.hits, sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

// E11 Tatl's Warning: Tatl shouts. Counter: heed her with C-Up in time.
static void Tatl_Start(PlayState* play) {
    gEv.hits = SEC(Rng_Range(3, 5));
    gEv.score = 0; // 1 = warning active, 2 = heeded
}

static void Tatl_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 window = SEC(P(0) / 10.0f);

    if (gEv.score == 2 && Pressed(BTN_CUP)) {
        // already heeded; ignore extra presses
    } else if (gEv.score == 1 && Pressed(BTN_CUP)) {
        gEv.score = 2;
        Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
    }
    if (--gEv.hits == window) {
        gEv.score = 1;
        Mf_Sfx(NA_SE_VO_NA_HELLO_0);
    }
    if (gEv.hits <= 0) {
        if (gEv.score == 2) {
            Vec3f miss = p->actor.world.pos;
            miss.x += Rng_Range(-120, 120);
            miss.z += Rng_Range(-120, 120);
            Lightning(play, &miss);
        } else {
            Lightning(play, &p->actor.world.pos);
            Mf_DamagePlayer(MF_MAX(1, P(1)), true, p->actor.shape.rot.y + 0x8000);
            gEv.failed = true;
        }
        gEv.score = 0;
        gEv.hits = window + SEC(Rng_Range(20, 45) / 10.0f);
    }
}

static void Tatl_Draw2D(PlayState* play) {
    if (gEv.score == 1 && (gRt.frame & 4)) {
        MfColor c = { 120, 220, 255, 255 };
        MfColor bg = { 0, 60, 120, 160 };
        Draw2D_Rect(96, 96, 128, 24, bg);
        Draw2D_TextCentered(99, c, "TATL: HEY! LOOK OUT!");
        Draw2D_TextCentered(110, c, "Press C-Up!");
    }
}

// E12 Hex of Silence: every B or C press hurts. Counter: don't attack or use items.
static void Silence_Update(PlayState* play) {
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

static void Silence_Draw2D(PlayState* play) {
    MfColor c = { 150, 60, 200, (u8)(50 + (gRt.frame % 20) * 3) };
    Draw2D_Rect(0, 0, SCREEN_WIDTH, 2, c);
    Draw2D_Rect(0, SCREEN_HEIGHT - 2, SCREEN_WIDTH, 2, c);
}

// E13 Moon Crank: a ward keeps the moon at bay only while it's wound up.
// Counter: spin the control stick in circles to wind it.
static void Crank_Start(PlayState* play) {
    gEv.hits = 100; // ward charge
    gEv.fa = 0.0f;  // accumulated rotation
    gEv.score = -1; // last stick angle (or -1)
}

static void Crank_Update(PlayState* play) {
    if (Mf_StickMag() > 45.0f) {
        s16 ang = Math_Atan2S_XY(gRt.stickY, gRt.stickX);
        if (gEv.score != -1) {
            s16 d = ang - (s16)gEv.score;
            gEv.fa += ABS(d);
        }
        gEv.score = (u16)ang;
    } else {
        gEv.score = -1;
    }
    while (gEv.fa >= 65536.0f) {
        gEv.fa -= 65536.0f;
        gEv.hits = MF_MIN(100, gEv.hits + MF_MAX(1, P(1)));
        Mf_Sfx(NA_SE_SY_CURSOR);
    }
    if (Every(SEC(1))) {
        gEv.hits = MF_MAX(0, gEv.hits - MF_MAX(1, P(0)));
    }
    if (gEv.hits == 0 && Every(SEC(1.5f))) {
        Player* p = Pl(play);
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        Lightning(play, &p->actor.world.pos);
        gEv.failed = true;
    }
}

static void Crank_Draw2D(PlayState* play) {
    MfColor c = (gEv.hits > 30) ? (MfColor){ 150, 220, 255, 230 } : (MfColor){ 255, 110, 110, 230 };
    Bar2D(36, gEv.hits, 100, c, "Ward");
    if (gEv.hits < 30) {
        Draw2D_TextCentered(48, (MfColor){ 255, 200, 120, 255 }, "Spin the stick in circles!");
    }
}

// E14 Moon Tether: stay inside the drifting circle.
static void Tether_Start(PlayState* play) {
    InitHz();
    Mf_FindSpawnPoint(play, &sHz[0].pos, 150.0f, 260.0f, (s16)Rng_Range(0, 0xFFFF));
    sHz[0].yaw = (s16)Rng_Range(0, 0xFFFF);
}

static void Tether_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sHz[0];
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

static void Tether_Draw3D(PlayState* play) {
    Hz* h = &sHz[0];
    Player* p = Pl(play);
    s32 inside = Mf_DistXZ(&h->pos, &p->actor.world.pos) <= P(0) * 10.0f;
    Draw3D_Ring(&h->pos, P(0) * 10.0f, 14.0f, inside ? cGood : cWarn);
    Draw3D_Pillar(&h->pos, 8.0f, 200.0f, cMoon);
}

static void Tether_Draw2D(PlayState* play) {
    if (gEv.timer < SEC(5)) {
        Draw2D_TextCentered(34, cText, "Get inside the circle!");
    }
}

// E15 Moon's Grasp: a hand grabs you. Counter: mash A to break free.
static void Grasp_Start(PlayState* play) {
    gEv.hits = SEC(3);  // countdown
    gEv.score = 0;      // 0 idle, 1 shadow, 2 grabbed
}

static void Grasp_Update(PlayState* play) {
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

static void Grasp_Draw3D(PlayState* play) {
    Player* p = Pl(play);
    if (gEv.score >= 1) {
        MfColor shadow = { 30, 0, 50, 200 };
        Draw3D_Disk(&p->actor.world.pos, gEv.score == 1 ? 90.0f - gEv.hits * 2.0f : 60.0f, shadow);
    }
}

static void Grasp_Draw2D(PlayState* play) {
    if (gEv.score == 2) {
        Bar2D(100, (s32)gEv.fa, P(0), (MfColor){ 255, 220, 90, 240 }, "MASH A");
    } else if (gEv.score == 1) {
        Draw2D_TextCentered(100, (MfColor){ 255, 120, 120, 255 }, "Something reaches up from below...");
    }
}

// E16 Heartbeat Hex: press A exactly on the beat.
static void Heartbeat_Start(PlayState* play) {
    gEv.hits = 0;  // frames into current beat
    gEv.score = 0; // pressed during this beat
}

static void Heartbeat_Update(PlayState* play) {
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

static void Heartbeat_Draw2D(PlayState* play) {
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

// E17 Rising Moon-Tide: dark water rises. Counter: climb above it.
static void Tide_Level(f32* out) {
    f32 rise = P(0) * 10.0f;
    f32 t = MF_MIN(1.0f, gEv.timer / (gEv.duration * 0.6f));
    *out = gEv.origin.y - 30.0f + (rise + 30.0f) * t;
}

static void Tide_Update(PlayState* play) {
    Player* p = Pl(play);
    f32 level;
    Tide_Level(&level);
    if (gEv.timer > gEv.duration * 0.6f && p->actor.world.pos.y < level + 5.0f && Every(SEC(1))) {
        Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
        gEv.failed = true;
    }
}

static void Tide_Draw3D(PlayState* play) {
    Player* p = Pl(play);
    Vec3f pos = p->actor.world.pos;
    MfColor water = { 40, 20, 90, 150 };
    Tide_Level(&pos.y);
    Draw3D_Disk(&pos, 1400.0f, water);
}

static void Tide_Draw2D(PlayState* play) {
    Player* p = Pl(play);
    f32 level;
    char buf[32];
    s32 above;
    Tide_Level(&level);
    above = (s32)((p->actor.world.pos.y - level) / 10.0f);
    buf[0] = '\0';
    Str_Cat(buf, above >= 0 ? "Above the tide: " : "Below the tide: ", sizeof(buf));
    Str_CatInt(buf, above >= 0 ? above : -above, sizeof(buf));
    Str_Cat(buf, "m", sizeof(buf));
    Draw2D_TextCentered(34, above >= 0 ? (MfColor){ 120, 255, 140, 255 } : (MfColor){ 255, 120, 120, 255 }, buf);
}

// E18 Moon's Reflex Test: a button flashes up. Counter: press that exact button, fast.
static const u16 sReflexBtns[4] = { BTN_A, BTN_B, BTN_R, BTN_Z };
static const char* sReflexNames[4] = { "A", "B", "R", "Z" };

static void Reflex_Start(PlayState* play) {
    gEv.hits = SEC(2);  // countdown to the next prompt
    gEv.score = -1;     // active prompt index, -1 = none
}

static void Reflex_Update(PlayState* play) {
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

static void Reflex_Draw2D(PlayState* play) {
    if (gEv.score >= 0) {
        MfColor bg = { 60, 0, 30, 220 };
        char buf[16];
        buf[0] = '\0';
        Str_Cat(buf, "PRESS ", sizeof(buf));
        Str_Cat(buf, sReflexNames[gEv.score], sizeof(buf));
        Draw2D_Rect(110, 96, 100, 22, bg);
        Draw2D_TextCentered(103, Mf_Rainbow(gRt.frame * 20, 255), buf);
    }
}

// E19 Gorgon's Gaze: an eye hangs in the air. Counter: keep your camera off it.
static void Gorgon_Place(PlayState* play) {
    Player* p = Pl(play);
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    sHz[0].pos = p->actor.world.pos;
    sHz[0].pos.x += Math_SinS(yaw) * 500.0f;
    sHz[0].pos.z += Math_CosS(yaw) * 500.0f;
    sHz[0].pos.y += 160.0f;
}

static void Gorgon_Start(PlayState* play) {
    InitHz();
    Gorgon_Place(play);
}

static void Gorgon_Update(PlayState* play) {
    Camera* cam = GET_ACTIVE_CAM(play);
    Vec3f look;
    Vec3f toEye;
    f32 ll;
    f32 le;
    f32 dot;

    if (Every(SEC(MF_MAX(2, P(0))))) {
        Gorgon_Place(play);
        Mf_Sfx(NA_SE_EN_WIZ_LAUGH);
    }
    look.x = cam->at.x - cam->eye.x;
    look.y = cam->at.y - cam->eye.y;
    look.z = cam->at.z - cam->eye.z;
    toEye.x = sHz[0].pos.x - cam->eye.x;
    toEye.y = sHz[0].pos.y - cam->eye.y;
    toEye.z = sHz[0].pos.z - cam->eye.z;
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

static void Gorgon_Draw3D(PlayState* play) {
    Vec3f base = sHz[0].pos;
    base.y -= 160.0f;
    Draw3D_Orb(&sHz[0].pos, 120.0f, (MfColor){ 255, 245, 230, 255 });
    Draw3D_Orb(&sHz[0].pos, 50.0f, (MfColor){ 220, 0, 30, 255 });
    Draw3D_Pillar(&base, 10.0f, 160.0f, (MfColor){ 200, 0, 40, 200 });
}

static void Gorgon_Draw2D(PlayState* play) {
    Draw2D_WorldMarker(play, &sHz[0].pos, (MfColor){ 255, 60, 60, 255 }, "(O)");
    Bar2D(36, gEv.hits, 100, (MfColor){ 170, 170, 170, 230 }, "Stone");
}

// E20 Ambush: local enemies pour in. Counter: fight and kill them all.
static s32 Ambush_PickSpecies(PlayState* play) {
    s32 options[16];
    s32 n = 0;
    s32 i;
    for (i = 0; i < gSpeciesCount && n < 16; i++) {
        if ((gSpecies[i].flags & SPF_ROAM) && gSpecies[i].difficulty <= 6 && Actors_SpeciesObjectReady(play, i)) {
            options[n++] = i;
        }
    }
    return (n > 0) ? options[Rng_Range(0, n - 1)] : -1;
}

static s32 Ambush_Eligible(PlayState* play) {
    return Ambush_PickSpecies(play) >= 0;
}

static void Ambush_Start(PlayState* play) {
    Player* p = Pl(play);
    s32 species = Ambush_PickSpecies(play);
    s32 count = MF_CLAMP(P(0), 1, 6);
    s32 i;
    gEv.score = species;
    gEv.hits = 0;
    for (i = 0; i < count && species >= 0; i++) {
        Vec3f pos;
        Mf_FindSpawnPoint(play, &pos, 220.0f, 380.0f, p->actor.shape.rot.y + (s16)(0x10000 * i / count));
        if (Actors_SpawnSpecies(play, species, &pos, Mf_YawTo(&pos, &p->actor.world.pos), TAG_EVENT, 0) != NULL) {
            EffectSsDeadDb_Spawn(play, &pos, &gZeroVec3f, &gZeroVec3f, &(Color_RGBA8){ 90, 0, 140, 255 },
                                 &(Color_RGBA8){ 20, 0, 40, 255 }, 120, 0, 12);
            gEv.hits++;
        }
    }
    if (gEv.hits == 0) {
        MfColor c = { 200, 200, 200, 255 };
        Hud_Notify("No monsters answered the moon's call.", c);
        gEv.duration = 1;
    }
}

static void Ambush_Update(PlayState* play) {
    if (gEv.timer > SEC(2) && Actors_CountTagged(TAG_EVENT, -1) == 0) {
        gEv.succeeded = true;
    }
}

static void Ambush_End(PlayState* play, s32 success) {
    Actor* survivor = Actors_FindTagged(TAG_EVENT, -1);
    if (survivor != NULL) {
        gEv.failed = true;
        if (gMf.nemesis.state == NEM_NONE && gMf.settings.nemesisEnabled && gEv.score >= 0 &&
            Rng_Chance(gMf.settings.promotionChance)) {
            ActorTag* tag = Tag_Get(survivor);
            if (tag != NULL) {
                tag->kind = TAG_NONE;
            }
            Nemesis_Promote(play, gEv.score, 5, survivor);
        }
    } else if (gEv.succeeded) {
        Marks_Add(Marks_Scale(gEv.hits * 2), "ambush crushed");
    }
    Actors_KillTagged(TAG_EVENT);
}

static void Ambush_Draw2D(PlayState* play) {
    char buf[32];
    buf[0] = '\0';
    Str_Cat(buf, "Ambushers left: ", sizeof(buf));
    Str_CatInt(buf, Actors_CountTagged(TAG_EVENT, -1), sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

// E21 Dissonance: a sour melody saps your heart. Counter: answer with 3 ocarina notes.
static s32 Dissonance_Eligible(PlayState* play) {
    return INV_CONTENT(ITEM_OCARINA_OF_TIME) == ITEM_OCARINA_OF_TIME;
}

static void Dissonance_Update(PlayState* play) {
    if (Every(SEC(MF_MAX(1, P(0))))) {
        Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
        Mf_Sfx(NA_SE_SY_OCARINA_ERROR);
        gEv.failed = true;
    }
}

static void Dissonance_Draw2D(PlayState* play) {
    s32 i;
    for (i = 0; i < 6; i++) {
        s32 y = 60 + i * 20 + (s32)(Math_SinS(gRt.frame * 1500 + i * 9000) * 6.0f);
        MfColor c = { 120, 60, 160, 90 };
        Draw2D_Rect(0, y, SCREEN_WIDTH, 1, c);
    }
    {
        char buf[32];
        buf[0] = '\0';
        Str_Cat(buf, "Notes played: ", sizeof(buf));
        Str_CatInt(buf, MF_CLAMP(gOcarinaNotesPlayed - gEv.notesAtStart, 0, 3), sizeof(buf));
        Str_Cat(buf, "/3", sizeof(buf));
        Draw2D_TextCentered(34, cText, buf);
    }
}

// E22 Mirror Madness: controls invert. Counter: face the mirror shard and hold Z to shatter it.
static void Mirror_Start(PlayState* play) {
    Player* p = Pl(play);
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    InitHz();
    sHz[0].pos = p->actor.world.pos;
    sHz[0].pos.x += Math_SinS(yaw) * 300.0f;
    sHz[0].pos.z += Math_CosS(yaw) * 300.0f;
    sHz[0].pos.y += 110.0f;
    gEv.hits = 0;
}

static void Mirror_Update(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    Player* p = Pl(play);
    s16 diff = Mf_YawTo(&p->actor.world.pos, &sHz[0].pos) - p->actor.shape.rot.y;

    in->rel.stick_x = -in->rel.stick_x;
    in->rel.stick_y = -in->rel.stick_y;
    in->cur.stick_x = -in->cur.stick_x;
    in->cur.stick_y = -in->cur.stick_y;

    if (Held(BTN_Z) && ABS(diff) < 0x1200) {
        gEv.hits++;
        if (gEv.hits >= SEC(P(0) / 10.0f)) {
            EffectSsIcePiece_SpawnBurst(play, &sHz[0].pos, 1.2f);
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

static void Mirror_Draw3D(PlayState* play) {
    MfColor glass = { 200, 240, 255, (u8)(150 + (gRt.frame % 10) * 8) };
    Draw3D_Orb(&sHz[0].pos, 45.0f, glass);
    Draw3D_Pillar(&(Vec3f){ sHz[0].pos.x, sHz[0].pos.y - 110.0f, sHz[0].pos.z }, 4.0f, 110.0f, glass);
}

static void Mirror_Draw2D(PlayState* play) {
    Bar2D(36, gEv.hits, MF_MAX(1, SEC(P(0) / 10.0f)), (MfColor){ 200, 240, 255, 230 }, "Shatter");
}

// E23 Volley of Stars: star volleys from a marked direction. Counter: face them and guard (hold R).
static void Volley_Start(PlayState* play) {
    InitHz();
    gEv.hits = SEC(2);
    gEv.score = 0;
}

static void Volley_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sHz[0];

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

static void Volley_Draw3D(PlayState* play) {
    Hz* h = &sHz[0];
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

// E24 Lonely Moon: loneliness gnaws at you. Counter: go talk to someone.
static s32 Lonely_Eligible(PlayState* play) {
    Actor* a = play->actorCtx.actorLists[ACTORCAT_NPC].first;
    Player* p = Pl(play);
    while (a != NULL) {
        if ((a->flags & ACTOR_FLAG_TALK) || (a->flags & ACTOR_FLAG_ATTENTION_ENABLED)) {
            if (Actor_WorldDistXZToActor(a, &p->actor) < 1500.0f) {
                return true;
            }
        }
        a = a->next;
    }
    return false;
}

static void Lonely_Update(PlayState* play) {
    if (Every(SEC(MF_MAX(1, P(0))))) {
        Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
        gEv.failed = true;
    }
}

// E25 Sleeping Giant: running wakes the giant. Counter: walk softly.
static void Giant_Update(PlayState* play) {
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

static void Giant_Draw2D(PlayState* play) {
    Bar2D(36, gEv.hits, 100, (MfColor){ 255, 170, 60, 230 }, "Noise");
}

// E26 Tag, You're It: a wisp tagged you. Counter: chase it down and touch it.
static void Tag_Start(PlayState* play) {
    InitHz();
    Mf_FindSpawnPoint(play, &sHz[0].pos, 220.0f, 300.0f, (s16)Rng_Range(0, 0xFFFF));
    sHz[0].pos.y += 30.0f;
}

static void Tag_Update(PlayState* play) {
    Player* p = Pl(play);
    Hz* h = &sHz[0];
    f32 d = Mf_DistXZ(&h->pos, &p->actor.world.pos);
    f32 speed = P(0) / 10.0f;
    s16 away = Mf_YawTo(&p->actor.world.pos, &h->pos) + (s16)Rng_Range(-0x1800, 0x1800);
    Vec3f next;

    if (d < 45.0f && fabsf(h->pos.y - (p->actor.world.pos.y + 30.0f)) < 90.0f) {
        Sparkle(play, &h->pos, 180, 255, 200);
        Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
        gEv.succeeded = true;
        return;
    }
    if (d > 600.0f) {
        speed *= 0.3f; // it waits if you fall far behind
    }
    next = h->pos;
    next.x += Math_SinS(away) * speed;
    next.z += Math_CosS(away) * speed;
    next.y = Mf_FloorY(&(Vec3f){ next.x, h->pos.y + 60.0f, next.z }, BGCHECK_Y_MIN);
    if (next.y > BGCHECK_Y_MIN + 1.0f && fabsf(next.y + 30.0f - h->pos.y) < 70.0f) {
        next.y += 30.0f;
        h->pos = next;
    } else {
        // Cornered: slip sideways.
        h->pos.x += Math_SinS(away + 0x4000) * speed;
        h->pos.z += Math_CosS(away + 0x4000) * speed;
    }
}

static void Tag_Draw3D(PlayState* play) {
    Draw3D_Orb(&sHz[0].pos, 22.0f, (MfColor){ 150, 255, 200, 240 });
}

static void Tag_End(PlayState* play, s32 success) {
    if (!gEv.succeeded && success && !gRt.playerDead) {
        Mf_DamagePlayer(MF_MAX(1, P(1)), true, 0);
        gEv.failed = true;
        {
            MfColor c = { 255, 150, 150, 255 };
            Hud_Notify("The wisp tags you back!", c);
        }
    }
}

// E27 Moon's Riddle: answer with the D-pad.
typedef struct {
    const char* q;
    const char* a[4]; // a[0] is always the right answer; shuffled at runtime
} Riddle;

static const Riddle sRiddles[] = {
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
    { "What buys Moon Pouch items?", { "Moon Marks", "Rupees", "Hearts", "Masks" } },
    { "Skull Kid wears which mask?", { "Majora's", "Keaton", "Bremen", "Blast" } },
    { "9 x 9 = ?", { "81", "72", "91", "99" } },
    { "The Clock Tower is in...", { "South Clock Town", "North Clock Town", "East Clock Town", "Laundry Pool" } },
    { "A heart holds how many quarters?", { "4", "3", "5", "2" } },
    { "Four giants guard four...", { "Directions", "Seasons", "Oceans", "Moons" } },
    { "Deku Link can fly using...", { "Flowers", "Feathers", "Bubbles", "Wind" } },
    { "12 x 12 = ?", { "144", "124", "132", "164" } },
    { "Which ends a Global Event early?", { "Its counter", "Pausing", "Crying", "Rupees" } },
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

static u8 sRiddleOrder[4];
static s32 sRiddleIdx;
static s32 sRiddleCorrect;

static void Riddle_Next(void) {
    s32 i;
    sRiddleIdx = Rng_Range(0, ARRAY_LEN(sRiddles) - 1);
    for (i = 0; i < 4; i++) {
        sRiddleOrder[i] = i;
    }
    for (i = 3; i > 0; i--) {
        s32 j = Rng_Range(0, i);
        u8 t = sRiddleOrder[i];
        sRiddleOrder[i] = sRiddleOrder[j];
        sRiddleOrder[j] = t;
    }
    gEv.hits = SEC(MF_MAX(5, P(0)));
}

static void Riddle_Start(PlayState* play) {
    gEv.score = 0; // riddles answered
    sRiddleCorrect = 0;
    Riddle_Next();
}

static void Riddle_Update(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    static const u16 dirs[4] = { BTN_DUP, BTN_DRIGHT, BTN_DDOWN, BTN_DLEFT };
    s32 i;
    s32 answer = -1;

    for (i = 0; i < 4; i++) {
        if (Pressed(dirs[i])) {
            answer = i;
        }
    }
    in->cur.button &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT);
    in->press.button &= ~(BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT);
    gEv.hits--;
    if (answer >= 0 || gEv.hits <= 0) {
        if (answer >= 0 && sRiddleOrder[answer] == 0) {
            sRiddleCorrect++;
            Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
        } else {
            Mf_DamagePlayer(MF_MAX(1, P(1)), false, 0);
            Mf_Sfx(NA_SE_SY_ERROR);
            gEv.failed = true;
        }
        gEv.score++;
        if (gEv.score >= MF_MAX(1, P(2))) {
            gEv.failed = (sRiddleCorrect * 2 < gEv.score);
            gEv.succeeded = true;
            return;
        }
        Riddle_Next();
    }
}

static void Riddle_Draw2D(PlayState* play) {
    static const char* arrows[4] = { "^ ", "> ", "v ", "< " };
    MfColor bg = { 20, 0, 50, 200 };
    MfColor q = { 255, 240, 170, 255 };
    s32 i;
    char buf[40];
    Draw2D_Rect(20, 60, 280, 76, bg);
    Draw2D_TextCentered(66, q, sRiddles[sRiddleIdx].q);
    for (i = 0; i < 4; i++) {
        buf[0] = '\0';
        Str_Cat(buf, arrows[i], sizeof(buf));
        Str_Cat(buf, sRiddles[sRiddleIdx].a[sRiddleOrder[i]], sizeof(buf));
        Draw2D_Text(40, 82 + i * 11, cText, buf);
    }
    buf[0] = '\0';
    Str_Cat(buf, "D-pad to answer - ", sizeof(buf));
    Str_CatInt(buf, gEv.hits / FPS + 1, sizeof(buf));
    Str_Cat(buf, "s", sizeof(buf));
    Draw2D_TextCentered(126, (MfColor){ 180, 180, 220, 255 }, buf);
}

// E28 Mask Salesman's Game: memorise the D-pad sequence and repeat it.
static u8 sSeq[10];
static s32 sSeqLen;
static s32 sSeqPos;

static void Simon_NewRound(void) {
    s32 i;
    sSeqLen = MF_MIN(10, 3 + gEv.score);
    for (i = 0; i < sSeqLen; i++) {
        sSeq[i] = Rng_Range(0, 3);
    }
    sSeqPos = 0;
    gEv.hits = 0; // show phase timer
    gEv.fa = 0.0f; // 0 = showing, 1 = input
}

static void Simon_Start(PlayState* play) {
    gEv.score = 0;
    Simon_NewRound();
}

static void Simon_Update(PlayState* play) {
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
        if (gEv.hits >= step * sSeqLen) {
            gEv.fa = 1.0f;
            gEv.hits = SEC(8);
        }
        return;
    }
    gEv.hits--;
    for (i = 0; i < 4; i++) {
        if (Pressed(dirs[i])) {
            if (sSeq[sSeqPos] == i) {
                sSeqPos++;
                Mf_Sfx(NA_SE_SY_DECIDE);
                if (sSeqPos >= sSeqLen) {
                    gEv.score++;
                    Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
                    if (gEv.score >= MF_MAX(1, P(1))) {
                        gEv.succeeded = true;
                        return;
                    }
                    Simon_NewRound();
                }
            } else {
                Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
                Mf_Sfx(NA_SE_SY_ERROR);
                gEv.failed = true;
                Simon_NewRound();
            }
            return;
        }
    }
    if (gEv.hits <= 0) {
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        gEv.failed = true;
        Simon_NewRound();
    }
}

static void Simon_Draw2D(PlayState* play) {
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
        if (idx < sSeqLen && (gEv.hits % step) < step - 2) {
            Draw2D_TextCentered(92, Mf_Rainbow(idx * 60, 255), arrows[sSeq[idx]]);
        }
    } else {
        Draw2D_TextCentered(74, c, "Repeat it with the D-pad!");
        buf[0] = '\0';
        for (i = 0; i < sSeqLen; i++) {
            Str_Cat(buf, i < sSeqPos ? arrows[sSeq[i]] : "_", sizeof(buf));
            Str_Cat(buf, " ", sizeof(buf));
        }
        Draw2D_TextCentered(92, cText, buf);
    }
}

// E29 Stargazer: a wandering star must be watched. Counter: keep it in the
// centre of your view until it's fully charted.
static void Star_Start(PlayState* play) {
    Player* p = Pl(play);
    s16 yaw = (s16)Rng_Range(0, 0xFFFF);
    InitHz();
    sHz[0].pos = p->actor.world.pos;
    sHz[0].pos.x += Math_SinS(yaw) * 450.0f;
    sHz[0].pos.z += Math_CosS(yaw) * 450.0f;
    sHz[0].pos.y += 220.0f;
    sHz[0].yaw = yaw + 0x4000;
    gEv.hits = 0;
}

static f32 CameraDot(PlayState* play, Vec3f* target) {
    Camera* cam = GET_ACTIVE_CAM(play);
    f32 lx = cam->at.x - cam->eye.x;
    f32 ly = cam->at.y - cam->eye.y;
    f32 lz = cam->at.z - cam->eye.z;
    f32 tx = target->x - cam->eye.x;
    f32 ty = target->y - cam->eye.y;
    f32 tz = target->z - cam->eye.z;
    f32 ll = sqrtf(lx * lx + ly * ly + lz * lz);
    f32 lt = sqrtf(tx * tx + ty * ty + tz * tz);
    return (lx * tx + ly * ty + lz * tz) / MF_MAX(1.0f, ll * lt);
}

static void Star_Update(PlayState* play) {
    Hz* h = &sHz[0];
    s32 need = SEC(MF_MAX(1, P(0)));

    if (Every(SEC(3))) {
        h->yaw += (s16)Rng_Range(-0x4000, 0x4000);
    }
    h->pos.x += Math_SinS(h->yaw) * 2.5f;
    h->pos.z += Math_CosS(h->yaw) * 2.5f;
    if (CameraDot(play, &h->pos) > 0.94f) {
        gEv.hits++;
        if ((gEv.hits % 10) == 0) {
            Mf_Sfx(NA_SE_SY_CURSOR);
        }
        if (gEv.hits >= need) {
            Sparkle(play, &h->pos, 255, 240, 160);
            Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
            gEv.succeeded = true;
        }
    } else if (Every(SEC(MF_MAX(1, P(1))))) {
        Player* p = Pl(play);
        Mf_DamagePlayer(MF_MAX(1, P(2)), false, 0);
        Lightning(play, &p->actor.world.pos);
        gEv.failed = true;
    }
}

static void Star_Draw3D(PlayState* play) {
    Draw3D_Orb(&sHz[0].pos, 60.0f, (MfColor){ 255, 240, 150, 240 });
}

static void Star_Draw2D(PlayState* play) {
    Draw2D_WorldMarker(play, &sHz[0].pos, (MfColor){ 255, 230, 120, 255 }, "*");
    Bar2D(36, gEv.hits, SEC(MF_MAX(1, P(0))), (MfColor){ 255, 230, 120, 230 }, "Charted");
}

// E30 Omen Targets: floating omens. Counter: shoot them down with any projectile.
static s32 Omens_Eligible(PlayState* play) {
    return (INV_CONTENT(ITEM_BOW) == ITEM_BOW && AMMO(ITEM_BOW) > 0) || (INV_CONTENT(ITEM_HOOKSHOT) == ITEM_HOOKSHOT) ||
           (INV_CONTENT(ITEM_DEKU_NUT) == ITEM_DEKU_NUT && AMMO(ITEM_DEKU_NUT) > 0) ||
           (INV_CONTENT(ITEM_MASK_ZORA) == ITEM_MASK_ZORA) || (INV_CONTENT(ITEM_MASK_DEKU) == ITEM_MASK_DEKU);
}

static void Omens_Start(PlayState* play) {
    s32 i;
    InitHz();
    sHzN = MF_CLAMP(P(0), 1, 8);
    for (i = 0; i < sHzN; i++) {
        Mf_FindSpawnPoint(play, &sHz[i].pos, 200.0f, 420.0f, (s16)(0x10000 * i / sHzN));
        sHz[i].pos.y += 90.0f;
        sHz[i].state = 1;
        sHz[i].yaw = (s16)Rng_Range(0, 0xFFFF);
    }
}

static void Omens_Update(PlayState* play) {
    s32 i;
    s32 alive = 0;
    s32 cat;
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
        if (h->state != 1) {
            continue;
        }
        h->yaw += 0x300;
        h->pos.y += Math_SinS(h->yaw) * 0.8f;
        for (cat = ACTORCAT_EXPLOSIVES; cat <= ACTORCAT_MISC; cat++) {
            Actor* a;
            if (cat == ACTORCAT_NPC || cat == ACTORCAT_ENEMY || cat == ACTORCAT_PROP) {
                continue;
            }
            a = play->actorCtx.actorLists[cat].first;
            while (a != NULL) {
                f32 dx = a->world.pos.x - h->pos.x;
                f32 dy = a->world.pos.y - h->pos.y;
                f32 dz = a->world.pos.z - h->pos.z;
                if (dx * dx + dy * dy + dz * dz < 55.0f * 55.0f) {
                    h->state = 0;
                    Sparkle(play, &h->pos, 255, 200, 255);
                    Mf_Sfx(NA_SE_SY_GET_ITEM);
                    break;
                }
                a = a->next;
            }
        }
        if (h->state == 1) {
            alive++;
        }
    }
    gEv.hits = alive;
    if (alive == 0) {
        gEv.succeeded = true;
    }
}

static void Omens_Draw3D(PlayState* play) {
    s32 i;
    for (i = 0; i < sHzN; i++) {
        if (sHz[i].state == 1) {
            Draw3D_Orb(&sHz[i].pos, 32.0f, (MfColor){ 255, 80, 200, 230 });
            Draw3D_Orb(&sHz[i].pos, 14.0f, (MfColor){ 255, 255, 255, 255 });
        }
    }
}

static void Omens_End(PlayState* play, s32 success) {
    s32 i;
    s32 left = 0;
    Player* p = Pl(play);
    for (i = 0; i < sHzN; i++) {
        if (sHz[i].state == 1) {
            Boom(play, &sHz[i].pos);
            left++;
        }
    }
    if (left > 0 && !gRt.playerDead) {
        Mf_DamagePlayer(MF_MIN(12, left * MF_MAX(1, P(1))), true, p->actor.shape.rot.y);
        gEv.failed = true;
    }
}

static void Omens_Draw2D(PlayState* play) {
    char buf[32];
    buf[0] = '\0';
    Str_Cat(buf, "Omens remaining: ", sizeof(buf));
    Str_CatInt(buf, gEv.hits, sizeof(buf));
    Draw2D_TextCentered(34, cText, buf);
}

// E31 Pressure Seals: step on the seals in numbered order.
static void Seals_Start(PlayState* play) {
    s32 i;
    InitHz();
    sHzN = MF_CLAMP(P(0), 2, 6);
    for (i = 0; i < sHzN; i++) {
        Mf_FindSpawnPoint(play, &sHz[i].pos, 180.0f, 450.0f, (s16)(0x10000 * i / sHzN + Rng_Range(-0x1000, 0x1000)));
        sHz[i].state = 1;
    }
    gEv.score = 0; // next seal to press
    gEv.hits = 0;  // wrong-step cooldown
}

static void Seals_Update(PlayState* play) {
    Player* p = Pl(play);
    s32 i;
    if (gEv.hits > 0) {
        gEv.hits--;
    }
    for (i = 0; i < sHzN; i++) {
        Hz* h = &sHz[i];
        if (h->state != 1 || Mf_DistXZ(&h->pos, &p->actor.world.pos) > 55.0f ||
            fabsf(h->pos.y - p->actor.world.pos.y) > 60.0f) {
            continue;
        }
        if (i == gEv.score) {
            h->state = 0;
            gEv.score++;
            Mf_Sfx(NA_SE_SY_CORRECT_CHIME);
            Sparkle(play, &h->pos, 120, 255, 180);
            if (gEv.score >= sHzN) {
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

static void Seals_Draw3D(PlayState* play) {
    s32 i;
    for (i = 0; i < sHzN; i++) {
        if (sHz[i].state == 1) {
            MfColor c = Mf_Rainbow(i * 60, 200);
            Draw3D_Ring(&sHz[i].pos, 55.0f, 12.0f, c);
            if (i == gEv.score) {
                Draw3D_Pillar(&sHz[i].pos, 10.0f, 160.0f, c);
            }
        }
    }
}

static void Seals_Draw2D(PlayState* play) {
    char buf[40];
    MfColor c = Mf_Rainbow(gEv.score * 60, 255);
    buf[0] = '\0';
    Str_Cat(buf, "Next seal: the one with the light beam (", sizeof(buf));
    Str_CatInt(buf, gEv.score + 1, sizeof(buf));
    Str_Cat(buf, ")", sizeof(buf));
    Draw2D_TextCentered(34, c, buf);
}

static void Seals_End(PlayState* play, s32 success) {
    if (!gEv.succeeded && !gRt.playerDead && success) {
        Player* p = Pl(play);
        Boom(play, &p->actor.world.pos);
        Mf_DamagePlayer(MF_MAX(1, P(2)), true, p->actor.shape.rot.y);
        gEv.failed = true;
    }
}

// ===========================================================================
// Table
// ===========================================================================

#define PN(a, b, c) { a, b, c }

const EventDef gEvents[NUM_EVENTS] = {
    { "Festival of Masks", "Nothing to fear. Enjoy the show!", "Carnival colours burst across the sky.",
      EVK_HARMLESS, 0, { 6, 1, 1 }, PN("Confetti", "Screen tint", "Music"), { 10, 1, 1 },
      AlwaysEligible, NULL, Festival_Update, NULL, Festival_Draw2D, NULL },
    { "Moon's Whisper", "Listen. The moon is spilling secrets.", "The moon lets slip where things are.",
      EVK_HARMLESS, 0, { 15, 5, 0 }, PN("Length (s)", "Sparkles", "-"), { 45, 9, 0 },
      AlwaysEligible, Whisper_Start, Whisper_Update, NULL, Whisper_Draw2D, NULL },
    { "Starfall Bounty", "Stand where the stars land to catch them!", "Falling stars scatter Moon Marks.",
      EVK_BENEFIT, 0, { 6, 30, 3 }, PN("Star rate", "Fall time (0.1s)", "Marks/star"), { 9, 60, 10 },
      AlwaysEligible, Starfall_Start, Starfall_Update, Starfall_Draw3D, Starfall_Draw2D, Starfall_End },
    { "Dowsing Moon", "Follow the warmth, then press A to dig.", "A cache of moonlight lies buried nearby.",
      EVK_BENEFIT, 0, { 70, 12, 0 }, PN("Distance (10u)", "Base reward", "-"), { 120, 40, 0 },
      AlwaysEligible, Dowsing_Start, Dowsing_Update, NULL, Dowsing_Draw2D, Dowsing_End },
    { "Moonfall", "Step out of the red circles before impact!", "Chunks of the moon rain down.",
      EVK_DANGER, 3, { 7, 4, 15 }, PN("Impacts/10s", "Damage (1/4 hrt)", "Warning (0.1s)"), { 20, 16, 40 },
      AlwaysEligible, Moonfall_Start, Moonfall_Update, Moonfall_Draw3D, NULL, NULL },
    { "Giant's Gale", "Hold the stick INTO the wind to brace.", "A giant exhales across Termina.",
      EVK_DANGER, 2, { 9, 30, 0 }, PN("Wind strength", "Debris every (0.1s)", "-"), { 20, 80, 0 },
      AlwaysEligible, Gale_Start, Gale_Update, NULL, Gale_Draw2D, NULL },
    { "Eye of the Moon", "When the eye opens, FREEZE. Don't move!", "The moon is watching.",
      EVK_DANGER, 2, { 10, 25, 2 }, PN("Warning (0.1s)", "Open time (0.1s)", "Damage"), { 30, 60, 8 },
      AlwaysEligible, Eye_Start, Eye_Update, NULL, Eye_Draw2D, NULL },
    { "Sinking Sands", "Keep moving! Standing still sinks you.", "The ground turns to hungry sand.",
      EVK_DANGER, 2, { 10, 1, 0 }, PN("Tick (0.1s)", "Damage", "-"), { 30, 8, 0 },
      AlwaysEligible, NULL, Sinking_Update, Sinking_Draw3D, NULL, NULL },
    { "Frostbite", "Roll to shake off the frost before it freezes you.", "A deathly chill seeps in.",
      EVK_DANGER, 2, { 3, 35, 6 }, PN("Chill speed", "Frost shed/roll", "Freeze damage"), { 5, 80, 16 },
      AlwaysEligible, NULL, Frost_Update, NULL, Frost_Draw2D, NULL },
    { "Scythe Sweep", "Jump the shockwave, or stand in its green gap.", "The moon swings a scythe of light.",
      EVK_DANGER, 3, { 5, 11, 4 }, PN("Every (s)", "Wave speed", "Damage"), { 12, 25, 16 },
      AlwaysEligible, Scythe_Start, Scythe_Update, Scythe_Draw3D, NULL, NULL },
    { "Moth Swarm", "Spin attack to scatter the clinging moths!", "Pale moths drink heartlight.",
      EVK_DANGER, 2, { 4, 6, 0 }, PN("Swarm rate", "Drain every (0.5s)", "-"), { 7, 20, 0 },
      AlwaysEligible, Moths_Start, Moths_Update, Moths_Draw3D, Moths_Draw2D, NULL },
    { "Tatl's Warning", "When Tatl shouts, press C-Up to heed her!", "Tatl senses something coming.",
      EVK_DANGER, 2, { 14, 4, 0 }, PN("Window (0.1s)", "Damage", "-"), { 30, 16, 0 },
      AlwaysEligible, Tatl_Start, Tatl_Update, NULL, Tatl_Draw2D, NULL },
    { "Hex of Silence", "Don't attack or use items. Every press hurts.", "A hex seals your hands.",
      EVK_DANGER, 1, { 2, 0, 0 }, PN("Damage/press", "-", "-"), { 12, 0, 0 },
      AlwaysEligible, NULL, Silence_Update, NULL, Silence_Draw2D, NULL },
    { "Moon Crank", "Spin the stick in circles to keep the ward wound!", "A moon-ward only turns while you crank it.",
      EVK_DANGER, 2, { 12, 18, 2 }, PN("Unwind/s", "Charge/spin", "Damage"), { 30, 40, 12 },
      AlwaysEligible, Crank_Start, Crank_Update, NULL, Crank_Draw2D, NULL },
    { "Moon Tether", "Stay inside the drifting circle!", "A chain of moonlight binds you.",
      EVK_DANGER, 2, { 16, 15, 3 }, PN("Radius (10u)", "Drift speed (0.1)", "Yank damage"), { 40, 50, 12 },
      AlwaysEligible, Tether_Start, Tether_Update, Tether_Draw3D, Tether_Draw2D, NULL },
    { "Moon's Grasp", "When it grabs you, MASH A to break free!", "Hands of stone reach from the ground.",
      EVK_DANGER, 3, { 12, 6, 0 }, PN("Presses needed", "Damage", "-"), { 30, 16, 0 },
      AlwaysEligible, Grasp_Start, Grasp_Update, Grasp_Draw3D, Grasp_Draw2D, NULL },
    { "Heartbeat Hex", "Press A exactly on each heartbeat.", "Your heart beats to the moon's drum.",
      EVK_DANGER, 2, { 14, 4, 2 }, PN("Beat (0.1s)", "Window (frames)", "Miss damage"), { 30, 10, 8 },
      AlwaysEligible, Heartbeat_Start, Heartbeat_Update, NULL, Heartbeat_Draw2D, NULL },
    { "Rising Moon-Tide", "Climb! Get above the rising dark tide.", "Black water rises from nowhere.",
      EVK_DANGER, 3, { 14, 4, 0 }, PN("Rise (10u)", "Damage/s", "-"), { 40, 12, 0 },
      AlwaysEligible, NULL, Tide_Update, Tide_Draw3D, Tide_Draw2D, NULL },
    { "Moon's Reflex Test", "Press the button that flashes up. Fast!", "The moon tests your reflexes.",
      EVK_DANGER, 2, { 12, 3, 0 }, PN("Window (0.1s)", "Damage", "-"), { 30, 12, 0 },
      AlwaysEligible, Reflex_Start, Reflex_Update, NULL, Reflex_Draw2D, NULL },
    { "Gorgon's Gaze", "Keep your camera turned AWAY from the eye.", "An eye of stone opens in the air.",
      EVK_DANGER, 2, { 7, 4, 6 }, PN("Moves every (s)", "Stare speed", "Petrify damage"), { 15, 10, 16 },
      AlwaysEligible, Gorgon_Start, Gorgon_Update, Gorgon_Draw3D, Gorgon_Draw2D, NULL },
    { "Ambush", "Defeat every ambusher before time runs out!", "The local monsters answer the moon's call.",
      EVK_DANGER, 4, { 3, 0, 0 }, PN("Ambushers", "-", "-"), { 6, 0, 0 },
      Ambush_Eligible, Ambush_Start, Ambush_Update, NULL, Ambush_Draw2D, Ambush_End },
    { "Dissonance", "Play any 3 notes on your ocarina to answer it.", "A sour melody claws at your heart.",
      EVK_DANGER, 1, { 3, 1, 0 }, PN("Drain every (s)", "Damage", "-"), { 10, 8, 0 },
      Dissonance_Eligible, NULL, Dissonance_Update, NULL, Dissonance_Draw2D, NULL },
    { "Mirror Madness", "Controls inverted! Face the shard, hold Z to shatter it.", "The world flips like a reflection.",
      EVK_DANGER, 2, { 15, 5, 1 }, PN("Stare (0.1s)", "Cut every (s)", "Damage"), { 40, 15, 8 },
      AlwaysEligible, Mirror_Start, Mirror_Update, Mirror_Draw3D, Mirror_Draw2D, NULL },
    { "Volley of Stars", "Face the glowing star and hold R to guard!", "Stars are hurled at you from afar.",
      EVK_DANGER, 3, { 15, 4, 4 }, PN("Warning (0.1s)", "Damage", "Gap (s)"), { 30, 16, 10 },
      AlwaysEligible, Volley_Start, Volley_Update, Volley_Draw3D, NULL, NULL },
    { "Lonely Moon", "Talk to someone, anyone, to break the spell.", "A crushing loneliness settles in.",
      EVK_DANGER, 1, { 4, 1, 0 }, PN("Drain every (s)", "Damage", "-"), { 10, 8, 0 },
      Lonely_Eligible, NULL, Lonely_Update, NULL, NULL, NULL },
    { "Sleeping Giant", "Tiptoe! Running makes noise and wakes the giant.", "A giant sleeps beneath your feet.",
      EVK_DANGER, 2, { 35, 6, 6 }, PN("Speed limit (0.1)", "Noise gain", "Stomp damage"), { 80, 20, 16 },
      AlwaysEligible, NULL, Giant_Update, NULL, Giant_Draw2D, NULL },
    { "Tag, You're It", "Chase down the wisp and touch it!", "A mischievous wisp tagged you.",
      EVK_DANGER, 2, { 50, 8, 0 }, PN("Wisp speed (0.1)", "Tag-back damage", "-"), { 90, 20, 0 },
      AlwaysEligible, Tag_Start, Tag_Update, Tag_Draw3D, NULL, Tag_End },
    { "Moon's Riddle", "Answer with the D-pad. Wrong answers hurt.", "The moon asks questions.",
      EVK_DANGER, 1, { 12, 4, 3 }, PN("Time/riddle (s)", "Damage", "Riddles"), { 30, 16, 6 },
      AlwaysEligible, Riddle_Start, Riddle_Update, NULL, Riddle_Draw2D, NULL },
    { "Mask Salesman's Game", "Memorise the arrows, then repeat them on the D-pad.", "\"Such a fun game...\"",
      EVK_DANGER, 2, { 8, 3, 4 }, PN("Arrow time (0.1s)", "Rounds", "Damage"), { 20, 6, 16 },
      AlwaysEligible, Simon_Start, Simon_Update, NULL, Simon_Draw2D, NULL },
    { "Stargazer", "Keep the wandering star centred in your view!", "A lost star must be charted.",
      EVK_DANGER, 2, { 6, 3, 2 }, PN("Watch time (s)", "Punish every (s)", "Damage"), { 20, 10, 12 },
      AlwaysEligible, Star_Start, Star_Update, Star_Draw3D, Star_Draw2D, NULL },
    { "Omen Targets", "Shoot the omens down with any projectile!", "Omens hang in the sky, ready to burst.",
      EVK_DANGER, 3, { 3, 4, 0 }, PN("Omens", "Damage each", "-"), { 8, 16, 0 },
      Omens_Eligible, Omens_Start, Omens_Update, Omens_Draw3D, Omens_Draw2D, Omens_End },
    { "Pressure Seals", "Step on the seals in order: follow the beam.", "Seals of the moon must be pressed.",
      EVK_DANGER, 2, { 4, 3, 8 }, PN("Seals", "Wrong-step damage", "Fail damage"), { 6, 12, 20 },
      AlwaysEligible, Seals_Start, Seals_Update, Seals_Draw3D, Seals_Draw2D, Seals_End },
};
