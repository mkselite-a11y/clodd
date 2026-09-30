# Moonfall Mayhem: design reference

Everything below matches the code in `mod/src`. Damage values are in quarter hearts
and are scaled by the Master Difficulty (Gentle 60%, Normal 100%, Cruel 150%,
Merciless 200%) and the in-game "Mod difficulty %" setting.

## Randomizer safety

The mod is built to sit on top of an Archipelago randomizer run without touching logic:

- It never gives items, rupees, upgrades, songs, masks or movement abilities.
- The Moon Pouch only heals, protects, refills ammo **for gear you already own**, or
  manipulates the mod's own systems. No speed boosts, no jump boosts, no time control.
- Events never spawn real bombs or fire. Explosions and flames are visual effects and
  the damage is calculated by the mod, so bombable walls, webs and ice can't be opened.
- Enemies that can steal gear (Like Like, Takkuri), grab and warp you (Wallmaster) or
  explode (Real Bombchu) are excluded from the nemesis and bounty rosters.
- The mod keeps its own save file next to the game's (`<save>.moonfall<slot>.bin`),
  so the randomizer's save data is never modified.
- Only hooks and Recomp events are used, no function patches, so it can load next to
  the Archipelago mod without patch conflicts.

## Global Events

One event roughly every 3 minutes (180s +/- 30s), lasting 30-45s. Dying ends the event
with no payout. Surviving a Danger event pays `3 + 2 x risk` Moon Marks (halved if you
failed its counter at least once, +15% while a curse is active, +25% if a friend sent it).
Events pause during menus, cutscenes and textboxes, and never start during minigames,
running timers, boss fights, on Epona or in the intro.

Each Danger event has one counter, and no two events share the same one.
Every event can be switched off, reweighted, tuned and force-triggered from
**Moon Menu > Global Events**.

| # | Event | Type | Risk | Counter | Settings (default) |
|---|---|---|---|---|---|
| 0 | Festival of Masks | Harmless | - | Nothing to fear. Enjoy the show! | Confetti (6), Screen tint (1), Music (1) |
| 1 | Moon's Whisper | Harmless | - | Listen. The moon is spilling secrets. |  |
| 2 | Starfall Bounty | Benefit | - | Stand where the stars land to catch them! | Star rate (6) |
| 3 | Dowsing Moon | Benefit | - | Follow the warmth, then press A to dig. |  |
| 4 | Moonfall | Danger | 3 | Step out of the red circles before impact! | Impacts/10s (7) |
| 5 | Giant's Gale | Danger | 2 | Hold the stick INTO the wind to brace. | Wind strength (9) |
| 6 | Eye of the Moon | Danger | 2 | When the eye opens, FREEZE. Don't move! |  |
| 7 | Sinking Sands | Danger | 2 | Keep moving! Standing still sinks you. |  |
| 8 | Frostbite | Danger | 2 | Roll to shake off the frost before it freezes you. | Chill speed (3), Frost shed/roll (35), Freeze damage (6) |
| 9 | Scythe Sweep | Danger | 3 | Jump the shockwave, or stand in its green gap. |  |
| 10 | Moth Swarm | Danger | 2 | Spin attack to scatter the clinging moths! | Swarm rate (4) |
| 11 | Tatl's Warning | Danger | 2 | When Tatl shouts, press C-Up to heed her! |  |
| 12 | Hex of Silence | Danger | 1 | Don't attack or use items. Every press hurts. | Damage/press (2) |
| 13 | Moon Crank | Danger | 2 | Spin the stick in circles to keep the ward wound! | Unwind/s (12), Charge/spin (18), Damage (2) |
| 14 | Moon Tether | Danger | 2 | Stay inside the drifting circle! |  |
| 15 | Moon's Grasp | Danger | 3 | When it grabs you, MASH A to break free! | Presses needed (12), Damage (6) |
| 16 | Heartbeat Hex | Danger | 2 | Press A exactly on each heartbeat. |  |
| 17 | Rising Moon-Tide | Danger | 3 | Climb! Get above the rising dark tide. |  |
| 18 | Moon's Reflex Test | Danger | 2 | Press the button that flashes up. Fast! |  |
| 19 | Gorgon's Gaze | Danger | 2 | Keep your camera turned AWAY from the eye. |  |
| 20 | Ambush | Danger | 4 | Defeat every ambusher before time runs out! | Ambushers (3) |
| 21 | Dissonance | Danger | 1 | Play any 3 notes on your ocarina to answer it. |  |
| 22 | Mirror Madness | Danger | 2 | Controls inverted! Face the shard, hold Z to shatter it. |  |
| 23 | Volley of Stars | Danger | 3 | Face the glowing star and hold R to guard! |  |
| 24 | Lonely Moon | Danger | 1 | Talk to someone, anyone, to break the spell. |  |
| 25 | Sleeping Giant | Danger | 2 | Tiptoe! Running makes noise and wakes the giant. |  |
| 26 | Tag, You're It | Danger | 2 | Chase down the wisp and touch it! |  |
| 27 | Moon's Riddle | Danger | 1 | Answer with the D-pad. Wrong answers hurt. |  |
| 28 | Mask Salesman's Game | Danger | 2 | Memorise the arrows, then repeat them on the D-pad. |  |
| 29 | Stargazer | Danger | 2 | Keep the wandering star centred in your view! |  |
| 30 | Omen Targets | Danger | 3 | Shoot the omens down with any projectile! | Omens (3), Damage each (4) |
| 31 | Pressure Seals | Danger | 2 | Step on the seals in order: follow the beam. | Seals (4), Wrong-step damage (3), Fail damage (8) |

