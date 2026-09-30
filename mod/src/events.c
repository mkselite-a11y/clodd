#include "mf.h"

// ---------------------------------------------------------------------------
// Global Event scheduler. Roughly every three minutes one event fires for
// 30-45 seconds (both adjustable). Death ends the event with no payout.
// ---------------------------------------------------------------------------

EventRuntime gEv;
static u8 sScheduled = false;

static const s16 sNoEventScenes[] = {
    // Minigames and timed challenges: an event here would ruin the attempt.
    SCENE_SYATEKI_MIZU, SCENE_SYATEKI_MORI, SCENE_DEKUTES, SCENE_KOEPONARACE, SCENE_GORONRACE,
    SCENE_DOUJOU, SCENE_TOUGITES, SCENE_TAKARAYA, SCENE_BOWLING,
    // Boss arenas
    SCENE_MITURIN_BS, SCENE_HAKUGIN_BS, SCENE_SEA_BS, SCENE_INISIE_BS, SCENE_LAST_BS,
    SCENE_LAST_DEKU, SCENE_LAST_GORON, SCENE_LAST_ZORA, SCENE_LAST_LINK,
    // Intro and special places
    SCENE_OPENINGDAN, SCENE_LOST_WOODS, SCENE_OKUJOU, SCENE_SPOT00, SCENE_INSIDETOWER,
};

static const s16 sDungeonScenes[] = {
    SCENE_MITURIN, SCENE_HAKUGIN, SCENE_SEA, SCENE_INISIE_N, SCENE_INISIE_R, SCENE_CASTLE,
    SCENE_PIRATE, SCENE_KAIZOKU, SCENE_REDEAD, SCENE_HAKASHITA, SCENE_DANPEI, SCENE_SECOM,
};

s32 Events_Param(s32 id, s32 idx) {
    return gMf.events[id].p[idx];
}

void Events_ResetSettings(void) {
    s32 i;
    for (i = 0; i < NUM_EVENTS; i++) {
        gMf.events[i].enabled = true;
        gMf.events[i].weight = 5;
        gMf.events[i].p[0] = gEvents[i].defaults[0];
        gMf.events[i].p[1] = gEvents[i].defaults[1];
        gMf.events[i].p[2] = gEvents[i].defaults[2];
    }
}

static s32 SceneAllowsEvents(PlayState* play) {
    s32 i;
    Player* player = GET_PLAYER(play);

    for (i = 0; i < (s32)ARRAY_LEN(sNoEventScenes); i++) {
        if (play->sceneId == sNoEventScenes[i]) {
            return false;
        }
    }
    if (!gMf.settings.allowDungeons) {
        for (i = 0; i < (s32)ARRAY_LEN(sDungeonScenes); i++) {
            if (play->sceneId == sDungeonScenes[i]) {
                return false;
            }
        }
    }
    // Any running game timer means a timed quest or minigame is in progress.
    for (i = 0; i < TIMER_ID_MAX; i++) {
        if (gSaveContext.timerStates[i] != TIMER_STATE_OFF) {
            return false;
        }
    }
    if (player->rideActor != NULL) {
        return false; // on Epona
    }
    return true;
}

s32 Events_RandomEligible(PlayState* play, s32 dangerOnly) {
    s32 total = 0;
    s32 i;
    s32 roll;

    for (i = 0; i < NUM_EVENTS; i++) {
        const EventDef* e = &gEvents[i];
        if (!gMf.events[i].enabled || gMf.events[i].weight == 0) {
            continue;
        }
        if (dangerOnly && e->kind != EVK_DANGER) {
            continue;
        }
        if (e->eligible != NULL && !e->eligible(play)) {
            continue;
        }
        total += gMf.events[i].weight;
    }
    if (total == 0) {
        return -1;
    }
    roll = Rng_Range(0, total - 1);
    for (i = 0; i < NUM_EVENTS; i++) {
        const EventDef* e = &gEvents[i];
        if (!gMf.events[i].enabled || gMf.events[i].weight == 0) {
            continue;
        }
        if (dangerOnly && e->kind != EVK_DANGER) {
            continue;
        }
        if (e->eligible != NULL && !e->eligible(play)) {
            continue;
        }
        roll -= gMf.events[i].weight;
        if (roll < 0) {
            return i;
        }
    }
    return -1;
}

