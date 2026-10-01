def sub(s,a,b,c=1):
    assert a in s, a[:80]
    return s.replace(a,b,c)
P='/root/RainbowCarpenter/src/menu.c'
s=open(P).read()
a=s.index("void Menu_ShowToast(const char* text) {")
b=s.index("void Menu_OpenV2(PlayState* play, s32 which) {")
notes=open('/tmp/claude-0/-home-claude/2c5d97f4-e068-50d0-96e9-b6f5034e87e0/scratchpad/patches/notes.c').read()
s=s[:a]+notes+s[b:]
s=sub(s,"""    if (sToastTimer > 0) {
        sToastTimer--;
    }
    if ((sToastTimer <= 0) && (sToastQueued > 0)) {
        s32 k;

        Toast_Copy(sToastBuf, sToastQueue[0]);
        for (k = 1; k < sToastQueued; k++) {
            Toast_Copy(sToastQueue[k - 1], sToastQueue[k]);
        }
        sToastQueued--;
        sToastText = sToastBuf;
        sToastTimer = TOAST_FRAMES;
    }
""","""    Notes_Tick();
""")
s=sub(s,"""    drawEvents = !gMenuOpen && Events_WantsDraw(play);
    if (!gMenuOpen && (sToastTimer <= 0) && !drawEvents) {
        return;
    }""","""    drawEvents = !gMenuOpen && Events_WantsDraw(play);
    if (!gMenuOpen && !Notes_Any() && !drawEvents) {
        return;
    }""")
s=sub(s,"""    if ((sToastTimer > 0) && (sToastText != NULL)) {
        Menu_DrawToast(play, &gfx);
    }""","""    if (gMenuOpen && (sToastTimer > 0) && (sToastText != NULL)) {
        Menu_DrawToast(play, &gfx);
    }
    if (!gMenuOpen && Notes_Any()) {
        Notes_Draw(play, &gfx);
    }""")
s=sub(s,"""    for (i = 0; i < V2_ShopServiceCount(); i++) {
        sV2ServiceEntries[sV2ServiceCount++] = ID_V2_SHOP_FIRST + V2_ShopItemCount() + i;
    }""","""    for (i = 0; i < V2_ShopServiceCount(); i++) {
        if (!V2_ServiceHidden(i)) {
            sV2ServiceEntries[sV2ServiceCount++] = ID_V2_SHOP_FIRST + V2_ShopItemCount() + i;
        }
    }""")
open(P,'w').write(s)