Eligibility notes: Ambush needs an enemy model in the area (outdoor areas load one for it), Dissonance needs the ocarina, Lonely Moon needs a nearby NPC, and Omen Targets needs a projectile (bow with arrows, hookshot, Deku nuts, or the Zora/Deku masks). Moon's Riddle draws from 63 questions.

## Curses (one per three-day cycle)

| Curse | System it bends | Effect |
|---|---|---|
| Curse of Haste | Events | Events arrive about every 2 minutes |
| Curse of Frailty | Damage | Event damage +50% |
| Curse of Vendetta | Nemesis | Nemesis tracks twice as fast; double stat gains |
| Curse of the Hunted | Bounties | Bounty targets move toward you and ambush closer |
| Curse of Poverty | Moon Marks | Payouts -40%, pouch prices +25% |
| Curse of Promotion | Promotion | Enemies become nemeses much more easily |
| Curse of the Crowd | Friends | Friend actions cost half and cool down twice as fast |
| Curse of the Sealed Pouch | Pouch | 45s between Moon Pouch uses |
| Curse of the Long Night | Duration | Events last 15s longer |
| Curse of Brittle Bounties | Contracts | Claimed contracts expire twice as fast; escaped targets flee |

A Curse Ward (pouch) suppresses the curse until the next dawn.

## Nemesis system

Only one nemesis at a time. An ordinary enemy from the roster is promoted when it:

1. kills you (always),
2. hits you 3 times and lives (2 under Curse of Promotion),
3. sees you leave the area mid-fight,
4. survives at a quarter health or less for 20 seconds,
5. outlasts an Ambush event,
6. is branded with a Nemesis Brand, or is crowned by friends.

Chances for 2-5 use the Promotion chance setting (default 60%).

**Stat points.** Every time the nemesis kills you it gains a random stat point:

| Stat | Effect per point |
|---|---|
| Health | +40% health (plus +1 base health per 3 levels) |
| Power | +20% extra damage on every hit it lands |
| Tracking | +50% travel speed across zones |
| Speed | +10% movement speed (capped at +80% total) |

**Traits** (starts with 1, up to 6; 50% chance to gain one per kill, 25% when you flee,
35% when it escapes):

Ironhide, Regenerating, Berserker, Vampiric, Venomous, Frostbrand, Stormcaller,
Pyromaniac, Blinker, Ambusher, Relentless, Thief, Mark-Hungry, Moonblessed, Giant,
Swift, Hexcaller, Coward, Warlord, Grudge-Bearer. Descriptions are in the Nemesis page
of the Moon Menu.

**Hunting.** The nemesis lives on a graph of 27 Termina zones. It keeps its own travel
clock: every 60-140 seconds of play (faster with Tracking, Relentless or Vendetta) it
walks one zone toward the last outdoor zone you stood in. When you're in the same
zone, it appears 400-600 units behind you (Ambushers appear right behind you, without
a banner). It remembers its wounds between fights. Killing it pays
`40 + 15 x level + 5 x traits` Moon Marks; there is a 25% chance (60% for Cowards)
it "survives" and returns scarred later. It also hunts unclaimed bounty targets it
meets, absorbing their traits.

## Bounties

The Bounty Board (South Clock Town owl statue, or the Moon Menu) holds 8 contracts.
Claim up to 3. Claimed targets appear when you enter their zone.

| Rank | Chance | Species difficulty | Traits | Reward multiplier |
|---|---|---|---|---|
| D | 35% | 1-3 | 0 | x1.0 |
| C | 30% | 2-5 | 1 | x1.5 |
| B | 20% | 4-6 | 1-2 | x2.2 |
| A | 11% | 5-8 | 2 | x3.2 |
| S | 4% | 6-9 | 3 | x4.5 |

