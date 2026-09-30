# Moonfall Mayhem

A chaos layer for **Majora's Mask: Recompiled**, built to run on top of an
Archipelago randomizer: Global Events, a Shadow of Mordor style Nemesis system,
Bounties, Moon Marks with a Moon Pouch, one curse per cycle, and a website where your
friends can watch and meddle.

- **32 Global Events** (2 harmless, 2 beneficial, 28 dangerous), roughly every
  3 minutes for 30-45 seconds. Every dangerous event has its own unique counter.
  All of them are tunable and can be force-triggered.
- **10 Curses**, one rolled per three-day cycle, each bending a different system.
- **Nemesis system**: enemies that kill you, humiliate you or watch you run get
  promoted, named and remembered. They gain a stat point (Health, Power, Tracking,
  Speed) every time they kill you, evolve up to 6 of 20 traits, and walk across
  Termina to find you.
- **Bounty Board**: named, trait-bearing targets with motives that roam, migrate,
  nest, hunt you and fight each other off-screen. Rewards scale with how dangerous
  the enemy really is (a Stalchild pays little, a Peahat a lot).
- **Moon Marks and the Moon Pouch**: 24 items bought with Moon Marks and used with
  L + D-pad (or L + C buttons).
- **Moon Court website**: friends earn favor while watching and spend it to help,
  cause chaos, post bounties, crown or empower your nemesis, or vote on the next event.

Full numbers and tables: [docs/DESIGN.md](docs/DESIGN.md).
Website hosting: [docs/HOSTING.md](docs/HOSTING.md).

## Install

1. Get `moonfall_mayhem.nrm` and `moonfall_net.dll`, from this repository's GitHub
   Actions build (Actions > latest run > **moonfall-mayhem** artifact) or by building
   them yourself (below).
2. Put **both files** in MM Recomp's `mods` folder (Recomp's mod menu has a button that
   opens it). The DLL has to sit next to the `.nrm`.
3. Enable **Moonfall Mayhem** in the mod menu. Requires MM Recomp 1.2.2 or newer.

The friend website is optional; see [docs/HOSTING.md](docs/HOSTING.md).

## Controls

| Where | Input | Does |
|---|---|---|
| Near the rainbow carpenter carrying a plank (South Clock Town) | L | Open the Moon Menu |
| Near the glowing owl statue (South Clock Town) | L | Open the Bounty Board |
| Anywhere, with the ocarina out | C-Up, C-Up, C-Down, C-Down | Open the Moon Menu |
| Anywhere | Hold L + D-pad/C left or right | Pick a Moon Pouch item |
| Anywhere | Hold L + D-pad/C down | Use it |
| Anywhere | Hold L + D-Up | Jump to the next item you own |
| Moon Menu | D-pad/stick, A, B, Left/Right, Start | Move, select, back, change values, close |

The world is frozen while the Moon Menu is open. The carpenters leave on the final
night; the ocarina shortcut still works then.

## Building

Requirements: `clang` and `ld.lld` (LLVM 18; LLVM 19.1.0 has a MIPS bug), `make`,
`RecompModTool` from [N64Recomp](https://github.com/N64Recomp/N64Recomp), and
`x86_64-w64-mingw32-gcc` for the DLL. On Windows, `choco install llvm --version 18.1.8`
and `choco install make mingw` work.

```sh
git clone --recursive https://github.com/mkselite-a11y/clodd
cd clodd/mod
make
RecompModTool mod.toml build        # -> build/moonfall_mayhem.nrm
cd ../native
make                                 # -> moonfall_net.dll
```

The GitHub Actions workflow in `.github/workflows/build.yml` does all of this and
uploads both files.

## Layout

| Path | What |
|---|---|
| `mod/` | The Recomp mod (C, compiled to MIPS). `src/` has one file per system. |
| `native/` | `moonfall_net.dll`: HTTPS client for the friend server and save-file IO |
| `server/` | Cloudflare Worker + Durable Object and the Moon Court website |
| `docs/` | Design reference and hosting guide |

## Status

The mod compiles and packages cleanly against MM Recomp's symbol set, and the server
and website were tested locally with a simulated game. It has **not been played in
game yet**, so expect tuning and bug fixes after the first sessions. Enemy spawn
positions, event balance and the extra-enemy-model loader are the areas most likely
to need adjustment. The Debug page of the Moon Menu is there to speed that up.
