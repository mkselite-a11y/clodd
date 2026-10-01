# Global Events V3: handoff (work in progress)

Read V3_PLAN.md first: it is the agreed spec plus a progress log.

## State
- Stages 1-3 are written and compile (`make` in RainbowCarpenter). Nothing has been tested in game.
  - Stage 1 events: src/events.c, src/ev_v11.inc, new src/ev_v3.inc (19 events ported from Moonfall Mayhem + Pop Quiz questions).
  - Stage 2 Moon systems: src/ev_v2.inc (77 zones, World_* system, traits, Nemesis, shop, payouts, save format 4).
  - Stage 3 UI: src/menu.c (tabs, pop-ups, banners), owl glow in src/rainbow_carpenter.c, Press L in ev_v2.inc Board_Update.
- Stage 4 Friend Link is DONE (see V3_PLAN.md progress log). What it covered:
  - Game side: src/ev_remote.inc needs Link_FriendsOnline() (it is declared in ev_v2.inc and used by the Overview tab; currently undefined), per-friend modes, radar status fields, map chunks, place/steer/surprise commands, rename without pop-up.
  - DLL: remote/native/ge_remote.c needs a `friends|` line pass-through and a map POST.
  - Web: remote/relay/* (worker + panel). The full protocol spec I planned is at the bottom of this file.
- Then: version 3.0.0 in mod.toml/mod_cheats.toml, build_both.sh, review pass, package with README.

## Build
- `make` in the mod folder (N64Recomp mod template), RecompModTool for .nrm, see build_both.sh.
- DLL: llvm-mingw `x86_64-w64-mingw32-clang -O2 -Wall -shared -o GlobalEventsRemote.dll ge_remote.c -lwinhttp -s`.
- Relay: `python3 build_relay.py` embeds tables.json and panel.html into worker.js.

## Friend Link protocol plan (Stage 4)
Game status additions (key=value lines): pos=x,y,z,yaw,camyaw; scene=id; acts=k:x:z;... (e enemy, n npc, b bounty, N nemesis, s surprise, max 24); fmodes=NAME:m,... (0 blocked, 1 helpful, 2 full); evplace=evId:secondsLeft; steer=0|1|2 (tether, moonfall, searchlight); surprises=zone:kind:from;...
Map chunks: POST /api/map with scene, bx, bz, base, cells (16x16 cells of 128 units; '.' none, '~' water, else chr(0x30+clamp(round((y-base)/40)+32,0,63))). GET /api/map?scene=N returns blocks.
Pull adds a line `friends|NAME,NAME` (online in last 30 s).
Commands (id|type|a|b|c|text|from): remove zap, mut_add; panel merges ev_now+ev_soon, curse+curse_set, bounty+wanted; nem_name cheap and silent; airstrike a,b = x,z, c = 1 targeted; ambush text "x,z;x,z"; place|ev|x|z|extra (treasure 9, dowsing 38, seals 55, scythe 42, procession 23), 15 s window after the event starts; steer|kind|x|z (live, auto after 5 s idle); surprise|zone|kind|arg|"x,z" (kinds: event, ambush, bomb, rupoors, nemesis), pop-up "NAME has placed a surprise...", faint shimmer, never within 400 units of an exit.
Per-friend points and cooldowns keyed by a hidden browser key + name; Blocked friends see a frightening dead page.

## Versioning (from the user)
Bump the third digit for each new build: 3.1.0 shipped, so the next build is 3.1.1, then 3.1.2, and so on. Set it in both mod.toml and mod_cheats.toml.
