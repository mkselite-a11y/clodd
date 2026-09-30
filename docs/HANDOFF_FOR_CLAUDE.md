# Moonfall Mayhem: complete handoff for another Claude

You are receiving this from another Claude session that built **Moonfall Mayhem**, a
Majora's Mask: Recompiled mod. The person sending this has a similar mod of their own
and wants you to understand this one completely, especially:

- how the **menus and pop-ups are rendered** (they like how these look),
- how **bounty movement pop-ups** work,
- the **Moon Marks / Moon Pouch** inventory system,
- every bounty quality, trait, stat, pouch item, event, the nemesis, and how the world
  keeps moving across zones when the player isn't there.

Everything below describes the code as it stands. Numbers are exact unless marked
"about". MM gameplay logic runs at **20 updates per second**, so "frames" below are
1/20 s and `SEC(x) = x * 20`.

---

## 1. Ground rules the mod follows

It is built to sit on top of an **Archipelago randomizer** run without breaking logic:

- Never gives items, rupees, upgrades, songs, masks, movement boosts or time control.
- Ammo refills only work for gear the player already owns.
- Events never spawn real bombs or real fire. Explosions and flames are visual effects;
  damage is computed by the mod, so bombable walls, webs and ice can't be opened.
- Enemies that steal gear (Like Like, Takkuri), grab-and-warp (Wallmaster) or explode
  (Real Bombchu) are excluded from the enemy roster.
- Its own save file sits next to the game save (`<save path>.moonfall<slot>.bin`,
  written by a native DLL). The game's and the randomizer's save data are never touched.
- Only `RECOMP_HOOK` / `RECOMP_HOOK_RETURN` hooks and Recomp events are used, never
  `RECOMP_PATCH`, so it can load alongside other mods that patch functions.

Damage is expressed in **quarter hearts** (1 heart = 16 health units = 4 quarters),
then scaled by a **Master Difficulty** config (Gentle 60%, Normal 100%, Cruel 150%,
Merciless 200%) times an in-game "Mod difficulty %" (default 100). Damage goes through
the game's own damage path (`play->damagePlayer` or the knockback helper
`func_800B8D10(play, NULL, speed, yaw, velY, type, damage)` with type 2 = small
knockback), so invincibility frames, double defense and the Giant's Mask all apply.

---

## 2. Hard-won technical lessons (read these first)

1. **`RECOMP_HOOK_RETURN` hooks do NOT receive the hooked function's arguments.** The
   runtime calls them with the CPU context as it is at the `return`, so a0-a3 are
   garbage. Declare return hooks as `void Hook(void)` and use a pointer you saved
   earlier (for example the `PlayState*` stored in `recomp_on_play_update`, or in the
   matching entry hook). Using the "arguments" crashed the game on the first frame.
2. **`EffectSsLightning_Spawn` crashes.** It's an unused Ocarina of Time leftover. Only
   use effects the game itself calls. Safe ones used here: `EffectSsHitmark_SpawnFixedScale`,
   `EffectSsKirakira_SpawnDispersed/SpawnSmall`, `EffectSsBomb2_SpawnLayered`,
   `EffectSsDeadDb_Spawn`, `EffectSsDust_Spawn`, `EffectSsIcePiece_SpawnBurst`,
   `EffectSsGSplash_Spawn`.
3. Avoid `Audio_PlaySfx_AtPos` with a pointer to a stack variable; the audio system keeps
   the pointer. Plain `Audio_PlaySfx(id)` is used everywhere now.
4. **Enemy models must be loaded to spawn an enemy.** `Actor_Spawn` fails if the actor's
   object isn't in `play->objectCtx`. See section 9 for how the mod loads extra models
   safely.
5. Freezing the world while a menu is open: set `play->frameAdvCtx.enabled = true`.
   `Play_UpdateMain` then only advances when controller 2 presses Z+R, so the world
   holds still while drawing continues. Clear it when the menu closes.
6. Recomp events used (all exist from MM Recomp 1.2.1): `recomp_on_init`,
   `recomp_after_play_init`, `recomp_on_play_update` (runs before `Play_Update`, the
   right place to read and modify controller input), `recomp_after_play_update`,
   `recomp_after_actor_init`, `recomp_should_actor_update`, `recomp_after_actor_update`,
   `recomp_after_load_save`, `recomp_after_owl_save`, `recomp_after_autosave`.
7. `z64recomp_extend_actor_all(sizeof(Tag))` in `recomp_on_init` gives every actor a
   small tag struct. That's how the mod marks "this Wolfos is the nemesis" and tracks
   per-enemy hits for promotions.

---

## 3. Rendering: menus, pop-ups and banners

This is the look the person likes. It's deliberately simple: the game's own 8x8 debug
font (`GfxPrint`) over translucent filled rectangles, drawn on the **overlay display
list**, on a 320x240 virtual screen (40 columns x 30 rows of 8 px characters).

### 3.1 Where drawing happens

- 2D (menus, HUD, pop-ups): a `RECOMP_HOOK_RETURN("Play_Draw")` hook with no
  arguments, using the saved `PlayState*`. Display lists are still open at that point.
  Skip drawing unless `gSaveContext.gameMode == GAMEMODE_NORMAL`.
- 3D (event markers: rings, disks, orbs, beams): `RECOMP_HOOK_RETURN("Actor_DrawAll")`
  into `POLY_XLU_DISP`.

### 3.2 The two 2D primitives