static void ScheduleNext(void) {
    s32 base = gMf.settings.eventIntervalSecs;
    s32 jitter = gMf.settings.eventJitterSecs;
    if (Curse_Active(CURSE_HASTE)) {
        base = base * 2 / 3;
    }
    gEv.nextIn = SEC(MF_MAX(30, base + Rng_Range(-jitter, jitter)));
    gEv.omenId = -1;
}

void Events_Start(PlayState* play, s32 id, s32 forced, const char* byFriend) {
    const EventDef* e;
    s32 minS;
    s32 maxS;

    if (id < 0 || id >= NUM_EVENTS) {
        return;
    }
    if (gEv.id >= 0) {
        Events_Stop(play, true);
    }
    e = &gEvents[id];
    gEv.id = id;
    gEv.forced = forced;
    gEv.failed = false;
    gEv.succeeded = false;
    gEv.timer = 0;
    gEv.hits = 0;
    gEv.score = 0;
    gEv.fa = 0.0f;
    gEv.fb = 0.0f;
    gEv.friendBonus = 0;
    gEv.notesAtStart = (s16)gOcarinaNotesPlayed;
    gEv.origin = GET_PLAYER(play)->actor.world.pos;
    gEv.byFriend[0] = '\0';
    if (byFriend != NULL) {
        Str_Copy(gEv.byFriend, byFriend, sizeof(gEv.byFriend));
        gEv.friendBonus = 25; // surviving a friend's meddling pays extra
    }
    minS = MF_MIN(gMf.settings.eventMinSecs, gMf.settings.eventMaxSecs);
    maxS = MF_MAX(gMf.settings.eventMinSecs, gMf.settings.eventMaxSecs);
    gEv.duration = SEC(Rng_Range(minS, maxS));
    if (Curse_Active(CURSE_LONGNIGHT)) {
        gEv.duration += SEC(15);
    }
    if (e->start != NULL) {
        e->start(play);
    }
    {
        char sub[64];
        sub[0] = '\0';
        if (byFriend != NULL) {
            Str_Cat(sub, byFriend, sizeof(sub));
            Str_Cat(sub, " sends: ", sizeof(sub));
        }
        Str_Cat(sub, e->counter, sizeof(sub));
        Hud_Banner(e->name, sub);
    }
    Mf_Sfx(e->kind == EVK_DANGER ? NA_SE_SY_STALKIDS_PSYCHO : NA_SE_SY_TRE_BOX_APPEAR);
}

void Events_Stop(PlayState* play, s32 success) {
    const EventDef* e;
    s32 id = gEv.id;

    if (id < 0) {
        return;
    }
    e = &gEvents[id];
    gEv.id = -1;
    if (e->end != NULL) {
        e->end(play, success);
    }
    if (success && !gRt.playerDead) {
        s32 base = 0;
        if (e->kind == EVK_HARMLESS) {
            base = 2;
        } else if (e->kind == EVK_DANGER) {
            base = 3 + e->danger * 2;
            if (gEv.failed) {
                base = MF_MAX(1, base / 2);
            }
        }
        if (base > 0) {
            if (gMf.curseId < NUM_CURSES && !gMf.curseWarded) {
                base = base * 115 / 100; // enduring under a curse pays a little more
            }
            base = base * MF_MAX(0, 100 + gEv.friendBonus) / 100;
            Marks_Add(Marks_Scale(MF_MAX(1, base)), e->name);
        }
        gMf.eventsSurvived++;
        World_Logf2("Survived: ", e->name, NULL);
    } else {
        gMf.eventsFailed++;
        World_Logf2("Fell to: ", e->name, NULL);
    }
    ScheduleNext();
    Save_MarkDirty();
}

void Events_OnDeath(PlayState* play) {
    if (gEv.id >= 0) {
        Events_Stop(play, false);
    }
}

void Events_OnSceneChange(PlayState* play) {
    // Scene-bound hazards re-anchor on the player in the new area.
    if (gEv.id >= 0) {
        gEv.origin = GET_PLAYER(play)->actor.world.pos;
        if (gEv.id == 13) { // Plague Fog: escaping the area is the counter
            gEv.succeeded = true;
        }
    }
}

