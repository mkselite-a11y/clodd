#include "mf.h"

Runtime gRt;

static s32 sWorldTickFrames = 0;
static s16 sHealthBeforeUpdate = 0;
static u8 sFrameAdvByUs = false;
static u8 sLHeld = false;
static u8 sNearInteractable = 0; // 1 = carpenter, 2 = owl statue

void Town_SetNear(s32 which) {
    sNearInteractable = which;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

RECOMP_CALLBACK("*", recomp_on_init) void Mf_OnInit(void) {
    s32 i;
    u8* p = (u8*)&gRt;
    for (i = 0; i < (s32)sizeof(Runtime); i++) {
        p[i] = 0;
    }
    gRt.sceneId = -1;
    gRt.zone = ZONE_NONE;
    gRt.lastZone = 0;
    gEv.id = -1;
    Actors_OnInit();
    mfn_init();
    Friends_Init();
}

RECOMP_CALLBACK("*", recomp_after_play_init) void Mf_AfterPlayInit(PlayState* play) {
    gRt.play = play;
    gRt.sceneChanged = true;
    gRt.framesSinceSceneStart = 0;
}

// Scene objects are loaded while the scene header is processed. This runs right
// before the first room is loaded, which is the one safe moment to add extra
// persistent objects (nemesis / bounty target models) for this scene.
RECOMP_HOOK("Play_InitEnvironment") void Mf_BeforeInitEnvironment(PlayState* play, s16 skyboxId) {
    gRt.play = play;
    if (gRt.loaded) {
        Actors_PreloadForScene(play);
    }
}

// ---------------------------------------------------------------------------
// Per-frame logic
// ---------------------------------------------------------------------------

static void ReadConfig(void) {
    gRt.difficultyIdx = (u8)recomp_get_config_u32("master_difficulty");
    gRt.debugMenu = (u8)recomp_get_config_u32("debug_menu");
}

static s32 IsGameplayOk(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return false;
    }
    if (IS_PAUSED(&play->pauseCtx) || gRt.menuOpen) {
        return false;
    }
    if (play->msgCtx.msgMode != MSGMODE_NONE) {
        return false;
    }
    if (Play_InCsMode(play) || (player->csAction != PLAYER_CSACTION_NONE)) {
        return false;
    }
    if ((play->transitionTrigger != TRANS_TRIGGER_OFF) || (play->transitionMode != TRANS_MODE_OFF)) {
        return false;
    }
    if (play->gameOverCtx.state != GAMEOVER_INACTIVE) {
        return false;
    }
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }
    return true;
}

static void CaptureInput(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);

    gRt.btnCur = in->cur.button;
    gRt.btnPress = in->press.button;
    gRt.stickX = in->rel.stick_x;
    gRt.stickY = in->rel.stick_y;
}

static void ClearButtons(Input* in, u16 mask) {
    in->cur.button &= ~mask;
    in->press.button &= ~mask;
    in->prev.button &= ~mask;
}

// Moon Pouch controls: hold L, then D-pad / C left-right to cycle, down to use.
// While L is held, those buttons are hidden from the game so items don't fire.
static void HandleLCombo(PlayState* play) {
    Input* in = CONTROLLER1(&play->state);
    const u16 comboMask = BTN_DLEFT | BTN_DRIGHT | BTN_DDOWN | BTN_DUP | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN;

    if (IS_PAUSED(&play->pauseCtx)) {
        sLHeld = false;
        return;
    }
    if (gRt.btnCur & BTN_L) {
        if (!sLHeld) {
            sLHeld = true;
            gRt.lHeldCombo = false;
        }
        // Left/right only step through items you actually own.
        if (gRt.btnPress & (BTN_DLEFT | BTN_CLEFT)) {
            gMf.pouchSel = Pouch_NextOwned(gMf.pouchSel, -1);
            gRt.lHeldCombo = true;
            Mf_Sfx(NA_SE_SY_CURSOR);
        }
        if (gRt.btnPress & (BTN_DRIGHT | BTN_CRIGHT)) {
            gMf.pouchSel = Pouch_NextOwned(gMf.pouchSel, 1);
            gRt.lHeldCombo = true;
            Mf_Sfx(NA_SE_SY_CURSOR);
        }
        if (gRt.btnPress & (BTN_DDOWN | BTN_CDOWN)) {
            gRt.lHeldCombo = true;
            if (gRt.gameplayOk && gMf.pouch[gMf.pouchSel] > 0) {
                Pouch_Use(play, gMf.pouchSel);
            }
        }
        if (gRt.btnPress & BTN_DUP) {
            gRt.lHeldCombo = true;
        }
        ClearButtons(in, comboMask);
    } else if (sLHeld) {
        sLHeld = false;
        if (!gRt.lHeldCombo && gRt.gameplayOk && sNearInteractable != 0) {
            Menu_Open(sNearInteractable == 2 ? PAGE_BOUNTY : PAGE_MAIN);
        }
    }
}

static void OnSceneChange(PlayState* play) {
    s32 zone = World_ZoneForScene(play->sceneId);

    gRt.prevSceneId = gRt.sceneId;
    gRt.sceneId = play->sceneId;
    gRt.zone = zone;
    if (zone != ZONE_NONE) {
        gRt.lastZone = zone;
    }
    gRt.lastAttacker = NULL;
    Events_OnSceneChange(play);
    Nemesis_OnSceneChange(play);
    Bounty_OnSceneChange(play);
}