```c
// Translucent filled rectangle straight into OVERLAY_DISP.
void Draw2D_Rect(s32 x, s32 y, s32 w, s32 h, Color c) {
    OPEN_DISPS(gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCycleType(OVERLAY_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, c.r, c.g, c.b, c.a);
    gDPFillRectangle(OVERLAY_DISP++, x, y, x + w, y + h);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(gfxCtx);
}

// Text through GfxPrint. The printer's commands are written inside the POLY_OPA
// buffer, skipped over with a branch, and called from OVERLAY_DISP, so text lands
// exactly where it's issued (rectangles and text can be interleaved freely).
void Draw2D_Text(s32 x, s32 y, Color c, const char* text) {
    GfxPrint printer;
    OPEN_DISPS(gfxCtx);
    Gfx* polyOpa = POLY_OPA_DISP;
    Gfx* gfx = Gfx_Open(polyOpa);
    gSPDisplayList(OVERLAY_DISP++, gfx);
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    GfxPrint_SetColor(&printer, c.r, c.g, c.b, c.a);
    GfxPrint_SetPosPx(&printer, x, y);
    GfxPrint_PrintString(&printer, text);
    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    gSPEndDisplayList(gfx++);
    Gfx_Close(polyOpa, gfx);
    POLY_OPA_DISP = gfx;
    CLOSE_DISPS(gfxCtx);
}
```

Helpers: `Draw2D_TextCentered(y, color, text)` centres by `strlen * 8`.
`Mf_Rainbow(t, alpha)` returns a hue-cycling colour (6-segment HSV, `t` in degrees);
titles use `Mf_Rainbow(frame * 3..6, 255)` so they shimmer. Text is kept to 38
characters per line; long pop-ups are wrapped at a space into two lines.

### 3.3 Palette

| Use | RGBA |
|---|---|
| Menu background (whole screen, inset 6 px) | 12, 4, 28, 225 |
| Menu header bar | 40, 16, 80, 255 |
| Selected row highlight | 90, 50, 160, 200 |
| Normal text | 225, 225, 245, 255 |
| Dim / secondary text | 150, 150, 180, 255 |
| Gold (Moon Marks, costs, claimed) | 255, 225, 130, 255 |
| Red (danger, active curse) | 255, 120, 120, 255 |
| Green (good, warded) | 130, 255, 160, 255 |
| Footer panel | 25, 10, 50, 230 |
| Pop-up (toast) backing | 0, 0, 0, 120 (fades out) |
| Rumor pop-up text | 200, 180, 255, 255 |
| Big banner backing | 20, 0, 40, 170 |

### 3.4 Moon Menu layout

- Full-screen panel `(6, 6, 308, 228)` in the background colour.
- **Header** at `(10, 10, 300, 24)`: page title in shimmering rainbow at `(16, 13)`,
  Moon Marks right-aligned in gold ("123 Marks"), and a second dim line at y=23:
  `Day N - <curse name>` (plus "(warded)").
- **List rows** start at y=40, 11 px apart, 14 rows visible with scrolling. The selected
  row gets a highlight rect `(14, y-2, 292, 11)`. Left text at x=20, an optional
  right-aligned value at `320 - 20 - len*8`.
- **Footer** `(10, 196, 300, 30)`: line 1 at y=199 (normal colour), line 2 at y=210 (dim).
  Used for the selected item's description plus control hints.
- **Flash message** for feedback ("Bought!", "Not enough Moon Marks."): a black box
  `(40, 100, 240, 16)` with gold centred text, shown for 2 s.
- Controls: D-pad or stick to move (8-frame initial delay, then repeats every 2 frames),
  A select, Left/Right change values or page through long lists, B back, Start close.
  The first 3 frames after opening ignore input so the opening press doesn't also select.
- While open: world frozen (`frameAdvCtx.enabled`), all controller input zeroed before
  the game reads it.

Pages: Global Events (list; each opens a detail page with Enabled, Frequency weight 0-10,
three event-specific settings, "Force trigger now", "Restore defaults"), This Cycle's
Curse, Your Nemesis (name, level, species, hunting/dormant, location if revealed, four
stat bars 100 px wide at 10 px per point, trait list with descriptions, kills / meetings /
times you fled, promotion reason in the footer), Bounty Board, Moon Pouch Shop, Friends,
Settings, Stats, Debug.

How the menu is opened:
- Standing near the plank-carrying carpenter in South Clock Town and pressing **L**
  (the carpenter is tinted rainbow by setting fog colour + `gSPFogPosition` before his
  draw and restoring with `func_800AE5A0` after it).
- Standing near the South Clock Town **owl statue** (pulsing purple the same way) and
  pressing L opens straight to the Bounty Board.
- Anywhere: playing **C-Up, C-Up, C-Down, C-Down** on the ocarina. The mod watches
  `play->msgCtx.ocarinaStaff->pos` / `buttonIndex` while `msgMode == MSGMODE_OCARINA_PLAYING`
  (it never calls the audio getter, which has side effects), then closes the ocarina the
  same way pressing B does and opens the menu.
- A "Press L: Moon Menu" prompt appears near the carpenter: black box `(w, 13)` centred at
  y=176, yellow text (255, 240, 120) at y=178.

### 3.5 Pop-ups ("toasts") and rumor pop-ups

A 4-slot queue. A new pop-up pushes the others down and drops the oldest.

- Each lives 5 s and fades out over its last second (text alpha and backing alpha both
  scale down).
- Drawn from y=34 downward (under the event bar), full width minus 8 px margins:
  backing rect `(8, y-2, 304, 12)` at (0,0,0,120), centred text; long ones wrap onto two
  lines with a 21 px backing.
- Colour depends on the source: rumors (200, 180, 255), Moon Marks gains (190, 170, 255),
  friend messages (255, 255, 180), warnings (255, 150, 150), nemesis blocked (255, 170, 120).
- Hidden while the menu is open.

**Rumor gating** (this is how bounty movement pop-ups work): every world message goes
through `World_Log(text)`. It only turns into a pop-up when a module-level "rumor mode"
flag is on and the player's "Rumor pop-ups" setting is on. Movement code switches rumor
mode on around its log calls:

