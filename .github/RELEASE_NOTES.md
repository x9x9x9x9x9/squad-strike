Squad Strike for Melee PC: Squad Strike teams, the ACE stages (Metal Cavern,
WarioWare, Realgam Colosseum), FROZEN Pokémon Stadium, Stage Expansion, SD
Remix characters, connect codes and better controller support, on top of
[melee-pc](https://github.com/999sian/melee-pc). Full feature list, controls
and credits are on the [project page](https://github.com/x9x9x9x9x9/squad-strike#readme).

You need your own **Super Smash Bros. Melee NTSC-U 1.02 (GALE01)** disc image.

## What's new in v1.0.5

- **Squad Strike in doubles and free-for-all:** with three or four players
  each squad is two fighters. In a team battle, squad members wear their
  team's colour.
- **Two players per computer online** (LAN and Direct): launcher → Settings
  → Online → Players on this computer = 2. The host plays P1 and P3, the
  guest P2 and P4.
- **Four computers, one player each (experimental):** Online → Direct
  Connect → Host a party, give out your code, and the others call it as
  usual. Every pair connects directly; a pair that can't is passed through
  another player automatically. Direct Connect only for now, quick chat is
  off in a party, and one player dropping ends the match for all four.
- **Fixes:** a crash drawing Link's bow; fighters cut off past the 4:3 edge
  in widescreen; music volume no longer silencing some sound effects (Kirby's
  inhale); move flags read wrong on PC; Green Greens' Whispy; team-battle
  edge cases (draws, stage chooser); a UPnP crash; a verified disc is
  remembered.

How to play doubles, step by step:
[Playing doubles](https://github.com/x9x9x9x9x9/squad-strike#playing-doubles).

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
