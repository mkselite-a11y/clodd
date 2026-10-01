// ---------------------------------------------------------------------------
// V3 pop-ups: up to 4 stacked at the bottom of the screen, 5 seconds each with
// a fade, coloured by where they came from. In the menu, feedback shows as a
// box in the middle instead.
// ---------------------------------------------------------------------------

#define NOTE_MAX 4
#define NOTE_FRAMES (5 * 20)
#define NOTE_WIDTH 36

typedef struct {
    char line[2][NOTE_WIDTH + 2];
    u8 lines;
    u8 kind;
    s16 timer;
} PopNote;

static PopNote sNotes[NOTE_MAX]; // [0] is the newest
static char sBannerTitle[40];
static char sBannerSub[2][NOTE_WIDTH + 2];
static s32 sBannerTimer = 0;

static s32 Note_Same(const PopNote* n, const char* text) {
    char joined[80];
    s32 k = 0;
    s32 i;

    for (i = 0; (n->line[0][i] != '\0') && (k < 78); i++) {
        joined[k++] = n->line[0][i];
    }
    if (n->lines > 1) {
        joined[k++] = ' ';
        for (i = 0; (n->line[1][i] != '\0') && (k < 78); i++) {
            joined[k++] = n->line[1][i];
        }
    }
    joined[k] = '\0';
    for (i = 0; (joined[i] != '\0') && (text[i] == joined[i]); i++) {
    }
    return (joined[i] == '\0') && (text[i] == '\0');
}

// Splits text over up to two lines at a space.
static s32 Note_Wrap(const char* text, char out[2][NOTE_WIDTH + 2]) {
    s32 len = Ui_StrLen(text);
    s32 cut = len;
    s32 i;
    s32 k;

    out[0][0] = out[1][0] = '\0';
    if (len > NOTE_WIDTH) {
        cut = NOTE_WIDTH;
        for (i = NOTE_WIDTH; i > NOTE_WIDTH / 2; i--) {
            if (text[i] == ' ') {
                cut = i;
                break;
            }
        }
    }
    for (k = 0; (k < cut) && (k < NOTE_WIDTH); k++) {
        out[0][k] = text[k];
    }
    out[0][k] = '\0';
    if (cut >= len) {
        return 1;
    }
    i = cut;
    while (text[i] == ' ') {
        i++;
    }
    for (k = 0; (text[i + k] != '\0') && (k < NOTE_WIDTH); k++) {
        out[1][k] = text[i + k];
    }
    out[1][k] = '\0';
    return 2;
}

void Menu_Notify(const char* text, s32 kind) {
    s32 i;

    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }
    if ((sNotes[0].timer > 0) && Note_Same(&sNotes[0], text)) {
        sNotes[0].timer = NOTE_FRAMES;
        return;
    }
    for (i = NOTE_MAX - 1; i > 0; i--) {
        sNotes[i] = sNotes[i - 1];
    }
    sNotes[0].lines = Note_Wrap(text, sNotes[0].line);
    sNotes[0].kind = kind;
    sNotes[0].timer = NOTE_FRAMES;
}

void Menu_ShowBanner(const char* title, const char* sub) {
    char joined[80];
    s32 n;
    s32 i;

    if (!gOpt[ID_V2_BANNERS]) {
        for (n = 0; (title[n] != '\0') && (n < 40); n++) {
            joined[n] = title[n];
        }
        joined[n++] = ' ';
        for (i = 0; (sub[i] != '\0') && (n < 78); i++) {
            joined[n++] = sub[i];
        }
        joined[n] = '\0';
        Menu_Notify(joined, NOTE_NEMESIS);
        return;
    }
    for (n = 0; (title[n] != '\0') && (n < 38); n++) {
        sBannerTitle[n] = title[n];
    }
    sBannerTitle[n] = '\0';
    Note_Wrap(sub, sBannerSub);
    sBannerTimer = 70;
}

void Menu_ShowToast(const char* text) {
    if ((text == NULL) || (text[0] == '\0')) {
        return;
    }
    if (!gMenuOpen) {
        Menu_Notify(text, NOTE_PLAIN);
        return;
    }
    // In the menu: the feedback box replaces right away.
    Toast_Copy(sToastBuf, text);
    sToastText = sToastBuf;
    sToastTimer = TOAST_FRAMES;
}