```c
World_SetRumor(true);
World_Logf2(name, " moved to ", zoneName);   // "Rotfang the Peahat moved to Milk Road"
World_SetRumor(false);
```

Bounty movement pop-ups are shown **only for bounty targets the player has claimed**:
- A claimed target changing zones: "Gnash the Wolfos moved to Termina Field".
- A fight between targets pops up only if one of them is claimed.
- "Your contract expired: ..." pops up; unclaimed withdrawals stay silent.
- Nemesis movement: "Grak the Unbroken was seen in Milk Road" (45% of its moves, and
  always when it reaches your zone).

### 3.6 Big banner

For important moments (event start, nemesis appears, nemesis promoted, bounty target
appears, curse rolled): a full-width band `(0, 92, 320, 36)` in (20, 0, 40, 170), a
rainbow-shimmer title at y=98 and a wrapped light-lavender subtitle (230, 230, 255) at
y=112. Lasts 3.5 s. Can be turned off in Settings.

### 3.7 Event bar

While an event runs: panel `(8, 4, 304, 26)`, event name + seconds left centred at y=7
(rainbow for dangerous events; blue (120,180,255) harmless, green (90,230,140) benefit),
the counter instruction in yellow (255, 235, 150) at y=17, and a 2 px time bar at y=27
shrinking from full width. Ten seconds before an event starts, "The moon stirs..." shows
at y=8 in red. Events add their own widgets under it: meters use a 120x8 bar at x=100
with the label to its left (for example "Frost", "Noise", "Ward", "Stone").

### 3.8 World markers and 3D shapes

- `Draw2D_WorldMarker(play, pos, colour, label)`: projects a world point with
  `Actor_GetProjectedPos`; if it's on screen, draws the label in a black box at that spot;
  otherwise draws `<<` / `>>` at the screen edge, or "v behind v" at the bottom.
- 3D (on `POLY_XLU_DISP`, combiner `G_CC_SHADE`, `G_RM_ZB_XLU_SURF`, no lighting or
  culling, vertices generated per frame with `GRAPH_ALLOC`): flat disk (triangle fan,
  24 segments, centre opaque fading to the rim), ring (15-segment quad strip), billboard
  orb (disk rotated with `play->billboardMtxF`), camera-facing pillar (quad rotated by the
  camera yaw), and a wedge (partial fan, used for the "safe gap" in Scythe Sweep).

---

## 4. Moon Marks and the Moon Pouch

**Moon Marks** are the mod's own currency (stored in its save, never rupees).
The player starts with 10.

Earned from:
- **Surviving a dangerous event:** `3 + 2 x risk` (risk 1-4 per event), halved if the
  player failed the counter at least once, +15% while a curse is active, +25% if a friend
  sent it, +50% if started with an Event Tempter, -50% if ended with a Stillwater Charm.
- **Harmless events:** 2. **Benefit events** pay by performance (see Starfall, Dowsing).
- **Claimed bounties** and **nemesis kills** (formulas in sections 6 and 7).
- **Ambush** wipes: 2 per ambusher.
- Friends can send 5.

Every payout passes through `Marks_Scale`: Curse of Poverty x0.6, then Mark Magnet x1.5
(consumes one charge), minimum 1. Each gain shows a pop-up "+N Moon Marks: <reason>"
with the rupee sound.

**Buying:** Moon Menu > Moon Pouch Shop lists all 24 items with price, owned/max and
description. A to buy. Prices are +25% under Curse of Poverty.

**Using in play (the pouch HUD):**
- Hold **L**. A panel appears bottom-centre `(6, 176, 308, 42)` in (10, 0, 30, 190):
  line 1 gold `< Item Name xN >`, line 2 the description, line 3 dim hints and your marks.
- While L is held, **D-pad / C left or right** step to the previous or next item you
  **own** (items with 0 are skipped and never displayed). If you own nothing, the panel
  says "Pouch empty - buy items in Moon Menu".
- **D-pad / C down** uses the shown item.
- While L is held, D-pad and C-left/right/down are removed from the input before the
  game sees them, so C items don't fire.
- Releasing L without having used a combo, while standing near the carpenter or owl,
  opens the menu instead.
- When L isn't held, active timed buffs are listed along the bottom (y=222) in mint green
  with seconds remaining ("Moonshield 5s").

### 4.1 The 24 items

| # | Item | Cost | Max | Effect |
|---|---|---|---|---|
| 0 | Lunar Tonic | 8 | 5 | Heal 3 hearts |
| 1 | Full Moon Draught | 20 | 3 | Full heal |
| 2 | Moonshield | 15 | 3 | 8 s: all damage negated (health restored each frame) |
| 3 | Stillwater Charm | 18 | 3 | Ends the current event as survived, half reward |
| 4 | Omen Scroll | 6 | 5 | Delays the next event by 60 s |
| 5 | Nemesis Lure | 12 | 3 | Your nemesis moves to your zone and appears right away (if its model can load) |
| 6 | Smoke Veil | 14 | 3 | Nemesis despawns, jumps 2 zones away, dormant for 180 s |
| 7 | Bounty Compass | 6 | 5 | 60 s: HUD line "Compass: 23m left" to the nearest claimed target, or which zone it's in |
| 8 | Hunter's Whetstone | 16 | 3 | 90 s: +50% damage to nemeses and bounty targets |
| 9 | Moonsteel Ward | 20 | 3 | 60 s: all damage halved |
| 10 | Second Wind | 30 | 2 | Passive: a lethal blow leaves you at 4 hearts instead (auto-consumed) |
| 11 | Magic Flask | 10 | 3 | Refill magic (only with a magic meter) |
| 12 | Quiver Refill | 8 | 3 | Arrows to capacity (only if you own the bow) |
| 13 | Bomb Satchel | 8 | 3 | Bombs and bombchus to capacity (only ones you carry) |
| 14 | Deku Bundle | 5 | 3 | Sticks and nuts to capacity (only ones you carry) |
| 15 | Curse Ward | 35 | 2 | Suppresses this cycle's curse until the next dawn |
| 16 | Mark Magnet | 15 | 3 | Next 3 Moon Mark payouts x1.5 |
| 17 | Moon Spyglass | 4 | 3 | Reveals your nemesis's location for the rest of the cycle |
| 18 | Stasis Orb | 22 | 3 | 6 s: every enemy's update is skipped (frozen) |
| 19 | Riposte Charm | 16 | 3 | 60 s: whatever hits you loses 2 HP (never below 1) |
| 20 | Event Tempter | 5 | 5 | Starts a random event now, +50% reward |
| 21 | Nemesis Brand | 25 | 1 | The next roaming enemy you damage becomes your nemesis |
| 22 | Bounty Reroll | 5 | 5 | Replaces every unclaimed bounty on the board |
| 23 | Heart Siphon | 18 | 3 | 45 s: every hit you land heals 1/4 heart (10-frame cooldown) |

