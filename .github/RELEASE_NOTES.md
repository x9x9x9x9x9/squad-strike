Squad Strike for Melee PC: Squad Strike teams, the ACE stages (Metal Cavern,
WarioWare, Realgam Colosseum), FROZEN Pokémon Stadium, Stage Expansion, SD
Remix characters, connect codes and better controller support, on top of
[melee-pc](https://github.com/999sian/melee-pc). Full feature list, controls
and credits are on the [project page](https://github.com/x9x9x9x9x9/squad-strike#readme).

You need your own **Super Smash Bros. Melee NTSC-U 1.02 (GALE01)** disc image.

## What's new in v1.0.1

- **Default rules:** 4 stocks with an 8:00 time limit, and a new default
  Random Stage list. The ACE stages get Random Stage switches, and Random
  can land on them.
- **Random characters** are now evenly spread, come in a random costume
  (not always the default), and P1 and P2 never get the same fighter in the
  same squad position.
- **Match timer** shows (counting up) in untimed stock matches.
- **Warlock Punch / Falcon Punch** (and Kirby's copies): fixed the flat
  purple / orange body; it's a faint glow again, as on console.
- **Same fighter, same costume** (outside teams): the second player is
  tinted lighter, as in Slippi.
- **New settings:** Post match results screen (Gameplay; online the host
  decides) and Magnifying glass (Video).
- **Ping display** is always the short "Ping: 34ms", even with RUN-AND-LOG.bat.
- The log now records each sound bank loaded, to track down missing sounds.

## Download

| System | File |
|---|---|
| Windows (most PCs) | `Melee-Windows-x86_64.zip` |
| Windows on ARM | `Melee-Windows-arm64.zip` |
| Linux | `Melee-x86_64.AppImage` (or `melee-linux-x86_64.tar.gz`); `aarch64` for ARM |
| macOS (Apple Silicon / Intel) | `Melee-macOS-arm64.zip` / `Melee-macOS-x86_64.zip` |

## Install (Windows)

1. Download `Melee-Windows-x86_64.zip` below and unzip it to a folder.
2. Run `melee.exe` (SmartScreen: More info, then Run anyway).
3. Choose Disc, pick your Melee 1.02 image, press Play.

Keep the whole folder together: `resources/` holds the ACE stages and
`sdremix/` the SD Remix fighters. `RUN-AND-LOG.bat` starts the game with a log
for bug reports. Online, both players need this same release.

## Character select (Squad Strike)

- **A**: drop your coin as usual. **Z** on a portrait: add that fighter (up to
  four). **Z** on Random: add a random fighter.
- **R**: remove the last fighter added. Hold **L**: clear the squad.
- Squads fight in the order you picked them, one stock each.

Rumble is off by default (Settings, Gameplay). This build has no auto-updater:
new versions are posted here.
