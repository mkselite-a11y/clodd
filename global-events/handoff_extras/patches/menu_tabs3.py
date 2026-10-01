def sub(s,a,b,c=1):
    assert a in s, a[:80]
    return s.replace(a,b,c)
P='/root/RainbowCarpenter/src/menu.c'
s=open(P).read()
s=sub(s,"#define VISIBLE_ROWS 12","#define VISIBLE_ROWS 13")
s=sub(s,"""#define PANEL_X 10
#define PANEL_W 218
#define PANEL_TOP 22
#define HEADER_H 34""","""#define PANEL_X 6
#define PANEL_W 308
#define PANEL_TOP 8
#define HEADER_H 30""")
s=sub(s,"#define PANEL_BOTTOM (FOOTER_Y + 22)","#define PANEL_BOTTOM (FOOTER_Y + 33)")
s=sub(s,"""    // Title
    if (gOpt[ID_THEME] == THEME_RAINBOW) {
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 6, GFXP_RAINBOW_ON "GLOBAL EVENTS MENU" GFXP_RAINBOW_OFF);
    } else {
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 6, "GLOBAL EVENTS MENU");
    }
    GfxPrint_SetColor(&printer, 255, 230, 120, 255);
    Ui_Print(&printer, PANEL_X + PANEL_W - 42, PANEL_TOP + 6, "v2.91");

    // Page name, e.g. "> Player Mods <"
    {
        const char* title = Menu_PageTitle(sPage);
        s32 len = Ui_StrLen(title);
        s32 n = 0;
        s32 k;

        pageLine[n++] = '>';
        pageLine[n++] = ' ';
        for (k = 0; (k < len) && (n < 28); k++) {
            pageLine[n++] = title[k];
        }
        pageLine[n++] = ' ';
        pageLine[n++] = '<';
        pageLine[n] = '\\0';
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        Ui_Print(&printer, PANEL_X + (PANEL_W - n * 8) / 2, PANEL_TOP + 20, pageLine);
    }""","""    // Header: the tab (or page) name, your Moon Marks in gold, and the tab strip.
    {
        const char* title = Menu_PageTitle(sPage);
        s32 len = Ui_StrLen(title);
        s32 n = 0;
        s32 k;
        char marks[24];

        if (Menu_IsTabRoot(sPage)) {
            pageLine[n++] = '<';
            pageLine[n++] = ' ';
        }
        for (k = 0; (k < len) && (n < 26); k++) {
            pageLine[n++] = title[k];
        }
        if (Menu_IsTabRoot(sPage)) {
            pageLine[n++] = ' ';
            pageLine[n++] = '>';
        }
        pageLine[n] = '\\0';
        if (gOpt[ID_THEME] == THEME_RAINBOW) {
            GfxPrint_SetColor(&printer, 255, 255, 255, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 5, GFXP_RAINBOW_ON "MOON MENU" GFXP_RAINBOW_OFF);
        } else {
            GfxPrint_SetColor(&printer, 220, 220, 255, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 5, "MOON MENU");
        }
        GfxPrint_SetColor(&printer, 255, 255, 255, 255);
        Ui_Print(&printer, PANEL_X + (PANEL_W - n * 8) / 2, PANEL_TOP + 5, pageLine);
        n = Ui_StrLen(V2_Value(ID_V2_MARKS, marks, sizeof(marks), &r, &g, &b));
        k = n;
        n = n + 0;
        marks[k] = '\\0';
        {
            char m2[32];
            s32 q = 0;
            s32 t;

            for (t = 0; marks[t] != '\\0'; t++) {
                m2[q++] = marks[t];
            }
            m2[q++] = ' ';
            m2[q++] = 'M';
            m2[q] = '\\0';
            GfxPrint_SetColor(&printer, 255, 215, 90, 255);
            Ui_Print(&printer, PANEL_X + PANEL_W - 10 - q * 8, PANEL_TOP + 5, m2);
        }
        // Tab strip: a dot per tab, the current one lit.
        if (sPage != MENU_V2_DRAFT) {
            s32 x0 = PANEL_X + (PANEL_W - (s32)ARRAY_COUNT(sTabs) * 12) / 2;

            for (k = 0; k < (s32)ARRAY_COUNT(sTabs); k++) {
                s32 on = (sTabs[k] == sTabs[sTab]);

                GfxPrint_SetColor(&printer, on ? 255 : 120, on ? 230 : 120, on ? 120 : 140, 255);
                Ui_Print(&printer, x0 + k * 12, PANEL_TOP + 17, on ? "o" : ".");
            }
            GfxPrint_SetColor(&printer, 150, 150, 180, 255);
            Ui_Print(&printer, PANEL_X + 10, PANEL_TOP + 17, "L");
            Ui_Print(&printer, PANEL_X + PANEL_W - 18, PANEL_TOP + 17, "R");
        }
    }""")
open(P,'w').write(s)