Reward = `difficulty x 3 x rank multiplier + 5 per trait + zone danger` (minimum 5).
Examples: a D-rank Stalchild pays about 5, a C-rank Peahat about 30, an S-rank Iron
Knuckle about 135.

Targets have motives (Roaming, Migrating, Nesting, Hunting you, Fleeing) and move
between zones on their own, each on its own random timer (20-50s when hunting you,
up to 150s when nesting), so the world never moves in lockstep. Targets that share a zone can fight; the winner ranks up
and takes part of the loser's bounty. Contracts expire after 22-40 minutes of play.

### Enemy roster

| Enemy | Difficulty | Can be a nemesis |
|---|---|---|
| Deku Baba | 1 | no (rooted) |
| Stalchild, Chuchu, Keese, Guay | 1 | yes |
| Mad Scrub | 2 | no (rooted) |
| Leever, Fire Keese, Black Boe, Tektite | 2 | yes |
| Snapper, Dragonfly, Eeno, Poe, Bubble | 3 | yes |
| Freezard | 3 | no (rooted) |
| Armos, Dodongo, Floormaster, Hiploop | 4 | yes |
| Wolfos, ReDead | 5 | yes |
| Peahat, White Wolfos, Garo | 6 | yes |
| Dinolfos | 7 | yes |
| Iron Knuckle | 9 | yes |

Nemeses and claimed bounty targets can follow you into areas where their model isn't
normally loaded: the mod loads one or two extra enemy models when the area loads
(up to 3 extra models, as long as 320 KB stays free for the area's own) and drops them automatically if a
room needs the memory.

## Moon Marks and the Moon Pouch

Earned from surviving events, benefit events, bounties you claimed, and nemesis kills.
Spent in the Moon Pouch Shop (Moon Menu). In play: hold **L**, press **D-pad or
C left/right** to pick an item, **D-pad or C down** to use it (**D-up** jumps to the next
item you own).

| Item | Cost | Max | Effect |
|---|---|---|---|
| Lunar Tonic | 8 | 5 | Heal 3 hearts |
| Full Moon Draught | 20 | 3 | Full heal |
| Moonshield | 15 | 3 | 8s of no damage |
| Stillwater Charm | 18 | 3 | End the current event as survived (half reward) |
| Omen Scroll | 6 | 5 | Delay the next event by 60s |
| Nemesis Lure | 12 | 3 | Your nemesis comes to your zone now |
| Smoke Veil | 14 | 3 | Nemesis loses your trail (3 min dormant, 2 zones away) |
| Bounty Compass | 6 | 5 | 60s: distance and direction to claimed targets |
| Hunter's Whetstone | 16 | 3 | 90s: +50% damage to nemesis and bounty targets |
| Moonsteel Ward | 20 | 3 | 60s: damage taken halved |
| Second Wind | 30 | 2 | Passive: survive one lethal blow with 4 hearts |
| Magic Flask | 10 | 3 | Refill magic (needs the magic meter) |
| Quiver Refill | 8 | 3 | Refill arrows (needs the bow) |
| Bomb Satchel | 8 | 3 | Refill bombs/bombchus you carry |
| Deku Bundle | 5 | 3 | Refill sticks/nuts you carry |
| Curse Ward | 35 | 2 | Suppress the curse until dawn |
| Mark Magnet | 15 | 3 | Next 3 payouts +50% |
| Moon Spyglass | 4 | 3 | Reveal your nemesis's location for this cycle |
| Stasis Orb | 22 | 3 | 6s: all enemies frozen |
| Riposte Charm | 16 | 3 | 60s: attackers take damage back |
| Event Tempter | 5 | 5 | Start a random event now (+50% reward) |
| Nemesis Brand | 25 | 1 | The next enemy you hit becomes your nemesis |
| Bounty Reroll | 5 | 5 | Replace all unclaimed bounties |
| Heart Siphon | 18 | 3 | 45s: each hit you land heals 1/4 heart |

## How the systems feed each other

- Events can create nemeses (Ambush survivors) and nemeses can create events (Hexcaller).
- Curses each bend one system: events, damage, nemesis, bounties, economy, promotion,
  friends, pouch, duration or contracts.
- The nemesis eats bounty targets and steals their traits; bounty targets fight each other.
- Moon Marks from events and bounties buy tools that manipulate all of the above
  (lures, veils, brands, compasses, wards).
- Friends spend favor to trigger events, post bounties, crown, empower or weaken
  the nemesis, reroll the curse or help you out, and the Court votes on events.
- News of what happens off-screen pops up as rumors while you play.
