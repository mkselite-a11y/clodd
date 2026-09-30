#include "mf.h"

#define TOAST_MAX 4
#define TOAST_LEN 64

typedef struct {
    char text[TOAST_LEN];
    MfColor color;
    s32 timer;
} Toast;

static Toast sToasts[TOAST_MAX];
static char sBannerTitle[40];
static char sBannerSub[64];
static s32 sBannerTimer = 0;
static char sPrompt[40];
static s32 sPromptFrames = 0;

void Hud_Notify(const char* text, MfColor c) {
    s32 i;
    // Push older toasts down; drop the oldest.
    for (i = TOAST_MAX - 1; i > 0; i--) {
        sToasts[i] = sToasts[i - 1];
    }
    Str_Copy(sToasts[0].text, text, TOAST_LEN);
    sToasts[0].color = c;
    sToasts[0].timer = SEC(5);
}

void Hud_Banner(const char* title, const char* sub) {
    if (!gMf.settings.hudBanners) {
        return;
    }
    Str_Copy(sBannerTitle, title, sizeof(sBannerTitle));
    Str_Copy(sBannerSub, (sub != NULL) ? sub : "", sizeof(sBannerSub));
    sBannerTimer = SEC(3.5f);
}

void Hud_SetPrompt(const char* text) {
    Str_Copy(sPrompt, text, sizeof(sPrompt));
    sPromptFrames = 2;
}

void Hud_Update(void) {
    s32 i;
    for (i = 0; i < TOAST_MAX; i++) {
        if (sToasts[i].timer > 0) {
            sToasts[i].timer--;
        }
    }
    if (sBannerTimer > 0) {
        sBannerTimer--;
    }
    if (sPromptFrames > 0) {
        sPromptFrames--;
    }
}

// Splits a long line in two at a space so it fits the 38-column screen.
static void DrawWrapped(s32 y, MfColor c, const char* text) {
    char a[40];
    char b[40];
    s32 len = Str_Len(text);
    s32 cut;
    s32 i;

    if (len <= 38) {
        Draw2D_TextCentered(y, c, text);
        return;
    }
    cut = 38;
    while (cut > 10 && text[cut] != ' ') {
        cut--;
    }
    for (i = 0; i < cut && i < 39; i++) {
        a[i] = text[i];
    }
    a[i] = '\0';
    Str_Copy(b, text + cut + 1, sizeof(b));
    Draw2D_TextCentered(y, c, a);
    Draw2D_TextCentered(y + 9, c, b);
}

void Hud_Draw(PlayState* play) {
    s32 i;
    s32 y = 34;

    if (gRt.menuOpen) {
        return;
    }
    // Toasts (under the event bar)
    for (i = 0; i < TOAST_MAX; i++) {
        if (sToasts[i].timer > 0) {
            MfColor bg = { 0, 0, 0, 120 };
            MfColor c = sToasts[i].color;
            if (sToasts[i].timer < SEC(1)) {
                c.a = (u8)(255 * sToasts[i].timer / SEC(1));
                bg.a = (u8)(120 * sToasts[i].timer / SEC(1));
            }
            if (Str_Len(sToasts[i].text) > 38) {
                Draw2D_Rect(8, y - 2, SCREEN_WIDTH - 16, 21, bg);
                DrawWrapped(y, c, sToasts[i].text);
                y += 22;
            } else {
                Draw2D_Rect(8, y - 2, SCREEN_WIDTH - 16, 12, bg);
                Draw2D_TextCentered(y, c, sToasts[i].text);
                y += 13;
            }
        }
    }
    // Banner
    if (sBannerTimer > 0) {
        MfColor bg = { 20, 0, 40, 170 };
        MfColor title = Mf_Rainbow(gRt.frame * 6, 255);
        MfColor sub = { 230, 230, 255, 255 };
        Draw2D_Rect(0, 92, SCREEN_WIDTH, 36, bg);
        Draw2D_TextCentered(98, title, sBannerTitle);
        DrawWrapped(112, sub, sBannerSub);
    }
    // Prompt
    if (sPromptFrames > 0) {
        MfColor bg = { 0, 0, 0, 150 };
        MfColor c = { 255, 240, 120, 255 };
        s32 w = Str_Len(sPrompt) * 8 + 12;
        Draw2D_Rect((SCREEN_WIDTH - w) / 2, 176, w, 13, bg);
        Draw2D_TextCentered(178, c, sPrompt);
    }
}
