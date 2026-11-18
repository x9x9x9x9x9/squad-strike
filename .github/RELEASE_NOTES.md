Squad Strike for Melee PC: Squad Strike teams, the ACE stages (Metal Cavern,
WarioWare), FROZEN Pokémon Stadium, Stage Expansion, SD
Remix characters, connect codes and better controller support, on top of
[melee-pc](https://github.com/999sian/melee-pc). Full feature list, controls
and credits are on the [project page](https://github.com/x9x9x9x9x9/squad-strike#readme).

You need your own **Super Smash Bros. Melee NTSC-U 1.02 (GALE01)** disc image.

## What's new in v1.0.9.1

- **Realgam Colosseum is removed.** Matches on it were crashing the game,
  and with some graphics drivers freezing the whole PC. The other ACE
  stages, Metal Cavern and WarioWare, stay. If you unzip over an older
  folder, Colosseum stays hidden even though its old files are still there.
- Both players need v1.0.9.1 for online matches.

## Also in v1.0.9

- **Fix:** a crash partway through a match (seen on Realgam Colosseum) when
  a fighter's animations had been damaged in memory on one of the two
  computers. The game now reloads that fighter's animations from the disc
  and carries on, and both players stay in sync.
- **Colosseum music guard:** the stage's music can no longer write past its
  own buffer into fighter data. A bad block of music plays as a moment of
  wrong sound instead.
- Holding **L** to clear your squad now takes 1 second (was 1.5).

## Also in v1.0.8

- **Faster Direct Connect.** Calling a friend you've played before now goes
  straight to where the game reached them last time, so it usually connects
  in a few seconds instead of 15 or more. The first call after updating
  still searches as before; from then on it's quick.
- The game keeps the same network port between calls, so a friend's game
  no longer chases addresses from your earlier calls.
- While waiting for a call, the game opens the way for friends you've
  played in the last 30 days, so their call gets through sooner.
- The search for a new friend repeats faster for the first half minute.

## Also in v1.0.7


- **Same costume in Squad Strike:** squad members can now wear the same
  colour as another player's fighter too (before, only the main pick could).
- **Fix:** a fighter that turned lighter mid-match (a squad member swapping
  in on the same character) flashed the wrong colour, e.g. a green Nana.

## Also in v1.0.6


- **Zelda starts as Sheik.** Down-B turns her into Zelda as usual; hold
  **A** while the match loads to start as Zelda.
- **Same character, same costume:** two players can pick the same fighter in
  the same colour. In the match, the higher port is drawn a little lighter.
- **Repeat fighters in a squad:** Squad Strike lineups like Fox, Fox, Falco,
  Fox. Press Z on a fighter already in your squad to add another.
- **Random P1 and P2 online:** a coin flip on connecting decides who is P1,
  and it stays that way until you disconnect.
- **Fix:** a crash on a ledge grab after a slow disc read mid-match. The game
  now waits for the read (up to 30 seconds; the other player sees a pause)
  instead of carrying on without the file.

## Also in v1.0.5

- Squad Strike in doubles and free-for-all (two fighters a squad with three
  or four players), two players per computer online, and four computers one
  player each (experimental, Direct Connect → Host a party).

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