Items that can't do anything (no nemesis, no bow, no event running...) show a warning
pop-up and are **not** consumed. Under Curse of the Sealed Pouch, uses need 45 s between them.

---

## 5. Enemy roster (shared by nemeses, bounties and Ambush)

Difficulty 1 (trivial) to 10 (brutal) drives rewards, stat budgets and who wins
off-screen fights. "Roaming" enemies can become nemeses; rooted ones are bounty-only.

| Enemy | Difficulty | Roaming |
|---|---|---|
| Deku Baba | 1 | no |
| Stalchild | 1 | yes (night) |
| Chuchu | 1 | yes |
| Keese | 1 | yes (flies) |
| Guay | 1 | yes (flies) |
| Mad Scrub | 2 | no |
| Leever (large) | 2 | yes |
| Fire Keese | 2 | yes (flies) |
| Black Boe | 2 | yes (night) |
| Tektite | 2 | yes |
| Snapper | 3 | yes |
| Dragonfly | 3 | yes (flies) |
| Eeno | 3 | yes |
| Poe | 3 | yes (flies, night) |
| Bubble | 3 | yes (flies) |
| Freezard | 3 | no |
| Armos | 4 | yes |
| Dodongo | 4 | yes |
| Floormaster | 4 | yes |
| Hiploop | 4 | yes |
| Wolfos | 5 | yes (night) |
| ReDead | 5 | yes (night) |
| Peahat | 6 | yes |
| White Wolfos | 6 | yes |
| Garo | 6 | yes (night) |
| Dinolfos | 7 | yes |
| Iron Knuckle | 9 | yes |

---

## 6. Traits and stats (shared engine for nemeses and bounty targets)

### 6.1 The four stats

| Stat | Effect |
|---|---|
| Health | Max HP x (1 + 0.4 per point). Nemeses also get +1 base HP per 3 levels and +50% base if scarred. |
| Power | Every hit it lands deals an extra `damage x power / 5` (min 1, difficulty-scaled). |
| Tracking | Travels between zones faster: interval divided by (1 + 0.5 per point). |
| Speed | Moves +10% faster per point (extra displacement added after its own update, capped at +80% total including traits). |

For bounty targets the **rank (0-4) is used as every stat value**. Mark-Hungry adds +1
Power per 50 Moon Marks the player carries (max +3). Berserker adds +2 Power below half HP.

### 6.2 The 20 traits

| Trait | Effect |
|---|---|
| Ironhide | Non-lethal hits deal a third less (HP added back). |
| Regenerating | +1 HP every 4 s without being hit. |
| Berserker | Below half HP: +40% speed and +2 Power. |
| Vampiric | Heals 2 HP whenever it damages you. |
| Venomous | Its hits poison you: 3 health units every 2 s for 10 s (never below 1/4 heart). |
| Frostbrand | Its hits chill you: the control stick is halved for 4 s. |
| Stormcaller | Its hits also shock you. |
| Pyromaniac | Its hits set you ablaze (short game burn, about 1 heart). |
| Blinker | If you stay over 350 units away for 8 s, it teleports 150-230 units behind you in a puff. |
| Ambusher | Appears 120-200 units right behind you after 1 s in the area, no banner, just "Behind you!". |
| Relentless | Crosses zones twice as fast. |
| Thief | Each hit steals 3 Moon Marks; repaid double when it's slain. |
| Mark-Hungry | +1 Power per 50 Moon Marks you carry (max +3). |
| Moonblessed | Immune to damage while a Global Event is running. |
| Giant | 1.35x size and +50% HP. |
| Swift | +30% movement speed. |
| Hexcaller | Its hits have a 35% chance to start a dangerous Global Event (if none is running). |
| Coward | At 30% HP it vanishes in smoke, goes dormant 240 s and returns healed. |
| Warlord | Arrives with 2 minions of its own kind flanking it. |
| Grudge-Bearer | +1 Power every time you flee its zone mid-fight. |

Bounty targets can't roll Warlord, Ambusher, Relentless or Grudge-Bearer (those become Swift).

Visual identification: tagged enemies emit sparkles every 6 frames at their focus point:
**purple** (170, 60, 255) for the nemesis, **gold** (255, 200, 40) for bounty targets.

---

## 7. Nemesis system