static void Notes_Tick(void) {
    s32 i;

    if (sToastTimer > 0) {
        sToastTimer--;
    }
    if (gMenuOpen) {
        return;
    }
    for (i = 0; i < NOTE_MAX; i++) {
        if (sNotes[i].timer > 0) {
            sNotes[i].timer--;
        }
    }
    if (sBannerTimer > 0) {
        sBannerTimer--;
    }
}

static s32 Notes_Any(void) {
    s32 i;

    if (sBannerTimer > 0) {
        return true;
    }
    for (i = 0; i < NOTE_MAX; i++) {
        if (sNotes[i].timer > 0) {
            return true;
        }
    }
    return false;
}

static void Notes_Color(s32 kind, u8* r, u8* g, u8* b) {
    static const u8 sCol[6][3] = {
        { 240, 240, 240 }, { 200, 175, 255 }, { 255, 255, 170 }, { 255, 140, 140 }, { 255, 170, 110 }, { 190, 200, 255 },
    };
    s32 k = ((kind >= 0) && (kind < 6)) ? kind : 0;

    *r = sCol[k][0];
    *g = sCol[k][1];
    *b = sCol[k][2];
}

static void Notes_Draw(PlayState* play, Gfx** gfxP) {
    Gfx* gfx = *gfxP;
    GfxPrint printer;
    s32 y = 226;
    s32 i;
    s32 k;

    // Backings first, newest at the bottom.
    for (i = 0; i < NOTE_MAX; i++) {
        PopNote* n = &sNotes[i];
        s32 h = (n->lines > 1) ? 21 : 12;
        s32 w = 0;
        u8 alpha;

        if (n->timer <= 0) {
            continue;
        }
        alpha = (n->timer > 20) ? 150 : (u8)(n->timer * 7);
        for (k = 0; k < n->lines; k++) {
            w = MAX(w, Ui_StrLen(n->line[k]) * 8);
        }
        w += 12;
        gfx = Ui_DrawRect(gfx, (SCREEN_WIDTH - w) / 2, y - h + 10, (SCREEN_WIDTH + w) / 2, y + 12, 0, 0, 0, alpha);
        y -= h + 3;
    }
    if (sBannerTimer > 0) {
        u8 a = (sBannerTimer > 15) ? 170 : (u8)(sBannerTimer * 11);

        gfx = Ui_DrawRect(gfx, 0, 92, SCREEN_WIDTH, 128 + ((sBannerSub[1][0] != '\0') ? 10 : 0), 20, 0, 40, a);
    }
    bzero(&printer, sizeof(printer));
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    gDPSetTextureFilter(printer.dList++, G_TF_POINT);
    printer.flags |= (1 << 2); // text shadow
    y = 226;
    for (i = 0; i < NOTE_MAX; i++) {
        PopNote* n = &sNotes[i];
        s32 h = (n->lines > 1) ? 21 : 12;
        u8 r, g, b;
        u8 alpha;

        if (n->timer <= 0) {
            continue;
        }
        alpha = (n->timer > 20) ? 255 : (u8)(n->timer * 12);
        Notes_Color(n->kind, &r, &g, &b);
        GfxPrint_SetColor(&printer, r, g, b, alpha);
        if (n->lines > 1) {
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[0]) * 8) / 2, y - 9, n->line[0]);
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[1]) * 8) / 2, y + 1, n->line[1]);
        } else {
            Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(n->line[0]) * 8) / 2, y + 1, n->line[0]);
        }
        y -= h + 3;
    }
    if (sBannerTimer > 0) {
        u8 a = (sBannerTimer > 15) ? 255 : (u8)(sBannerTimer * 17);
        u8 r, g, b;

        HueToRgb((play->gameplayFrames * 6) % 360, &r, &g, &b);
        GfxPrint_SetColor(&printer, r, g, b, a);
        Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sBannerTitle) * 8) / 2, 98, sBannerTitle);
        GfxPrint_SetColor(&printer, 230, 230, 255, a);
        for (k = 0; k < 2; k++) {
            if (sBannerSub[k][0] != '\0') {
                Ui_Print(&printer, (SCREEN_WIDTH - Ui_StrLen(sBannerSub[k]) * 8) / 2, 112 + k * 10, sBannerSub[k]);
            }
        }
    }
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    *gfxP = gfx;
}

