Squad Strike for Melee PC: Squad Strike teams, the ACE stages (Metal Cavern,
WarioWare), FROZEN Pokémon Stadium, Stage Expansion, SD
Remix characters, connect codes and better controller support, on top of
[melee-pc](https://github.com/999sian/melee-pc). Full feature list, controls
and credits are on the [project page](https://github.com/x9x9x9x9x9/squad-strike#readme).

You need your own **Super Smash Bros. Melee NTSC-U 1.02 (GALE01)** disc image.

## What's new in v1.1.01

- **20XX Falcon's shine works like Fox's.** His grounded down-B (the fiery
  explosion, same 10% hit) now comes out on frame 1, can be jump-cancelled
  from frame 4, stays out while you hold B, and turns around with the
  stick. This is the timing 20XX's own code gives its shine.
- **20XX Falcon in Training.** D-pad left with Falcon on your door switches
  him there too; hold the CPU's coin and press it to switch the CPU's Falcon.
- Both players need v1.1.01 for online matches.

## Also in v1.0.12

- **Fix:** crashes when Kirby swallowed Mr. Game & Watch, as he swallowed
  him, used the chef pan or cast his shadow (worst in Squad Strike, when
  Kirby came in after another fighter). Flat Kirby keeps his eyes and
  mouth, and his pan is black like Game & Watch's own.
- **20XX Falcon's shine can be jump-cancelled** from frame 11, two frames
  after its hit comes out (like Fox's shine).
- **Neutral spawns in 1v1:** with two players, the lower port starts on one
  side and the higher port on the other, facing each other, whatever ports
  you're on.
- `RUN-AND-LOG.bat` now asks for the three log files it writes.
- Both players need v1.0.12 for online matches.

## Also in v1.0.11

- **Updater.** The launcher tells you when a new version is out. On Windows,
  **Update now** downloads, checks and installs it, then restarts. Your
  settings, replays and disc stay. (Settings → Launcher → Check for updates.)
- **Stats for every game.** A on a replay opens its page: kills, deaths,
  damage, openings, neutral wins, best punish, L-cancels, APM and every
  stock lost. **Stats** on the Replays page adds up your record by fighter,
  opponent and stage, using each game's real result (Squad Strike and
  time-outs included).
- **20XX Captain Falcon.** On character select, D-pad left with Falcon
  picked switches to the 20XX HackPack's SD Remix Falcon (a quick,
  shine-like grounded down-B). Each player picks their own.
- **Esc** opens the in-game menu, and its new **Launcher** button takes you
  back to the launcher without closing the game.
- **Magnifying glass: Widescreen (top only)** replaces Off: the bubble shows
  only for a fighter above the screen.
- **Fix:** music on the ACE stages (Metal Cavern, WarioWare) could turn to
  static and crash the game at the next screen. Each block of music is now
  checked as it plays, and the stage files are kept in memory.
- **Fix:** a crash freeing a file that was never loaded (seen on Pokémon
  Stadium online). Crashes now also leave a full report in melee-pc.log.
- Both players need v1.0.11 for online matches.

## Also in v1.0.10

- **Replays.** Every match, offline and online, is saved as a small replay
  file. Watch them from **Replays** on the launcher: pause, slow motion,
  fast forward, frame by frame and rewind. Settings → Gameplay → Save
  replays turns saving off.
- **Export to video.** Any replay can be saved as an MP4 with sound, ready to
  edit or share (built in on Windows; Linux and macOS need ffmpeg).
- **Fix:** a desync in the last frames of an online match, as it went to the
  results screen. The DESYNC warning also clears itself now once the two
  games are back in sync, instead of staying up for the rest of the session.
- Both players need v1.0.10 for online matches.

## Also in v1.0.9.1

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

Rumble is off by default (Settings, Gameplay). The launcher tells you when a
new version is out (on Windows, **Update now** installs it).