Only one nemesis at a time. Names are built from 24 first names (Grak, Vorn, Skrell,
Ulgoth, Maz, Kethra, Brum, Ziv, Horga, Drel, Vasko, Tuk, Oona, Ragnor, Pell, Syx, Gorm,
Irla, Kaz, Mordu, Fenn, Zorba, Quill, Hask) and 24 epithets (the Unbroken, Moonbitten,
Skullcleaver, the Patient, Ashmaw, the Grinning, Nightstalker, the Twice-Dead, Gloomfang,
the Collector, Bonegnaw, the Laughing, Stormhide, the Hungry, Duskwalker, the Scarred,
Ironjaw, the Whisper, Blightclaw, the Relentless, the Mirthless, Cinderheart, the Masked,
Frostmourn), for example "Grak the Unbroken".

### 7.1 Promotion

A roaming roster enemy is promoted when it:

| # | Condition | Chance |
|---|---|---|
| 0 | Kills you | 100%, and it gains a stat point immediately |
| 1 | Hits you 3 times and lives (2 under Curse of Promotion) | Promotion chance (default 60%, +30 under Curse of Promotion) |
| 2 | You leave the area while tangled with it (it or you landed a hit within ~3 s, within 350 units) | half the promotion chance (+30 under the curse); it stays in the zone you left |
| 3 | Survives at 1/4 HP or less for 20 s after you hurt it | half the promotion chance |
| 4 | You branded it (Nemesis Brand) | 100% |
| 5 | It survived an Ambush event | promotion chance |
| 6 | Friends crowned it on the website | 100% |

When promoted in place, the live enemy becomes the nemesis on the spot (stats applied,
purple sparkles). Banner "A NEMESIS RISES" + name, pop-up with species and the reason.
It starts at level 1 with one random trait (plus a stat point under Curse of Vendetta).

### 7.2 Growth

- **Level** = 1 + total stat points.
- Every time it **kills you**: +1 random stat (+2 under Vendetta), 50% chance of a new
  trait, wounds healed. Logged as "Grak killed you. +Power".
- Every time you **flee** it mid-fight: escapes +1, Grudge-Bearer gains Power, 25% new trait.
- Coward escapes: 35% new trait.
- Up to 6 traits.
- It **remembers its wounds**: HP percentage is saved while you fight and reapplied next time.
- Friends can add or remove stat points, rename it, or send it after you.

### 7.3 Manifesting

When the nemesis is **hunting** and in the **same overworld zone** as the player, after
5 s in the area (1 s for Ambushers) and a 60 s cooldown since its last appearance, it
spawns 380-600 units behind the player on solid ground at a similar height (the spawn
search tries 16 angles/distances and raycasts the floor). Banner: its name and
"Lv4 Wolfos - Ironhide". A purple smoke puff marks the spot. If its model can't be
loaded in this area: a pop-up "Your nemesis is here but can't reach you in this area."

### 7.4 Defeat

Killing it pays `40 + 15 x level + 5 x traits + 2 x stolen marks` (then Marks_Scale),
banner "NEMESIS DEFEATED". Then:
- 25% chance (60% for Cowards) it "survives": dormant 600 s, marked **scarred**
  (+1 Health, +50% base HP), then returns hunting.
- Otherwise it's gone for good, and a new enemy can be promoted.

---

## 8. Bounties

The **Bounty Board** holds 8 contracts. Up to **3** can be claimed. Only claimed targets
appear in the world, pay out, and produce movement pop-ups.

### 8.1 Generation

| Rank | Chance | Species difficulty | Traits | Reward multiplier |
|---|---|---|---|---|
| D | 35% | 1-3 | 0 | x1.0 |
| C | 30% | 2-5 | 1 | x1.5 |
| B | 20% | 4-6 | 1-2 | x2.2 |
| A | 11% | 5-8 | 2 | x3.2 |
| S | 4% | 6-9 | 3 | x4.5 |

- **Reward** = `difficulty x 3 x rank multiplier + 5 per trait + zone danger`, minimum 5.
  About 5 for a D Stalchild, about 30 for a C Peahat, about 135 for an S Iron Knuckle.
- **Name**: one of 24 names + species, for example "Rotfang the Peahat" (Rotfang, Mudgut,
  Bristle, Old Scratch, Nettle, Cinder, Hollow, Grimbly, Snarl, Mossback, Widow, Pale Tom,
  Ruckus, Gnash, Ember, Crooked Kit, Sallow, Brine, Thistle, Grub, Lantern, Soot, Marrow,
  Tallow).
- **Home zone**: any zone with danger 1 or more (never a town).
- **Motive**: Roaming 35%, Migrating 25% (toward a random target zone, then Nesting),
  Nesting 25% (stays; 8% chance per move roll to start roaming), Hunting you 10%,
  Fleeing 5%. Rooted species always Nest.
- **Expiry**: 30-55 world ticks (45 s each, about 22-40 minutes of play).
  Curse of Brittle Bounties makes claimed ones expire twice as fast.
- Friends can post **sponsored** bounties (shown in blue on the board with "by <name>");
  reward is at least the normal formula, capped at 300.

### 8.2 Appearing

When a **claimed** target is in the player's zone, after 6 s in the area, with a 45 s
spawn cooldown and at most 2 bounty targets present at once: it spawns 350-650 units away
in a random direction (200-320 units, behind the player, if it's Hunting or under Curse of
the Hunted). Banner: its name and "Rank B bounty target - 43 Marks". Rank acts as its
stats; its traits work exactly like the nemesis's. Wounds are remembered.

Killing it while claimed: banner "BOUNTY COLLECTED" and the reward. Killing an unclaimed
one: "That was an unclaimed bounty target. No payout." The slot then rolls a new bounty.

---

## 9. The living world (what happens when the player isn't there)

### 9.1 The zone graph

Termina's overworld is 27 zones with danger ratings (0 = town, 5 = deadliest):

