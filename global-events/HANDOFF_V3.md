# Global Events: handoff back to the original builder (from 3.1.3)

You handed over V3 with Stages 1-3 done and Stage 4 (Friend Link) not started.
Stage 4 is done, and the user has played several builds since. Current version:
**3.1.3**. V3_PLAN.md is still the agreed spec; its progress log is up to date
through Stage 4.

## Rules the user set (keep them)

- **Only build when the user says "go".** Source changes can be made and committed in between.
- **Versioning:** bump the third digit for every build (3.1.3 shipped, next is 3.1.4). Set it in both mod.toml and mod_cheats.toml.
- **Every file that goes in the mods folder starts with `CUSTOM_`:** `CUSTOM_global_events_v3.nrm`, `CUSTOM_global_events_cheats_v3.nrm`, `CUSTOM_GlobalEventsRemote.dll`, and the DLL's own `CUSTOM_GlobalEventsRemote.txt` / `CUSTOM_GlobalEventsSaves.txt` (the DLL copies the old unprefixed .txt files over on first run).
- **Release zip layout:** `README.md`, `Put in mods folder/` (both .nrm + the DLL), `Put in Cloudflare/worker.js`.
- **Writing style for anything the user reads:** no em dashes; no "genuinely", "honestly" or similar filler.
- Ask the user when a request is ambiguous; they're happy to answer.

## Building

```
git submodule update --init --recursive
RECOMP_MOD_TOOL=/path/to/RecompModTool sh build_both.sh
```
build_both.sh no longer has /root paths. It builds both .nrm variants, the DLL
(`DLL_CC`, default x86_64-w64-mingw32-gcc), regenerates `remote/relay/tables.json`
and `remote/native/ge_scenes.h` from the game source (`sync_tables.py`), and
rebuilds `worker.js` (`build_relay.py`). Everything lands in `dist/`.

## What changed since your handoff

### Friend Link (Stage 4), game side: src/ev_remote.inc
- **Friends:** list from the relay (`ger_friends`), join/leave pop-ups, per-friend mode on the Friends tab (rows `ID_LNK_FR_FIRST..+11`): Full, Helpful (non-harmful only, `Link_Harmful`), Blocked. Saved with `ger_store("friend_modes")`.
- **Steering:** `Steer_Get(kind)` used by Moon Tether (ev_v3.inc), Moonfall and Searchlights (events.c). The friend's mark holds for 5 s after their last steer.
- **Placements:** in the first 15 s of Treasure Hunt (`Treasure_PlaceAt`), Procession (`Proc_PlaceAt`), Dowsing, Scythe and Pressure Seals (`V3_Place` in ev_v3.inc).
- **Surprises:** `Sur_*`. A friend hides an event, ambush, bomb drop, rupoor rain or the Nemesis in any of the 77 zones, optionally at x,z. It's settled away from exits (`World_ExitDist`), shimmers faintly, and fires when Link walks into it. The pop-up says who, never where.
- **Radar status lines:** pos, scene, acts, fmodes, evplace, steer, surprises, ap, apslot.
- **Live map scan:** `LinkMap_Update` sends floor blocks (16x16 cells of 128 units) via `ger_map`. Superseded by the ROM maps below; the relay ignores these once ROM maps exist.
- **Pranks:** `Prank_Sound` (the music dips with `Audio_SetMainBgmVolume`; short sounds repeat), cuccos (via `Linger_Add`, gone after 30 s), disco, confetti.
- **Beam:** "Look here" draws a free column of sparkles at the friend's click.
- **Forced Ambush** (`forced_ambush|pool|count`):
  - While Link is held, one command is polled into `sStash`. If it's a forced ambush, the mod menu closes, the pause menu closes the way Start does (only from `PAUSE_STATE_MAIN` + idle), and the ocarina is put away the way B does.
  - The mutator draft (`Menu_IsDraft`) is never forced shut; the ambush lands after the pick.
- **Removed:** zap and mut_add. Nemesis rename is silent.
- **Archipelago room window:** Friends tab > "Archipelago room" opens a recompui window (address + slot), saved with `ger_store("ap_room")` and sent as status lines `ap` / `apslot`.

