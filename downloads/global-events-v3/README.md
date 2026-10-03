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

New in 3.1:

- **Forced Ambush**: an expensive move that pulls you out of any menu (even the pause menu) and surrounds you.
- **One button for what's urgent**: when a friend can place or steer something, or an event is about to start, a big glowing button at the top takes them straight there.
- **Look here**: on the radar, a friend can click anywhere for free to put a column of light in your world.
- Air strikes and ambushes live on the Radar tab (Auto, or Aim). Curses and the mutator draft moved into Events; Gifts and Pranks are one tab. Enemies are listed easiest to hardest. The four temples have their own spots on the map.
- New pranks: Cucco party, disco lights, confetti, and more sounds (the music dips so they're heard).
- Friends type the room key and their name every time they open the page.
- In game: the next-event countdown runs in real time, even while you're paused or in a menu. Searchlights' backup shows up close and sticks around 10 seconds after the lights go out. Big Poes are gone. Event settings are at the top of Choose Events, and Call/Ban an Event are Moon Services.

New in 3.1.1:

- **Maps of every area**, not just the ones you've visited. The first time the game connects, `CUSTOM_GlobalEventsRemote.dll` reads the ROM copy MM Recompiled keeps on your PC, builds a rough height map of all 77 areas and uploads it to your relay (once). Friends can then pick exact spots anywhere on the Map tab. If it can't find the ROM, add `rom=C:\path\to\your\rom.z64` to `CUSTOM_GlobalEventsRemote.txt`. Nothing from the ROM goes anywhere but your own relay.
- **Radar zoom**: mouse wheel, pinch, + and -, "Whole area" and "Near them".
- **Archipelago heatmap (spoilers)** on the Map tab, off until a friend turns it on. You set your room once in game: **Friends tab > Archipelago room** (address like `archipelago.gg:38281`, and your slot name). It's remembered; update it when your room's port changes. The page connects as a read-only tracker and colors each area by how many important items are left there. Rooms with a password aren't supported.

New in 3.1.9:

- **Fixed: placing a surprise box could freeze the game.** When a box edge sat on certain map positions, the purple edge wall never finished drawing as you walked up to it.
- **Cleaner screen.** No description line under a running event (just its name, timer and goals). No pop-ups about your Nemesis's points, stats, picks or unique traits; they're on the Nemesis page. When a Nemesis is near, its bar shows only its name, level and health.
- **Phones can draw surprise boxes again.** After a pinch zoom, the page could think a finger was still down and ignore every drag. You can also tap one corner, then the other.

New in 3.1.8:

- **A new Nemesis system.** Its level is the number of skill points it has earned. A strong level-up (it kills you, you flee its area, or it gets away) gives 5-6 points; any other fall gives 1-3. Friends spend points on Health, Power, Speed and Tracking (the game spends them after 2 minutes). Every 5 points it gains a trait friends pick. Each stat unlocks a unique trait at 5, 10 and 20: Second Skin, Undying, Colossus, Heavy Hand, Shatter, Executioner, Lunge, Afterimage, Blitz, Echolocation, Bloodscent and Battlethirst. Every extra friend gets their own Nemesis (up to 4 in all), handed out to whoever's died longest ago. Killing one pays a flat 40 Moon Marks, and a Thief keeps what it stole.
- **Events.** Infighting and Sky Leviathan are gone. Searchlights sends more backup. Mirror Dance is Normal. Mirage has two sets of 3 (2 real with 3x health, tougher enemies). Giant's Gale no longer hurts. Moonlit Prey has 2 targets. Frostbite and Sleeping Giant build faster. Scythe Sweep winds up before it swings, with a smaller safe gap. Moth Swarm is never random without a sword. Friends can pick the same event twice in a combo to double it (where that makes sense).
- **Enemies.** Garo Master is Brutal; Garo (new) and Like Like are Medium.
- **Friend page.** Radar icons: red skull Nemesis, yellow skull bounty, red dot enemy, purple surprise zones; drag to pan. Surprises are a box you drag, any size, and in game a purple wall shows only when you're very close to its edge. Friends see the next event and can only wager before it starts (that locks it in). Teleport is click-to-teleport on their map. Event difficulty reads Easy, Normal, Hard, Brutal. The quiz writer starts collapsed.

New in 3.1.7:

- **Better maps.** Every area is mapped now, indoors too (dungeons, houses, grottos, boss rooms). Every walkable surface is drawn, and the ground you can actually reach is bright while the rest is dimmed. Reach is worked out from doors, exits, grottos and every enemy, NPC and chest, and follows ramps, ladders, vines and narrow paths. The old maps also read the wrong ROM files for every area, which made them look broken; that's fixed. Your game rebuilds and re-uploads the maps once after you update `worker.js`.
- **Double Defense switch.** A free Moon Service that turns your Double Defense off for a harder run, or back on. It only works once you've earned Double Defense (from the Great Fairy or the randomizer), and it never takes it away: the game's own flag stays set.
- **Look here** is a solid pillar of light now, like the Pressure Seals pillars.
- **Bullet Hell is gone.**
- **Experimental, trimmed and fixed.** No more pop-ups for a friend's Experimental moves. Gone: Bounce, Launch spin, Health/magic/rupees, Look, Explosion, Bomb Rain, Meteors, Earthquake, Drops, the Moon Draft card and "End it now". Resize Link caps at 15x, no timer goes past 300 seconds, and settings move in steps of 0.1 or more. The Sky has Eerie purple and Sickly green. Raw sound picks from a tree of every sound by name. Tune the running event, the Director and event timing are one card. Each move that uses a spot has a Pick spot button that opens their map right there. Tick boxes untick again, and ticked values really send.
- **Frozen fix.** A freeze with no length could hold Link (and the clock) forever. Freezes now always let go, Experimental freezes last 20 seconds at most, and Undo releases a freeze and borrowed invincibility.

New in 3.1.6 (harder, and lots of polish):

- **Harder events.** Frostbite, Moon Tether, Mirror Madness, Moth Swarm, Hex of Silence, Sinking Sands, Dissonance, Moon's Grasp, Reflex Test, Heartbeat Hex, Scythe Sweep, Giant's Gale and the Mask Salesman's Game are all tougher. Bomb Rain drops more often and the aimed bombs land where you're heading. Pop Quiz gives you 12 seconds per question.
- **Fairer events.** Changing rooms no longer restarts an event's progress (seals, Simon rounds). Volley of Stars and Mirror Madness show where to look. The quiz's "what's here" question only counts what's really in the room, and some confusing riddles are fixed. Combos show which half is left.
- **Less free money.** Bounties, the Nemesis, Starfall and Dowsing pay less. A missed Treasure Hunt pays nothing and freezes you longer. Events you start yourself (and Experimental ones) give no Marks or contract progress. Ending an event the moon sent costs 60 Moon Marks (free after 2 minutes, or if you started it).
- **Shop.** Second Wind 320, Banish 300, Hire a Bounty 190, Nemesis Lure 120, New Contracts 40 (only swaps unfinished ones). Adding a star to a bounty costs more for higher stars. Moon Shield holds 2.
- **Fixes.** A Thief Nemesis no longer pays back twice. Restarting the game can't re-earn finished contracts, a second WANTED or the hunt streak. Turning the Moon Draft off drops your picks. Lots of menu text fixed.
- **Friends.** Helping costs more (heal, gifts, pouch items priced by what they're worth); hurting is a bit cheaper. Friend bounties pay half. Plenty of panel fixes, and the relay uses much less of Cloudflare's free plan.

Update `worker.js` on Cloudflare along with the mod.

New in 3.1.5:

- **Experimental friends.** A 4th friend mode (Friends tab: Full > Experimental > Helpful > Blocked). Experimental friends get a sandbox tab on their page with about 30 moves and lots of settings each: resize Link, speed, gravity, launch, teleport, juiced explosive arrows, spawn any enemy at any size (hitboxes grow too), change every enemy at once, Nemesis level and stats, explosions, bomb rain, meteors, time, sky, quakes, drops, critters, camera zoom, start any event, tune events, the Director, Moon Marks, the Moon Draft, banners and raw sounds. Free, one a second, and it can kill you. "Undo Experimental stuff" in your Friends tab clears it all. It never touches your items or anything that could give a randomizer check, and nothing it spawns or starts pays Marks or counts for contracts.
- **Friends write Pop Quizzes.** Up to 4 questions with their own answers.
- **Moon Marks rebalanced.** Contracts wait for the next dawn instead of renewing at once, pay less, and you can hold 999 at most. Shop prices are up, and there are new things to spend on: Rupee Exchange, Moon Insurance, Training Dummy and Hire a Bounty.
- **Fixes.** The event clock waits while Pop Quiz waits for your answer. Sleeping Giant, Reaction Test, Dissonance and the other newer events wait until their meter shows before they can hurt you. Your Nemesis heals fully when it levels up and never keeps damage between fights. Leevers can't become your Nemesis.
- The menu shows the exact version now.

Update `worker.js` on Cloudflare along with the mod.

New in 3.1.4:

- **Moon Draft.** Curses and mutators are one draft now. Each dawn and dusk you pick 1 of 2 from a mix of both. Picks last the cycle, up to 6, and each one adds 10% to your Moon Marks. There's no random curse at the start of a cycle anymore. "Shed Last Pick" in the shop removes your newest one. Friends rig your next draft with any two.
- **Friend combos can use any event.** If either half has a goal (Nemesis, Champion, Pop Quiz, or one that runs until you finish), you have to clear both to end it.
- **Friends list fixed.** Only friends who are here show up. They drop off a few seconds after closing the page. Your Blocked and Helpful settings are still remembered.
- **Panel:** Forced Ambush is a "Forced" checkbox on the Ambush card (its cost updates with it). Grant Moon Marks is under Help them; the Marks tax and Message are under Pranks. The Termina map is bigger.
- More special Nemesis names, and they show up twice as often (1 in 16).

Update `worker.js` on Cloudflare along with the mod.

New in 3.1.3:

- **One Map tab.** The live view around you and the Termina map are side by side. Friends click any area on Termina to look at it and hide a surprise at an exact spot ("Surprise spot"), then "Back to them" for the live view, strikes, steering and placing.
- **Maps show only ground you can walk on.** The area maps follow the ground from each area's entrances (steps, drops, swimming, never through walls), so scenery behind invisible walls is gone. The relay replaces the old maps by itself the first time the new DLL connects.
- **Forced Ambush** waits for you to pick your mutator instead of closing that screen, and puts your ocarina away like pressing B.

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