South Clock Town (0), East Clock Town (0), West Clock Town (0), North Clock Town (0),
Laundry Pool (0), Termina Field (2), Road to the Swamp (2), Southern Swamp (3),
Deku Palace (1), Woodfall (3), Woods of Mystery (2), Path to the Mountain (3),
Mountain Village (2), Twin Islands (3), Goron Village (1), Path to Snowhead (4),
Snowhead (4), Milk Road (2), Romani Ranch (1), Great Bay Coast (3), Zora Cape (3),
Pirates' Fortress exterior (4), Zora Hall (0), Road to Ikana (3), Ikana Graveyard (4),
Ikana Canyon (5), Stone Tower (5).

Edges follow the real map (the Clock Town districts all connect to Termina Field, which
connects to the Road to the Swamp, Path to the Mountain, Milk Road, Great Bay Coast and
Road to Ikana, and so on). Seasonal scene variants (winter/spring Mountain Village,
poisoned/clean swamp, both Stone Towers) map to the same zone. Interiors and dungeons
map to no zone; the player's graph position is the **last outdoor zone** they stood in.
Paths use breadth-first search.

### 9.2 Independent clocks (nothing moves in lockstep)

- **Nemesis**: its own travel timer. Each interval is a random 60-140 s, divided by
  (1 + 0.5 x Tracking), halved by Relentless, halved again by Curse of Vendetta,
  minimum 15 s. When it fires, the nemesis steps one zone along the shortest path toward
  the player's last outdoor zone (unless it's already there or busy fighting).
- **Each bounty target**: its own timer, re-rolled after every move:
  Hunting you (or claimed under Curse of the Hunted) 20-50 s, Migrating 30-70 s,
  Fleeing 30-80 s, Nesting 60-150 s, Roaming 40-110 s.
  - Roaming: 30% chance to step to a random neighbour.
  - Migrating: 50% chance to step toward its target zone; arriving switches it to Nesting.
  - Nesting: stays; 8% chance to start Roaming.
  - Hunting: 60% chance to step toward the player.
  - Fleeing: 50% chance to step to a neighbour that isn't closer to the player.
  - Towns are off limits unless it's hunting the player.
  - It doesn't move while it's fighting the player.
- **World tick** every 45 s of play (only during normal gameplay) handles the slower
  things:
  - **Dormant nemesis** counts down 45 s per tick, then "stirs. It is hunting again."
  - **The nemesis hunts bounty targets**: if an unclaimed target shares its zone, 20% per
    tick it devours it. 50% chance it absorbs one of the victim's traits it lacks
    ("Grak absorbed the trait Swift"), otherwise +1 random stat. The slot re-rolls.
  - **Bounty targets fight each other**: two targets in the same zone (not the player's)
    have a 12% chance per tick to brawl. Power = difficulty + 2 x rank + random 0-4; the
    winner ranks up and gains a third of the loser's reward; the loser's slot re-rolls.
    A claimed target is never killed by an unclaimed one.
  - Contract expiry counts down.
- Everything pauses in menus, cutscenes, textboxes and transitions.

### 9.3 How it's surfaced

Movement and world news become pop-ups under the rumor rules in 3.5, so the player hears
"Grak the Unbroken was seen in Milk Road" or "Gnash the Wolfos moved to Termina Field"
while exploring elsewhere. The nemesis page shows its location only when it's in or next
to the player's zone, or after a Moon Spyglass; the bounty board always shows each target's
current zone and motive.

### 9.4 Loading enemy models across zones

Enemies can only spawn if their model (object file) is loaded. Right before an area's
first room loads (a `RECOMP_HOOK("Play_InitEnvironment")`, which runs after the scene
header and before `Room_SetupFirstRoom`), the mod appends up to **3** extra persistent
objects with `Object_SpawnPersistent`:

1. the nemesis's model if it's hunting in this zone or a neighbouring one (up to 400 KB),
2. claimed bounty targets' models in this zone, then ones in neighbouring zones (400 KB),
3. one random roaming monster for the Ambush event (up to the "object budget", 256 KB),
   harder in wilder zones.

Each is only added if at least **320 KB** of object space stays free for the area.
A `RECOMP_HOOK("Scene_CommandObjectList")` safety valve checks every room's object list
before it loads; if the room wouldn't fit, the extras are dropped (their enemies vanish,
treated as slipping away rather than dying). The Debug page shows the last result, for
example "Wolfos model loaded, 512KB free".

---

## 10. Global Events

Roughly every **180 s ± 30 s** (settings), an event starts and lasts **30-45 s**.
Rules:
- Paused during menus, cutscenes, textboxes; never starts in minigames, while any game
  timer runs, in boss arenas, on Epona, or in the intro areas.
- Dying ends the event with no payout.
- 20 s before the next event, it's pre-rolled so a Moon's Whisper or Omen Scroll can hint
  at it.
- Every event can be toggled, weighted 0-10, tuned (three settings each) and force-triggered
  from the menu. Force-triggering refuses events that can't happen where you are.
- Each dangerous event has **its own counter**; no two share a solution.
- Strikes use a hit flash and sparks (never real explosions or fire).

