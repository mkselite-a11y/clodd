# Global Events Mod v3 (Majora's Mask: Recompiled)

Every couple of minutes, a random event hits wherever you are in Termina: 46 of them, each with its own counter. Around them sit the Moon systems (Moon Marks, a Bounty Board with targets that roam Termina, a Nemesis that hunts you and grows, the Moon Shop and Moon Pouch, a Mutator Deck and a Cycle Curse), and Friend Link, where friends mess with your run from a web page.

Randomizer-safe: nothing here gives real items, sets story flags or touches checks.

## Install

The release has two folders: **Put in mods folder** and **Put in Cloudflare**.

1. From **Put in mods folder**, pick **one** of the two mods:
   - `CUSTOM_global_events_v3.nrm`: the mod.
   - `CUSTOM_global_events_cheats_v3.nrm`: the same, plus cheat pages.
2. Put it in your mods folder, along with `CUSTOM_GlobalEventsRemote.dll` (also in that folder) (the mod won't load without it, even if you never use Friend Link).
3. Updating from v2: delete `CUSTOM_global_events_v2.nrm` (or the cheats one) and the old `GlobalEventsRemote.dll` first. Two versions of the same mod won't load together.

Every file this mod puts in your mods folder starts with `CUSTOM_`: the mod, `CUSTOM_GlobalEventsRemote.dll`, `CUSTOM_GlobalEventsRemote.txt` (your relay settings) and `CUSTOM_GlobalEventsSaves.txt` (your Moon progress). The first time the new DLL runs, it copies your old `GlobalEventsRemote.txt` and `GlobalEventsSaves.txt` to the new names. After that, the old ones can go.

Open the menu by pressing **L** near any owl statue (they glow) or the rainbow carpenter in South Clock Town, or anywhere by playing **C-Up, C-Up, C-Down, C-Down** on the ocarina. **L/R or Z** switch tabs.

## Friend Link

Friends open a web page (the "Moon's Hand") and spend chaos points on you: start or pick events, send ambushes and air strikes, train your Nemesis, post bounties, gift you things and more.

What's new in v3:

- **Everyone has their own points and cooldowns.** Each friend picks a name (5 letters, unique in the room). Their browser keeps a hidden id, so their points stay theirs.
- **You decide what each friend may do.** Open the **Friends** tab. Every friend who has joined is listed (online ones first, marked "here"). Press A on a friend to switch between:
  - **Full**: anything.
  - **Helpful**: only nice things (heals, gifts, messages, harmless pranks).
  - **Blocked**: nothing. Their page shows them the moon's wrath instead.
- **Pop-ups when friends join or leave.**
- **Radar**: a live top-down view around you that fills in as you explore. Friends can aim air strikes and ambushes on it.
- **Steering**: during Moon Tether, Moonfall and Searchlights, a friend can drag the circle, the falling rocks or a searchlight. Let go for 5 seconds and it goes back to normal.
- **Placements**: in the first 15 seconds of Treasure Hunt, Procession of the Dead, Dowsing Moon, Scythe Sweep and Pressure Seals, a friend can pick where things go.
- **Surprises**: a friend can hide a surprise (an event, an ambush, a bomb drop, rupoor rain or your Nemesis) in any area of Termina. You get a pop-up that they placed one, never where. It gives off a faint shimmer and goes off when you walk into it. It's never placed right by a door or a loading zone.
- **Merged moves**: "start an event" also covers "hurry the next one", "set their curse" also covers "reroll", and the bounty stars include WANTED. Renaming your Nemesis is cheap and quiet: you find out when you meet it. Zap and Force a Mutator are gone.

A Friend Shield from the Moon Shop still eats the next mean move.

### Setting up the relay (once, free)

Friend Link needs a small relay on Cloudflare (free plan). No installs.

1. In the Cloudflare dashboard: **Storage & Databases > D1 > Create database**, name it `global-events`.
2. **Workers & Pages > Create > Worker** ("Hello World"), then **Edit code**, replace everything with `worker.js` from **Put in Cloudflare**, and **Deploy**.
3. The worker's **Settings > Bindings > Add > D1 database**: variable name `DB`, database `global-events`. Deploy again if asked.
4. Set your room password: change `ROOM_KEY` at the top of `worker.js`, or add a secret named `ROOM_KEY` under **Settings > Variables and Secrets**.
5. Start the game once with the mod. It writes `CUSTOM_GlobalEventsRemote.txt` next to the DLL. Fill it in and restart:
   ```
   url=https://your-worker.your-name.workers.dev
   key=your room password
   ```
6. Send your friends the worker's address and the room password.

Updating from v2: just paste the new `worker.js` and deploy. The database upgrades itself. Old shared points don't carry over: every friend starts with a full pool.

A friend who clears their browser data comes back as someone new (with a new name). If you blocked them, block the new name too.

## Building

Needs the N64Recomp mod toolchain (clang with MIPS, `RecompModTool`), a Windows cross compiler for the DLL, and Python 3.

```
git submodule update --init --recursive
RECOMP_MOD_TOOL=/path/to/RecompModTool sh build_both.sh
```

Everything lands in `dist/`. `build_both.sh` also regenerates `remote/relay/tables.json` from the game source (`sync_tables.py`) and rebuilds `worker.js` (`build_relay.py`), so the web page always lists the same events, enemies, traits and areas as the game.

Source layout:

- `src/events.c` and its includes: the events (`ev_v11.inc`, `ev_v3.inc`), the Moon systems (`ev_v2.inc`), Moonlit Prey (`ev_prey.inc`) and Friend Link (`ev_remote.inc`).
- `src/menu.c`: the menu, pop-ups and banners. `src/mods.c`: cheats and fun options. `src/rainbow_carpenter.c`: the carpenter and owl glow.
- `remote/native/ge_remote.c`: `CUSTOM_GlobalEventsRemote.dll` (WinHTTP, a background thread, and the auto-save file).
- `remote/relay/`: the Cloudflare Worker (`worker_src.js`) and the web page (`panel.html`).
