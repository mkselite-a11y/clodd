# V3 plan (agreed with user). Version 3.0.0, menu "v3.0". Backup: scratchpad/backup/rc_2.9.1.tgz
Alt source: scratchpad/alt/mod/src (events_list.c has the 19 events to port)

## Stage 1 events
- [ ] Intensity fixed at NORMAL; Fierce Moon curse + friend Empower => BRUTAL. Remove Intensity/Wild/Pressure/event-settings pages from menu.
- [ ] Defaults: master ON, interval 2 min, duration 30s; long events (Simon, Seals, Dowsing, Quiz) run until done, cap 60s
- [ ] Hide: Majora, Imposters, Poltergeist, Swarm (plus already hidden)
- [ ] Echo: one-shot (fairy/Second Wind save, Moon Shield doesn't), 2s delay, head start
- [ ] Bomb Rain: high drop ~2s, shadow, no landing under roof, ~40% fewer, fewer aimed
- [ ] Treasure: no HUD at all. Tingle: any projectile pops balloon
- [ ] Pop Quiz: 4 questions; songs + Alt riddles + contextual (zone connects, actor in area, bounty zone, nemesis species, last event, mask worn)
- [ ] Moonfall -> Alt version (circles ahead). Blood Moon: kill all ends early (+Marks)
- [ ] Port 19 Alt events into src/ev_v3.inc: Gorgon, Volley, Scythe, Simon(Mask Salesman), Grasp, Silence, Frost, Moths, Gale, Mirror Madness, Seals, Reflex, Heartbeat, Sinking, Sleeping Giant, Tether, Starfall, Dissonance, Dowsing
- [ ] Tiers for new events (D1). Director: tier budget 4/8/14/24, enemy cost 1/2/4/8, x hearts scale (3h 0.7,10h 1.0,20h 1.4), wider spawn 800-2500 outdoors
## Stage 2 Moon systems
- traits cut Swift,Brutal,Giant,Leech,SecondWind; add Coward,Moonblessed,MarkHungry,Frostbrand; Thief steals 3 Marks, repaid x2
- nemesis: wounds remembered, 25% scarred return, crown on 3 hits+lives, level cap 200 all, stats cap 50, no limiter, crit mass 10 (rainbow bar + powers), style charger/shadower, no ReDead in pools
- bounties/world: 27 outdoor zones + indoor (skip sword-blocked), real positions far, never within 400 of exits, entrance arrivals, walk out, personalities (Nest/Roam/Migrate/Hunt 1in5/Skittish), names, compass arrow, move pop-ups, Clock Town ok
- shop: cut Storm Rod, Blood Pact, Decoy, Recall; add Lunar Tonic, Second Wind, Nemesis Brand, Stillwater; fix Portal (survive room change, cross-room) + Dash (step-to-step, 300). Services merge Lure; cut Rare, Lull, Mercy, Ward, Sight, Lucky, Gamble. Prices per round 3.
- mutators cut Bleeding, WeakRecovery, MoonTears, VengefulDead, RupeeRot. curses cut NightTerrors, Unseen, Aftershocks, Meteor, Doom, Legion, Ambush, Wrath, Weightless
- balance H1-H4. remove save code UI (keep autosave)
## Stage 3 UI
- tabs Z/R: Overview, Board, Shop, Nemesis, Events(curse shown here), Friends, Settings; wide panel; gold Marks; red unaffordable; Bought! box; stat bars rainbow at 10+
- Press L near any owl (all glow) or carpenter; remove board carpenter; carpenter normal dialogue
- pop-ups bottom stack 4, 5s fade, colors; banners for big non-event moments (off -> pop-up); event bar how-to line; buff timers; world-news toggle
## Stage 4 Friend Link
- per-friend pools+cooldowns (name+key), connected list in game, join/leave pop-ups, per-friend Block/Helpful/Full, scary blocked page
- rename cheap no pop-up; radar + Termina map + browse all areas; placements (15s timeout); surprises (pop-up "X placed a surprise...", faint shimmer, not near exits)
- merges: ev_now+ev_soon, curse+curse_set, bounty+wanted; cut zap, mut_add
## Progress log
- Stage 1 DONE (compiles): intensity fixed, defaults, hidden events, Echo, Bomb Rain(+marks), Treasure HUD off, Tingle projectiles, Quiz (ev_v3.inc V3_QuizQuestion), Moonfall predictive+circles, Director(Dir_* in events.c), Mirage/Infight toned, far enemies close in, 19 ported events in src/ev_v3.inc (V3_* hooks). TODO later: menus hide intensity/wild/pressure/settings pages; payout use V3_Failed().
- Stage 2 DONE (compiles): 77 zones (sZones, Zone_*), traits remapped in place (TR_RETIRED 13), mutators retired via Mut_Retired, nemesis caps 200/50 + Nem_StatEff/Nem_Crit + crit powers in V2_AfterActorUpdate, scarred/dormant/style/hpPct, World_* system (ev_v2.inc before V2_UpdateInner), Brand/SecondWind/Tonic/Stillwater, portals cross-room, dash fix, services hidden via price 0 (V2_ServiceHidden), Lure merged, payouts V2_Earn, prices, save format 4 (Save_Load4). Pop-ups: Menu_Notify/Menu_ShowBanner in menu.c (stacked, bottom).
- Stage 3 DONE (compiles): menu tabs (sTabs, MENU_OVERVIEW, MENU_EV_DETAIL), wide panel, header marks, stat bars, footer 3 lines; pop-ups; owl glow (rainbow_carpenter.c ObjWarpstone hooks); Press L via Board_Update (ev_v2); compass+buff HUD; event bar how-to line. NOTE: menu.c patches saved in scratchpad/patches/*.py (menu.c was restored from backup once - re-apply order: menu_notes.py, menu_tabs1-4.py if needed)
- Stage 4 DONE (compiles, relay tested in Node with a SQLite D1 stand-in, panel tested in Chromium): friends list + per-friend modes (Friends tab rows ID_LNK_FR_*, saved as "friend_modes" via ger_store), join/leave pop-ups, steering (Steer_Get: Tether, Moonfall, Searchlights), placements (Treasure_PlaceAt, Proc_PlaceAt, V3_Place), surprises (Sur_*), radar status (pos, scene, acts, fmodes, evplace, steer, surprises), map blocks (LinkMap_Update -> ger_map -> POST /api/map), zap and mut_add cut, rename silent. DLL: ger_friends, ger_map, status 4096. Relay: per-friend points/cooldowns (X-Friend-Id + unique name), modes from fmodes, blocked page, radar + Termina map + surprises, maps table. tables.json is generated by remote/relay/sync_tables.py.
- V3 shipped as 3.0.0: downloads/global-events-v3 (both .nrm, DLL, worker.js, README, zip). Not yet tested in game.