| # | Event | Type / risk | Counter | How it works | Settings (defaults) |
|---|---|---|---|---|---|
| 0 | Festival of Masks | Harmless | None needed | Rainbow confetti sparkles around you, rainbow screen-edge strips, a coin jingle every 4 s. | Confetti 6, Screen tint on, Music on |
| 1 | Moon's Whisper | Harmless | None needed | For 15 s shows three secrets: the next pre-rolled event, your nemesis's zone, and a bounty target's zone. Moon-dust sparkles. | Length 15 s, Sparkles 5 |
| 2 | Starfall Bounty | Benefit | Stand where stars land | Stars fall onto ringed spots 120-380 units away; standing within 60 units when one lands catches it (+1/4 heart). Pays marks per star. | Star rate 6, Fall time 3.0 s, 3 marks/star |
| 3 | Dowsing Moon | Benefit | Follow the warmth, press A | A cache is buried 420-700 units away. A "Warmth" bar and a beeping (faster when closer, rumble under 200 units) guide you; within 55 units press A to dig: 12 marks + 1 per 3 s left, heal 1 heart. | Distance 700, Base reward 12 |
| 4 | Moonfall | Danger 3 | Step out of marked circles | Red circles appear where you're heading (15 frames of your velocity ahead, ±60); a falling rock drops for 1.5 s, then an explosion hits everything within 85 units with knockback. | 7 impacts/10 s, 1 heart, 1.5 s warning |
| 5 | Giant's Gale | Danger 2 | Hold the stick into the wind | Wind shoves you (player `pushedSpeed`) from a direction that changes every 8 s; holding the stick within ~56° of straight into it cuts the push to 15%. Otherwise debris hits for 1/4 heart every 3 s. On-screen text says which way it blows. | Strength 9, Debris 3.0 s |
| 6 | Eye of the Moon | Danger 2 | Freeze when the eye opens | A big eye drawn at the top cycles closed (2.5-4.5 s) / opening (1 s warning) / open (2.5 s). While open, any stick movement, speed or A/B press hurts every 8 frames. | Warning 1.0 s, Open 2.5 s, 1/2 heart |
| 7 | Sinking Sands | Danger 2 | Keep moving | Standing still (speed < 1.5) for 1 s starts damage ticks every 1 s with sand dust; a sand disk darkens under you. | Tick 1.0 s, 1/4 heart |
| 8 | Frostbite | Danger 2 | Roll | A frost meter fills; rolling (A while grounded and moving fast) sheds 35. At 100: ice burst, damage, 3 s chill, meter resets to 30. Blue screen edges grow with the meter. | Chill speed 3, Shed 35, 1.5 hearts |
| 9 | Scythe Sweep | Danger 3 | Jump it, or stand in the gap | Every 5 s a magenta shockwave ring expands from a point 150-280 units away at 11 units/frame. When it passes you: safe if airborne or inside its green 60° gap wedge, else damage + knockback. | Every 5 s, Speed 11, 1 heart |
| 10 | Moth Swarm | Danger 2 | Spin attack | Pale moths drift in and latch on (orbiting you). Every 3 s they drain `attached/2` quarter hearts. A spin attack clears all within 120 units (other forms clear one per strike). | Swarm rate 4, Drain every 3 s |
| 11 | Tatl's Warning | Danger 2 | Press C-Up when Tatl shouts | Every 3-6 s Tatl shouts "HEY! LOOK OUT! Press C-Up!" with a 1.4 s window; heeding her makes the strike miss, otherwise a moon strike hits for 1 heart with knockback. | Window 1.4 s, 1 heart |
| 12 | Hex of Silence | Danger 1 | Don't press B or C | Every B or C-left/right/down press costs 1/2 heart (10-frame cooldown). Purple screen-edge glow. | 1/2 heart per press |
| 13 | Moon Crank | Danger 2 | Spin the stick in circles | A ward (0-100) unwinds by 12 per second; each full stick rotation adds 18. At 0 you take damage every 1.5 s. "Ward" bar. | Unwind 12/s, Charge 18/spin, 1/2 heart |
| 14 | Moon Tether | Danger 2 | Stay inside the drifting circle | A 160-unit ring appears 150-260 units away and drifts (turns every 4 s, follows the floor). After a 5 s grace, being outside yanks you toward it with damage every 1 s. Ring turns green when you're inside. | Radius 160, Drift 1.5, 3/4 heart |
| 15 | Moon's Grasp | Danger 3 | Mash A | A shadow grows under you for 1.5 s, then a hand grabs you: stick, A and B stop reaching the game; mash A 12 times within 3 s to break free, or take 1.5 hearts and get thrown. Repeats every 4-7 s. | 12 presses, 1.5 hearts |
| 16 | Heartbeat Hex | Danger 2 | Press A on the beat | A heart pulses every 1.4 s with an alarm beep; press A within 4 frames of the beat. Missing a beat costs 1/2 heart, pressing off-beat costs 1/4. | Beat 1.4 s, Window 4 frames, 1/2 heart |
| 17 | Rising Moon-Tide | Danger 3 | Climb above it | A dark translucent plane rises from 30 below your starting height to 140 above it over the first 60% of the event; after that, being below it costs 1 heart per second. HUD shows metres above/below. | Rise 140, 1 heart/s |
| 18 | Moon's Reflex Test | Danger 2 | Press the shown button | Every 2-4.5 s "PRESS A/B/R/Z" flashes up; you have 1.2 s. Wrong button or too slow costs 3/4 heart. | Window 1.2 s, 3/4 heart |
| 19 | Gorgon's Gaze | Danger 2 | Keep your camera away from the eye | A big white-and-red eye with a red beam floats 500 units away, 160 up, and moves every 7 s; an on-screen marker/arrow shows where. While it's within ~35° of your view centre, a "Stone" meter fills; at 100 you take 1.5 hearts and are chilled 2 s. | Moves every 7 s, Stare speed 4, 1.5 hearts |
| 20 | Ambush | Danger 4 | Kill them all | 3 enemies of a locally loaded roaming species (difficulty ≤ 6) appear in a ring 220-380 units around you. Kill them all to end it early (+2 marks each). Survivors vanish at the end, and one may be promoted to nemesis. | 3 ambushers |
| 21 | Dissonance | Danger 1 | Play 3 ocarina notes | A sour melody drains 1/4 heart every 3 s with wobbling lines on screen until you play any 3 notes. Needs the ocarina. | Drain every 3 s, 1/4 heart |
| 22 | Mirror Madness | Danger 2 | Face the shard and hold Z | The stick is inverted. A glass shard floats 300 units away; face it (within ~25°) and hold Z for 1.5 s to shatter it. Cuts for 1/4 heart every 5 s meanwhile. | Stare 1.5 s, Cut every 5 s, 1/4 heart |
| 23 | Volley of Stars | Danger 3 | Face the star and hold R | A glowing star appears 450 units away in a random direction; 1.5 s later its stars arrive. Guarding (R) while facing within 45° blocks them; otherwise 1 heart with knockback. Every 4 s. | Warning 1.5 s, 1 heart, Gap 4 s |
| 24 | Lonely Moon | Danger 1 | Talk to any NPC | Drains 1/4 heart every 4 s until you start a conversation with any NPC. Only happens with an NPC within 1500 units. | Drain every 4 s, 1/4 heart |
| 25 | Sleeping Giant | Danger 2 | Walk softly | Moving faster than 3.5 while grounded fills a "Noise" meter by the excess; at 100 the giant stomps (earthquake, 1.5 hearts, knockback). | Speed limit 3.5, Noise gain 6, 1.5 hearts |
| 26 | Tag, You're It | Danger 2 | Chase and touch the wisp | A wisp spawns 220-300 units away and flees at 5 units/frame (slows if you fall far behind, slips sideways when cornered, follows the floor). Touch it to win; if time runs out it tags you back for 2 hearts. | Wisp speed 5.0, 2 hearts |
| 27 | Moon's Riddle | Danger 1 | Answer with the D-pad | 3 multiple-choice riddles (63-question pool of Majora's Mask trivia and quick maths), answers shuffled onto Up/Right/Down/Left, 12 s each. Wrong or too slow costs 1 heart. Counts as failed if under half are right. D-pad is hidden from the game meanwhile. | 12 s each, 1 heart, 3 riddles |
| 28 | Mask Salesman's Game | Danger 2 | Memorise and repeat | A sequence of arrows (length 3 + rounds won, max 10) is shown one at a time (0.8 s each), then you repeat it on the D-pad within 8 s. A mistake costs 1 heart and starts a new round; win 3 rounds. | Arrow time 0.8 s, 3 rounds, 1 heart |
| 29 | Stargazer | Danger 2 | Keep the star centred in view | A star drifts 220 units up and 450 away (turns every 3 s). Keeping it within ~20° of your camera's centre fills a "Charted" bar; 6 s total wins. When it's not centred you take 1/2 heart every 3 s. Marker/arrow shows where it is. | Watch 6 s, Punish every 3 s, 1/2 heart |
| 30 | Omen Targets | Danger 3 | Shoot them down | 3 pink omens bob 90 units up, 200-420 units away. Any projectile (arrows, hookshot, nuts, Zora fins, Deku bubbles; anything in the explosive/item-action/misc actor lists) within 55 units destroys one. Survivors explode at the end: 1 heart each (max 3). Needs a projectile. | 3 omens, 1 heart each |
| 31 | Pressure Seals | Danger 2 | Step on them in order | 4 rainbow rings appear 180-450 units around you; the next one in order has a light beam. Stepping on the wrong one costs 3/4 heart (1.5 s cooldown). Finish them all to win, or take 2 hearts at the end. | 4 seals, 3/4 heart, 2 hearts |