// Checks that must run even while a textbox or the ocarina is up.
static void PassiveChecks(PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (gEv.id == 21 && (gOcarinaNotesPlayed - gEv.notesAtStart) >= 3) {
        gEv.succeeded = true; // Dissonance: answered with music
    }
    if (gEv.id == 24 && (player->stateFlags1 & PLAYER_STATE1_TALKING) && player->talkActor != NULL &&
        player->talkActor->category == ACTORCAT_NPC) {
        gEv.succeeded = true; // Lonely Moon: found company
    }
}

void Events_Update(PlayState* play) {
    if (!gMf.settings.eventsEnabled && gEv.id < 0) {
        return;
    }
    if (gEv.id < 0) {
        if (!sScheduled) {
            sScheduled = true;
            ScheduleNext();
        }
        if (!gRt.gameplayOk) {
            return;
        }
        if (gEv.nextIn > 0) {
            gEv.nextIn--;
            // Pre-roll the next event 20s ahead so an Omen Scroll can reveal it.
            if (gEv.nextIn == SEC(20) || (gEv.omenId < 0 && (gRt.frame % 20) == 0)) {
                gEv.omenId = Events_RandomEligible(play, false);
            }
            return;
        }
        if (!SceneAllowsEvents(play) || gRt.framesSinceSceneStart < SEC(3)) {
            gEv.nextIn = SEC(10);
            return;
        }
        {
            s32 id = gEv.omenId;
            if (id < 0 || (gEvents[id].eligible != NULL && !gEvents[id].eligible(play)) || !gMf.events[id].enabled) {
                id = Events_RandomEligible(play, false);
            }
            if (id < 0) {
                gEv.nextIn = SEC(20);
                return;
            }
            Events_Start(play, id, false, NULL);
        }
        return;
    }

    PassiveChecks(play);
    if (gEv.succeeded) {
        Events_Stop(play, true);
        return;
    }
    if (!gRt.gameplayOk) {
        return;
    }
    gEv.timer++;
    if (gEvents[gEv.id].update != NULL) {
        gEvents[gEv.id].update(play);
    }
    if (gEv.id >= 0 && (gEv.succeeded || gEv.timer >= gEv.duration)) {
        Events_Stop(play, true);
    }
}

void Events_Draw3D(PlayState* play) {
    if (gEv.id >= 0 && gEvents[gEv.id].draw3d != NULL) {
        gEvents[gEv.id].draw3d(play);
    }
}

void Events_Draw2D(PlayState* play) {
    const EventDef* e;
    char buf[48];
    s32 remain;
    s32 w;
    MfColor bg = { 10, 0, 25, 170 };
    MfColor bar = { 200, 80, 255, 230 };
    MfColor hint = { 255, 235, 150, 255 };

    if (gEv.id < 0 || gRt.menuOpen) {
        if (!gRt.menuOpen && gMf.settings.eventsEnabled && gEv.id < 0 && gEv.nextIn > 0 && gEv.nextIn < SEC(10)) {
            MfColor c = { 255, 120, 120, 255 };
            Draw2D_TextCentered(8, c, "The moon stirs...");
        }
        return;
    }
    e = &gEvents[gEv.id];
    if (e->kind == EVK_BENEFIT) {
        bar.r = 90;
        bar.g = 230;
        bar.b = 140;
    } else if (e->kind == EVK_HARMLESS) {
        bar.r = 120;
        bar.g = 180;
        bar.b = 255;
    }
    Draw2D_Rect(8, 4, SCREEN_WIDTH - 16, 26, bg);
    remain = MF_MAX(0, gEv.duration - gEv.timer);
    w = (SCREEN_WIDTH - 20) * remain / MF_MAX(1, gEv.duration);
    Draw2D_Rect(10, 27, w, 2, bar);
    buf[0] = '\0';
    Str_Cat(buf, e->name, sizeof(buf));
    Str_Cat(buf, "  ", sizeof(buf));
    Str_CatInt(buf, remain / FPS + 1, sizeof(buf));
    Str_Cat(buf, "s", sizeof(buf));
    Draw2D_TextCentered(7, (e->kind == EVK_DANGER) ? Mf_Rainbow(gRt.frame * 5, 255) : bar, buf);
    Draw2D_TextCentered(17, hint, e->counter);
    if (e->draw2d != NULL) {
        e->draw2d(play);
    }
}
