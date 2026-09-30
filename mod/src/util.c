#include "mf.h"

// ---------------------------------------------------------------------------
// RNG: xorshift32, independent of the game's RNG so we never disturb the
// randomizer's or the game's own random sequences.
// ---------------------------------------------------------------------------

u32 Rng_Next(void) {
    u32 x = gMf.rngState;
    if (x == 0) {
        x = 0x9E3779B9u ^ (u32)osGetTime();
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gMf.rngState = x;
    return x;
}

s32 Rng_Range(s32 lo, s32 hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + (s32)(Rng_Next() % (u32)(hi - lo + 1));
}

f32 Rng_Float(void) {
    return (Rng_Next() & 0xFFFFFF) / (f32)0x1000000;
}

s32 Rng_Chance(s32 percent) {
    return Rng_Range(0, 99) < percent;
}

// ---------------------------------------------------------------------------
// Strings (no libc in mod code)
// ---------------------------------------------------------------------------

s32 Str_Len(const char* s) {
    s32 n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void Str_Copy(char* dst, const char* src, s32 max) {
    s32 i = 0;
    if (max <= 0) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void Str_Cat(char* dst, const char* src, s32 max) {
    s32 n = Str_Len(dst);
    s32 i = 0;
    while (src[i] != '\0' && n < max - 1) {
        dst[n++] = src[i++];
    }
    dst[n] = '\0';
}

void Str_CatInt(char* dst, s32 value, s32 max) {
    char tmp[12];
    s32 i = 0;
    s32 neg = value < 0;
    u32 v = neg ? (u32)(-value) : (u32)value;
    char out[13];
    s32 o = 0;

    do {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    } while (v != 0 && i < 11);
    if (neg) {
        out[o++] = '-';
    }
    while (i > 0) {
        out[o++] = tmp[--i];
    }
    out[o] = '\0';
    Str_Cat(dst, out, max);
}

void Str_CatHex(char* dst, u32 value, s32 max) {
    static const char digits[] = "0123456789ABCDEF";
    char out[9];
    s32 i;
    for (i = 0; i < 8; i++) {
        out[i] = digits[(value >> (28 - i * 4)) & 0xF];
    }
    out[8] = '\0';
    Str_Cat(dst, out, max);
}

s32 Str_ParseInt(const char** s) {
    const char* p = *s;
    s32 neg = 0;
    s32 v = 0;
    while (*p == ' ') {
        p++;
    }
    if (*p == '-') {
        neg = 1;
        p++;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *s = p;
    return neg ? -v : v;
}

s32 Str_StartsWith(const char* s, const char* prefix) {
    while (*prefix != '\0') {
        if (*s++ != *prefix++) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Player helpers
// ---------------------------------------------------------------------------

Player* Mf_Player(void) {
    if (gRt.play == NULL) {
        return NULL;
    }
    return GET_PLAYER(gRt.play);
}

static const u8 sDifficultyPct[] = { 60, 100, 150, 200 };

s32 Mf_DifficultyScale(s32 value) {
    s32 pct = sDifficultyPct[gRt.difficultyIdx & 3] * gMf.settings.difficultyPct / 100;
    s32 scaled = value * pct / 100;

    if (value > 0 && scaled < 1) {
        scaled = 1;
    }
    return scaled;
}

// Damage in quarter hearts (1 heart = 16 health units). Goes through the game's
// own damage path so invincibility frames, double defense and the Giant's Mask apply.
void Mf_DamagePlayer(s32 quarterHearts, s32 knockback, s16 fromYaw) {
    PlayState* play = gRt.play;
    Player* player = Mf_Player();
    s32 dmg;

    if (play == NULL || player == NULL || gRt.playerDead) {
        return;
    }
    if (Curse_Active(CURSE_FRAILTY) && gEv.id >= 0) {
        quarterHearts = quarterHearts * 3 / 2;
    }
    dmg = Mf_DifficultyScale(quarterHearts * 4);
    if (dmg <= 0) {
        return;
    }
    if (knockback) {
        // Type 2: small knockback, respects invincibility frames.
        func_800B8D10(play, NULL, 7.0f, fromYaw, 6.0f, 2, (u32)MF_MIN(dmg, 255));
    } else if (play->damagePlayer != NULL) {
        play->damagePlayer(play, -dmg);
    }
}

void Mf_HealPlayer(s32 quarterHearts) {
    if (gRt.play != NULL && !gRt.playerDead) {
        Health_ChangeBy(gRt.play, quarterHearts * 4);
    }
}

void Mf_ShockPlayer(s32 quarterHearts) {
    PlayState* play = gRt.play;
    Player* player = Mf_Player();
    if (play == NULL || player == NULL || gRt.playerDead) {
        return;
    }
    func_800B8D10(play, NULL, 0.0f, player->actor.shape.rot.y, 0.0f, 4,
                  (u32)MF_MIN(Mf_DifficultyScale(quarterHearts * 4), 255));
}

void Mf_BurnPlayer(void) {
    Player* player = Mf_Player();
    s32 i;
    if (player == NULL || gRt.playerDead || player->bodyIsBurning) {
        return;
    }
    // Same as the game's own ignition, but with short flame timers (about one heart).
    for (i = 0; i < PLAYER_BODYPART_MAX; i++) {
        player->bodyFlameTimers[i] = Rng_Range(30, 70);
    }
    player->bodyIsBurning = true;
}

f32 Mf_FloorY(Vec3f* pos, f32 fallback) {
    CollisionPoly* poly;
    s32 bgId;
    Vec3f p = *pos;
    f32 y;

    if (gRt.play == NULL) {
        return fallback;
    }
    y = BgCheck_EntityRaycastFloor3(&gRt.play->colCtx, &poly, &bgId, &p);
    if (y <= BGCHECK_Y_MIN + 1.0f) {
        return fallback;
    }
    return y;
}

s16 Mf_YawTo(Vec3f* from, Vec3f* to) {
    return Math_Atan2S_XY(to->z - from->z, to->x - from->x);
}

f32 Mf_DistXZ(Vec3f* a, Vec3f* b) {
    f32 dx = a->x - b->x;
    f32 dz = a->z - b->z;
    return sqrtf(dx * dx + dz * dz);
}

// Looks for solid ground around the player, roughly at the same height, so that
// spawned enemies and hazards don't land inside walls or at the bottom of pits.
s32 Mf_FindSpawnPoint(PlayState* play, Vec3f* out, f32 minDist, f32 maxDist, s16 preferYaw) {
    Player* player = GET_PLAYER(play);
    s32 attempt;

    for (attempt = 0; attempt < 16; attempt++) {
        s16 yaw = preferYaw + (s16)Rng_Range(-0x3000 - attempt * 0x400, 0x3000 + attempt * 0x400);
        f32 dist = minDist + Rng_Float() * (maxDist - minDist);
        Vec3f p;
        f32 floorY;

        p.x = player->actor.world.pos.x + Math_SinS(yaw) * dist;
        p.z = player->actor.world.pos.z + Math_CosS(yaw) * dist;
        p.y = player->actor.world.pos.y + 120.0f;
        floorY = Mf_FloorY(&p, BGCHECK_Y_MIN);
        if (floorY <= BGCHECK_Y_MIN + 1.0f) {
            continue;
        }
        if (fabsf(floorY - player->actor.world.pos.y) > 180.0f) {
            continue;
        }
        p.y = floorY;
        *out = p;
        return true;
    }
    // Fallback: right next to the player.
    *out = player->actor.world.pos;
    out->x += Math_SinS(preferYaw) * minDist * 0.5f;
    out->z += Math_CosS(preferYaw) * minDist * 0.5f;
    out->y = Mf_FloorY(out, player->actor.world.pos.y);
    return false;
}

s32 Mf_IsOutdoors(PlayState* play) {
    return World_ZoneForScene(play->sceneId) != ZONE_NONE;
}

s32 Mf_HasWater(PlayState* play) {
    return (play->colCtx.colHeader != NULL) && (play->colCtx.colHeader->numWaterBoxes > 0);
}

s32 Mf_PlayerGrounded(void) {
    Player* player = Mf_Player();
    return (player != NULL) && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
}

f32 Mf_StickMag(void) {
    f32 x = gRt.stickX;
    f32 y = gRt.stickY;
    return sqrtf(x * x + y * y);
}

s16 Mf_StickWorldYaw(PlayState* play) {
    s16 angle = Math_Atan2S_XY(gRt.stickY, -gRt.stickX);
    return angle + Camera_GetInputDirYaw(GET_ACTIVE_CAM(play));
}

void Mf_Sfx(u16 sfxId) {
    Audio_PlaySfx(sfxId);
}