---

## 11. Curses

One random curse per three-day cycle (never the same one twice in a row), rolled when
`threeDayResetCount` changes and announced with a banner once the player has control.
Each bends a different system:

| Curse | System | Effect |
|---|---|---|
| Curse of Haste | Events | Event interval x2/3 (about every 2 min) |
| Curse of Frailty | Damage | Event damage +50% |
| Curse of Vendetta | Nemesis | Travel twice as fast; double stat gains |
| Curse of the Hunted | Bounties | Claimed targets hunt you and spawn close behind you |
| Curse of Poverty | Moon Marks | Payouts x0.6, pouch prices x1.25 |
| Curse of Promotion | Promotion | +30% promotion chance, 2 hits instead of 3 |
| Curse of the Crowd | Friends | Friend actions half price, half cooldown |
| Curse of the Sealed Pouch | Pouch | 45 s between pouch uses |
| Curse of the Long Night | Duration | Events +15 s |
| Curse of Brittle Bounties | Contracts | Claimed contracts expire twice as fast; targets that escape flee to a neighbouring zone |

A Curse Ward suppresses it until the next dawn.

---

## 12. Friend website (short version)

A Cloudflare Worker with one Durable Object per room serves a website ("Moon Court").
The game (through a small WinHTTP DLL) posts a JSON snapshot every second and receives
queued commands as text lines (`HEAL|4|Bob`, `EVENT|12|Bob`, `BOUNTY|species|zone|reward|Bob`...).
Friends earn 1 favor per 15 s while the player is online (max 20) and spend it:
message 1, heal 3, Moonshield 4, send 5 marks 4, gift a pouch item 5, weaken the nemesis 6,
post a bounty 6, rename the nemesis 4, summon a calm event 3 or a dangerous one 6, random
event 4, chill 3, empower the nemesis 8, send it hunting 7, crown a nemesis 10, reroll the
curse 12. Every 4 minutes the room holds a free 45 s vote between 3 events. The player can
block friends entirely or allow only helpful actions, and sets the per-friend cooldown.

---

## 13. Controls summary

| Where | Input | Does |
|---|---|---|
| Near the rainbow carpenter (South Clock Town) | L | Moon Menu |
| Near the purple owl statue (South Clock Town) | L | Bounty Board |
| Ocarina out, anywhere | C-Up, C-Up, C-Down, C-Down | Moon Menu |
| Anywhere | Hold L + D-pad/C left or right | Pick an owned pouch item |
| Anywhere | Hold L + D-pad/C down | Use it |
| Menu | D-pad/stick, A, B, Left/Right, Start | Move, select, back, change values, close |
