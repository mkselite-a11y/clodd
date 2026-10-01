def sub(s,a,b,c=1):
    assert a in s, a[:80]
    return s.replace(a,b,c)
P='/root/RainbowCarpenter/src/menu.c'
s=open(P).read()
i=s.index("    // Footer\n    GfxPrint_SetColor(&printer, 170, 170, 170, 255);")
j=s.index("    gfx = GfxPrint_Close(&printer);",i)
s=s[:i]+'''    // Footer: what the highlighted row means, over up to three lines.
    GfxPrint_SetColor(&printer, 170, 170, 170, 255);
    {
        const char* desc = (count > 0) ? Events_InfoDesc(entries[cursor]) : NULL;

        if (desc != NULL) {
            char lines[3][40];
            s32 line = 0;
            s32 len = 0;
            s32 k = 0;

            lines[0][0] = lines[1][0] = lines[2][0] = '\\0';
            while ((desc[k] != '\\0') && (line < 3)) {
                s32 wordLen = 0;

                while ((desc[k + wordLen] != '\\0') && (desc[k + wordLen] != ' ')) {
                    wordLen++;
                }
                if ((len > 0) && (len + 1 + wordLen > 36)) {
                    line++;
                    len = 0;
                    if (line >= 3) {
                        break;
                    }
                }
                if (len > 0) {
                    lines[line][len++] = ' ';
                }
                while ((wordLen > 0) && (len < 36)) {
                    lines[line][len++] = desc[k++];
                    wordLen--;
                }
                k += wordLen;
                lines[line][len] = '\\0';
                while (desc[k] == ' ') {
                    k++;
                }
            }
            GfxPrint_SetColor(&printer, 210, 170, 255, 255);
            for (k = 0; k < 3; k++) {
                Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 2 + k * 10, lines[k]);
            }
        } else {
            Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 2, "A: Select   B: Back");
            Ui_Print(&printer, PANEL_X + 8, FOOTER_Y + 12, "L / R: switch tabs");
        }
        GfxPrint_SetColor(&printer, 120, 120, 150, 255);
        Ui_Print(&printer, PANEL_X + PANEL_W - 42, FOOTER_Y + 22, "v3.0");
    }

'''+s[j:]
s=sub(s,"""    // Text.
    bzero(&printer, sizeof(printer));""","""    // Nemesis stat bars: 10 points fills the bar, and from there it glows rainbow.
    for (i = 0; (i < VISIBLE_ROWS) && (scroll + i < count); i++) {
        s32 id = entries[scroll + i];

        if ((id >= ID_V2_NEMROW_FIRST + 8) && (id < ID_V2_NEMROW_FIRST + 12)) {
            s32 val = V2_NemStatValue(id - ID_V2_NEMROW_FIRST - 8);
            s32 rowY = LIST_TOP + i * ROW_H;
            s32 bx = PANEL_X + 110;
            s32 bw = 140;
            s32 fill = bw * CLAMP(val, 0, 10) / 10;
            u8 cr, cg, cb;

            if (val < 0) {
                continue;
            }
            if (val >= 10) {
                HueToRgb((play->gameplayFrames * 9 + i * 50) % 360, &cr, &cg, &cb);
            } else {
                Menu_ThemeColor(play, 0, &cr, &cg, &cb);
            }
            gfx = Ui_DrawRect(gfx, bx, rowY + 1, bx + bw, rowY + 7, 30, 30, 40, 220);
            gfx = Ui_DrawRect(gfx, bx, rowY + 1, bx + fill, rowY + 7, cr, cg, cb, 240);
        }
    }

    // Text.
    bzero(&printer, sizeof(printer));""")
open(P,'w').write(s)
