#include "mf.h"

// Watches notes played on the ocarina without touching the audio code's state.
// C-Up C-Up C-Down C-Down opens the Moon Menu from anywhere. No vanilla song
// contains that run of notes, so it can't collide with a real song.

s32 gOcarinaNotesPlayed = 0;

static u8 sHistory[8];
static s32 sLastPos = -1;

void Ocarina_Close(PlayState* play) {
    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
    play->msgCtx.ocarinaMode = OCARINA_MODE_END;
    Message_CloseTextbox(play);
}

void Ocarina_Update(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;
    OcarinaStaff* staff;
    s32 i;

    if (msgCtx->msgMode != MSGMODE_OCARINA_PLAYING || msgCtx->ocarinaStaff == NULL) {
        sLastPos = -1;
        return;
    }
    staff = msgCtx->ocarinaStaff;
    if (sLastPos < 0) {
        sLastPos = staff->pos;
        for (i = 0; i < 8; i++) {
            sHistory[i] = OCARINA_BTN_INVALID;
        }
        return;
    }
    if (staff->pos == sLastPos || staff->pos == 0) {
        return;
    }
    sLastPos = staff->pos;
    gOcarinaNotesPlayed++;

    for (i = 0; i < 7; i++) {
        sHistory[i] = sHistory[i + 1];
    }
    sHistory[7] = staff->buttonIndex;

    if (sHistory[4] == OCARINA_BTN_C_UP && sHistory[5] == OCARINA_BTN_C_UP && sHistory[6] == OCARINA_BTN_C_DOWN &&
        sHistory[7] == OCARINA_BTN_C_DOWN) {
        for (i = 0; i < 8; i++) {
            sHistory[i] = OCARINA_BTN_INVALID;
        }
        Ocarina_Close(play);
        Mf_Sfx(NA_SE_SY_TRE_BOX_APPEAR);
        Menu_Open(PAGE_MAIN);
    }
}
