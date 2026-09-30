#include "mf.h"

// ---------------------------------------------------------------------------
// 2D overlay: rectangles straight into OVERLAY_DISP, text through the game's
// debug printer (GfxPrint), which uses a built-in 8x8 font.
// ---------------------------------------------------------------------------

static GraphicsContext* s2DGfx = NULL;

void Draw2D_Begin(GraphicsContext* gfxCtx) {
    s2DGfx = gfxCtx;
}

void Draw2D_End(void) {
    s2DGfx = NULL;
}

void Draw2D_Rect(s32 x, s32 y, s32 w, s32 h, MfColor c) {
    GraphicsContext* gfxCtx = s2DGfx;
    if (gfxCtx == NULL || w <= 0 || h <= 0) {
        return;
    }
    x = MF_CLAMP(x, 0, SCREEN_WIDTH);
    y = MF_CLAMP(y, 0, SCREEN_HEIGHT);
    OPEN_DISPS(gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, c.r, c.g, c.b, c.a);
    gDPFillRectangle(OVERLAY_DISP++, x, y, MF_MIN(x + w, SCREEN_WIDTH), MF_MIN(y + h, SCREEN_HEIGHT));
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(gfxCtx);
}

void Draw2D_Text(s32 x, s32 y, MfColor c, const char* text) {
    GraphicsContext* gfxCtx = s2DGfx;
    GfxPrint printer;
    Gfx* polyOpa;
    Gfx* gfx;

    if (gfxCtx == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    OPEN_DISPS(gfxCtx);
    polyOpa = POLY_OPA_DISP;
    gfx = Gfx_Open(polyOpa);
    gSPDisplayList(OVERLAY_DISP++, gfx);

    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    GfxPrint_SetColor(&printer, c.r, c.g, c.b, c.a);
    GfxPrint_SetPosPx(&printer, x, y);
    GfxPrint_PrintString(&printer, text);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    gSPEndDisplayList(gfx++);
    Gfx_Close(polyOpa, gfx);
    POLY_OPA_DISP = gfx;
    CLOSE_DISPS(gfxCtx);
}

void Draw2D_TextCentered(s32 y, MfColor c, const char* text) {
    s32 w = Str_Len(text) * 8;
    Draw2D_Text((SCREEN_WIDTH - w) / 2, y, c, text);
}

MfColor Mf_Rainbow(s32 t, u8 alpha) {
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

// ---------------------------------------------------------------------------
// 3D primitives drawn into POLY_XLU_DISP with per-vertex colour.
// Vertices are generated per frame into graph memory.
// ---------------------------------------------------------------------------

static PlayState* s3DPlay = NULL;

#define CIRCLE_SEGS 24

void Draw3D_Begin(PlayState* play) {
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

void Draw3D_End(void) {
    GraphicsContext* gfxCtx;
    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    OPEN_DISPS(gfxCtx);
    gDPPipeSync(POLY_XLU_DISP++);
    // Restore the defaults the game's xlu setup expects.
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING | G_FOG);
    CLOSE_DISPS(gfxCtx);
    s3DPlay = NULL;
}

static void SetVtx(Vtx* v, f32 x, f32 y, f32 z, MfColor c) {
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

static void LoadMatrixAt(Vec3f* pos, s32 billboard) {
    GraphicsContext* gfxCtx = s3DPlay->state.gfxCtx;
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    if (billboard) {
        Matrix_ReplaceRotation(&s3DPlay->billboardMtxF);
    }
    OPEN_DISPS(gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    CLOSE_DISPS(gfxCtx);
}

// Draws a triangle fan in the local XZ plane (flat) or XY plane (billboard).
static void DrawFan(Vec3f* pos, f32 radius, MfColor center, MfColor rim, s32 billboard, s16 startAngle, s32 sweep) {
    GraphicsContext* gfxCtx;
    Vtx* vtx;
    s32 i;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    vtx = GRAPH_ALLOC(gfxCtx, sizeof(Vtx) * (CIRCLE_SEGS + 2));
    SetVtx(&vtx[0], 0, 0, 0, center);
    for (i = 0; i <= CIRCLE_SEGS; i++) {
        s16 a = startAngle + (s16)(sweep * i / CIRCLE_SEGS);
        f32 s = Math_SinS(a) * radius;
        f32 c = Math_CosS(a) * radius;
        if (billboard) {
            SetVtx(&vtx[i + 1], s, c, 0, rim);
        } else {
            SetVtx(&vtx[i + 1], s, 0, c, rim);
        }
    }
    LoadMatrixAt(pos, billboard);
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, CIRCLE_SEGS + 2, 0);
    for (i = 1; i <= CIRCLE_SEGS; i++) {
        gSP1Triangle(POLY_XLU_DISP++, 0, i, i + 1, 0);
    }
    CLOSE_DISPS(gfxCtx);
}

void Draw3D_Disk(Vec3f* pos, f32 radius, MfColor c) {
    Vec3f p = *pos;
    MfColor rim = c;
    p.y += 2.0f;
    rim.a = (u8)(c.a / 3);
    DrawFan(&p, radius, c, rim, false, 0, 0x10000);
}

void Draw3D_Wedge(Vec3f* pos, f32 radius, s16 yaw, s16 halfAngle, MfColor c) {
    Vec3f p = *pos;
    p.y += 3.0f;
    DrawFan(&p, radius, c, c, false, yaw - halfAngle, (s32)halfAngle * 2);
}

void Draw3D_Orb(Vec3f* pos, f32 radius, MfColor c) {
    MfColor rim = c;
    rim.a = 0;
    DrawFan(pos, radius, c, rim, true, 0, 0x10000);
}

#define RING_SEGS 15

void Draw3D_Ring(Vec3f* pos, f32 radius, f32 thickness, MfColor c) {
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
        SetVtx(&vtx[i * 2], Math_SinS(a) * radius, 0, Math_CosS(a) * radius, c);
        SetVtx(&vtx[i * 2 + 1], Math_SinS(a) * inner, 0, Math_CosS(a) * inner, ci);
    }
    p.y += 3.0f;
    LoadMatrixAt(&p, false);
    OPEN_DISPS(gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, (RING_SEGS + 1) * 2, 0);
    for (i = 0; i < RING_SEGS; i++) {
        s32 o = i * 2;
        gSP2Triangles(POLY_XLU_DISP++, o, o + 2, o + 1, 0, o + 1, o + 2, o + 3, 0);
    }
    CLOSE_DISPS(gfxCtx);
}

// Vertical beam that always faces the camera around the Y axis.
void Draw3D_Pillar(Vec3f* pos, f32 radius, f32 height, MfColor c) {
    GraphicsContext* gfxCtx;
    Vtx* vtx;
    MfColor top = c;

    if (s3DPlay == NULL) {
        return;
    }
    gfxCtx = s3DPlay->state.gfxCtx;
    top.a = 0;
    vtx = GRAPH_ALLOC(gfxCtx, sizeof(Vtx) * 4);
    SetVtx(&vtx[0], -radius, 0, 0, c);
    SetVtx(&vtx[1], radius, 0, 0, c);
    SetVtx(&vtx[2], radius, height, 0, top);
    SetVtx(&vtx[3], -radius, height, 0, top);

    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_RotateYS(Camera_GetCamDirYaw(GET_ACTIVE_CAM(s3DPlay)), MTXMODE_APPLY);
    OPEN_DISPS(gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_XLU_DISP++, gfxCtx);
    gSPVertex(POLY_XLU_DISP++, vtx, 4, 0);
    gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    CLOSE_DISPS(gfxCtx);
}