RECOMP_CALLBACK("*", recomp_on_play_update) void Mf_OnPlayUpdate(PlayState* play) {
    Player* player = GET_PLAYER(play);
    Input* in = CONTROLLER1(&play->state);
    s32 dead;

    gRt.play = play;
    // Title screen demo and other non-gameplay modes: stay out of the way.
    if (gSaveContext.gameMode != GAMEMODE_NORMAL || gSaveContext.fileNum == 0xFF) {
        return;
    }
    gRt.frame++;
    gRt.framesSinceSceneStart++;

    if ((gRt.frame % SEC(5)) == 1) {
        ReadConfig();
    }
    Save_Update(play);
    if (!gRt.loaded || player == NULL) {
        return;
    }

    if (gRt.sceneChanged) {
        gRt.sceneChanged = false;
        OnSceneChange(play);
    }

    CaptureInput(play);
    gRt.gameplayOk = IsGameplayOk(play);

    // Death tracking
    dead = (player->stateFlags1 & PLAYER_STATE1_DEAD) || (play->gameOverCtx.state != GAMEOVER_INACTIVE);
    if (dead && !gRt.playerDead) {
        gRt.playerDead = true;
        gMf.deaths++;
        Events_OnDeath(play);
        Nemesis_OnPlayerDeath(play, gRt.lastAttacker);
        Save_MarkDirty();
    } else if (!dead && gRt.playerDead && gSaveContext.save.saveInfo.playerData.health > 0) {
        gRt.playerDead = false;
    }

    // Menu takes over the controller and freezes the world.
    if (gRt.menuOpen) {
        if (!play->frameAdvCtx.enabled) {
            play->frameAdvCtx.enabled = true;
            sFrameAdvByUs = true;
        }
        Menu_Update(play);
        in->cur.button = 0;
        in->press.button = 0;
        in->rel.button = 0;
        in->cur.stick_x = in->cur.stick_y = 0;
        in->rel.stick_x = in->rel.stick_y = 0;
        return;
    } else if (sFrameAdvByUs) {
        play->frameAdvCtx.enabled = false;
        sFrameAdvByUs = false;
    }

    // Frostbrand chill: halve the stick before the player reads it.
    if (gChillTimer > 0) {
        in->rel.stick_x /= 2;
        in->rel.stick_y /= 2;
        in->cur.stick_x /= 2;
        in->cur.stick_y /= 2;
    }

    Curse_Update(play);
    Town_Update(play);
    Ocarina_Update(play);
    HandleLCombo(play);
    Hud_Update();

    Events_Update(play);
    if (gRt.gameplayOk) {
        Pouch_Update(play);
        Nemesis_Update(play);
        Bounty_Update(play);
        Actors_Update(play);

        sWorldTickFrames++;
        if (sWorldTickFrames >= SEC(45)) {
            sWorldTickFrames = 0;
            gMf.worldSecs += 45;
            World_Tick(play);
        }
    }
    Friends_Update(play);

    sHealthBeforeUpdate = gSaveContext.save.saveInfo.playerData.health;
}

RECOMP_CALLBACK("*", recomp_after_play_update) void Mf_AfterPlayUpdate(PlayState* play) {
    s16 now = gSaveContext.save.saveInfo.playerData.health;
    s32 lost;

    if (!gRt.loaded || gRt.menuOpen) {
        return;
    }
    lost = sHealthBeforeUpdate - now;
    gRt.damageThisFrame = (lost > 0) ? lost : 0;

    // Protective pouch buffs apply to damage from any source.
    if (lost > 0 && !gRt.playerDead) {
        s32 refund = 0;
        if (Pouch_BuffActive(PI_MOONSHIELD)) {
            refund = lost;
        } else if (Pouch_BuffActive(PI_MOONSTEEL_WARD)) {
            refund = lost / 2;
        }
        if (refund > 0) {
            gSaveContext.save.saveInfo.playerData.health += refund;
        }
    }
    // Second Wind: catches a lethal blow.
    if (gSaveContext.save.saveInfo.playerData.health <= 0 && gMf.pouch[PI_SECOND_WIND] > 0 && !gRt.playerDead) {
        MfColor c = { 120, 255, 160, 255 };
        gMf.pouch[PI_SECOND_WIND]--;
        gSaveContext.save.saveInfo.playerData.health = 16 * 4;
        Hud_Notify("Second Wind! The moon is not done with you.", c);
        Mf_Sfx(NA_SE_SY_HP_RECOVER);
        Save_MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Note: return hooks don't receive the hooked function's arguments (the
// registers hold whatever they had at the return), so use the saved PlayState.
RECOMP_HOOK_RETURN("Actor_DrawAll") void Mf_AfterActorDrawAll(void) {
    PlayState* play = gRt.play;
    if (!gRt.loaded || play == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return;
    }
    Draw3D_Begin(play);
    Events_Draw3D(play);
    Draw3D_End();
}

RECOMP_HOOK_RETURN("Play_Draw") void Mf_AfterPlayDraw(void) {
    PlayState* play = gRt.play;
    if (!gRt.loaded || play == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return;
    }
    Draw2D_Begin(play->state.gfxCtx);
    Events_Draw2D(play);
    Bounty_DrawHud(play);
    Hud_Draw(play);
    Pouch_DrawHud(play);
    Menu_Draw(play);
    Draw2D_End();
}