### Game fixes after testing
- **Next-event countdown:** runs in real time (osGetTime) while paused or in menus.
- **Searchlights backup:** spawns close (`sSpawnNear`) and stays 10 s after the event ends (`Linger_Keep`).
- **Stuck Nemesis:** `Ev_StepToward`, Speed points and Enraged no longer move airborne enemies, and a watchdog unsticks a Nemesis stuck mid-jump (a Dinolfos got stuck before). This is a best guess; it was never reproduced.
- **Big Poes:** retired from every pool (`POOL_BIGPOE`, removed from sHuntPool, sRarePool, the Nemesis mini-boss list and the pool menu).
- **Menu changes:**
  - Carpenter settings page removed from Settings.
  - Call / Ban an event moved under Moon Services.
  - Interval, duration, features, pool and critters moved to the top of Choose Events.

### DLL: remote/native/ge_remote.c + ge_rommap.c
- **Exports:** ger_init, ger_state, ger_poll, ger_status, ger_store, ger_fetch, ger_friends, ger_map. The status buffer is 4096.
- **ROM maps** (`ge_rommap.c`):
  - **Source:** once per relay, the DLL finds the player's ROM: `rom=` in the .txt, else `%LOCALAPPDATA%\Zelda64Recompiled`, else the folders above the mods folder. The user confirmed it finds it.
  - **Reading:** it unpacks each zone's scene file (dmadata index from `ge_scenes.h`, Yaz0).
  - **Walkable only:** it floods outward from the spawn list (scene cmd 0x00) and doors (0x0E) through floor layers. Allowed: step up 70, out of water 100, drop 800. A wall crossing at chest height blocks.
  - **Upload:** blocks of only reachable ground go to `/api/mapbulk` (reset first, then `done=1`, kv `maps_rom_v2`).
  - **Status:** shown as `maprom=` in the status.
  - **Tested** on a synthetic walled area only. If an area misses walkable parts, the step-up limit or the wall test is the first place to look.

### Relay and panel: remote/relay/
- **Identity:** each friend has a hidden browser id (X-Friend-Id) plus a unique 5-letter name (X-Friend-Name), their own points, cooldowns and fall bonus (`bonus_total` / `bonus:<fid>`), and their own refunds and wager payouts.
- **Modes:** read from the status `fmodes`. Blocked friends get the scary moon page and can't rename their way out. Helpful friends can't send `HARMFUL` moves.
- **Login:** the room key and name are asked every visit; nothing but the id is stored.
- **Endpoints:**
  - `/api/map` (live blocks, GET per scene or a list of scenes)
  - `/api/mapbulk` (ROM maps)
  - `/api/ap` (the Archipelago room: from the game's status first, else typed on the page)
- **Commands:** place, steer, surprise, beam, cuccos, disco, confetti, forced_ambush. "random" (Surprise me) is gone.
- **Prices:** a surprise never costs less than doing the same thing directly.
- **Panel tabs:** Events (now including curse + mutator draft), Combat (Nemesis + bounties), **Map**, Gifts & Pranks, Info.
- **The Map tab:**
  - The live view around Link sits beside the Termina picker, and the temples are their own nodes.
  - Clicking an area browses its map; there, "Surprise spot" picks an exact spot.
  - Zoom works with the mouse wheel, pinch, + and -, and "Whole area".
  - Air strike / Ambush are Auto or Aim. The Forced Ambush card is there too.
- **Heatmap:** the Archipelago spoiler heatmap, off by default every visit. It connects to the room as a read-only Tracker, scouts missing locations, and colors zones by progression items. `ap_locations.json` maps the randomizer's 397 locations to zones; it was built from the mm_recomp apworld's regions plus name keywords.
- **Action button:** a glowing button at the top appears only for time-limited things (place, steer, the next event is under 30 s).
- **Tables:** `sync_tables.py` builds tables.json from the C source: events, traits, mutators, pouch, curses (with the game's own descriptions), pools (sorted easy to brutal in the page), 77 zones with layout, scene ids and aploc. It also writes ge_scenes.h. Prank sound names in tables.json must stay in step with `sPrankSfx`.

### Not done / on purpose
- **Auto-reading the randomizer's saved AP connection file:** blocked by a safety check, because it would have sent the room password to the relay. The user chose the in-game window instead. Don't re-add the file reading.
- **Password-protected AP rooms:** not supported by the heatmap.
- **Moonfall Mayhem** (the other mod in this repo, mod/): has unbuilt source fixes waiting on the user's go.

## Where things are tested
Nothing new was ever run in the real game by me. The user has played 3.0 through 3.1.2 and reported issues; everything above that came from their reports is fixed in source and built in 3.1.3. The relay was tested in Node against a SQLite stand-in for D1, the panel in headless Chromium, and the heatmap against a stand-in Archipelago server.
