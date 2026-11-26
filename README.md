# Squad Strike for Melee PC

A mod of [melee-pc](https://github.com/999sian/melee-pc), the native PC port of
Super Smash Bros. Melee. It adds **Squad Strike** (teams of up to four
fighters in a 1v1, two each in doubles), **online doubles** (two players per
computer, or four computers), three stages from the **ACE build**, **SD Remix**
characters, **Stage Expansion** layouts, a FROZEN Pokémon Stadium pick,
Slippi-style connect codes for online play, and better controller support.

You need your own legally acquired **Melee NTSC-U 1.02 (GALE01)** disc image. No Nintendo game
data is included.

<img width="1922" height="1437" alt="squad strike in game" src="https://github.com/user-attachments/assets/73568ca2-4ce9-4467-a3eb-397c80465d06" />
<img width="1966" height="1437" alt="squad strike done" src="https://github.com/user-attachments/assets/0b4dbb0a-8f16-464a-b86e-5f36cc27c923" />


## Features

### Squad Strike
- Build a squad of up to **four fighters** on the VS character select screen.
  Each fighter has one stock. When one is KO'd, the next one drops in on the
  revival platform.
- Squads fight **in the order you picked them**: first pick first, second
  pick second, and so on.
- Works offline and online in **LAN Play** and **Direct Connect** (both
  players build their squads on the same screen). It is not offered in
  Unranked or Ranked.
- The star counter on the results screen counts **games won**.
- Turn it on or off in the launcher under Settings → Gameplay → Squad Strike.

### Stages
- **ACE stages:** Metal Cavern (from Smash 64) and WarioWare, each with
  its ACE music. (Realgam Colosseum was taken out in v1.0.9.1: matches on
  it were crashing.) Their tiles sit under the stage grid,
  next to FROZEN. The files come with the download, so you don't need the
  ACE disc.
- **FROZEN Pokémon Stadium:** a second Stadium tile under the grid, tagged
  FROZEN. Pick it for a Stadium that never transforms. The normal tile is
  regular Stadium.
- **Stage Expansion:** UnclePunch's hazard-free layouts for Peach's Castle,
  Onett, Mute City, Jungle Japes, Great Bay, Yoshi's Island, Green Greens,
  Fourside, Mushroom Kingdom I and II, Flat Zone and more. On by default; the
  ACE tiles follow this setting too.
- **Loser picks the stage** between games online.
- **Random stage list:** Random picks from a tournament-style list by
  default (Yoshi's Story, Fountain of Dreams, Pokémon Stadium, Onett,
  Mushroom Kingdom, Jungle Japes, Yoshi's Island, Green Greens, Fourside,
  Flat Zone, Battlefield, Final Destination, Dream Land N64 and the three ACE
  stages). Change it under VS Mode → Rules → Additional Rules → Random Stage,
  where the ACE stages now have their own switches.


<img width="1787" height="1437" alt="squad fourside" src="https://github.com/user-attachments/assets/f945fb7e-97a4-48a7-9f82-d5aa91b0537f" />

<img width="1955" height="1437" alt="squad on wartioware" src="https://github.com/user-attachments/assets/6d6d12de-7f19-44a9-979a-879b30977fa2" />


### Characters
- **SD Remix** characters (balance changes from sdremix.games). Turn on
  Settings → Gameplay → SD Remix characters. The fighter files come with the
  Windows download.
- **20XX Captain Falcon:** on character select, press **D-pad left** with
  Falcon picked (or under your coin) to switch to the 20XX HackPack's SD
  Remix Falcon, whose grounded down-B is a quick, shine-like kick. A
  "20XX FALCON" tag shows under your name; press again to switch back. Each
  player picks their own, and it works with SD Remix on or off. Online, while
  your door shows Falcon, D-pad left is his switch rather than chat.
- Kirby's throws work correctly.
- **Zelda starts as Sheik.** Down-B turns her into Zelda as usual. To start
  a match as Zelda, hold **A** while the match loads.
- **Same character, same costume:** two players can pick the same fighter in
  the same colour (X/Y lands on it). In the match, the higher port is drawn a
  little lighter so you can tell them apart.

<img width="1827" height="1437" alt="squad mods" src="https://github.com/user-attachments/assets/36e32faf-57b7-41c2-9b3f-4a095bf44a2d" />

### Online
- **Connect codes**, Slippi style: pick your own code like `CFOX#123`, give it
  to a friend, and they call you from Online → Direct Connect.
- Direct Connect works on the same home network and over **phone hotspots**,
  and asks your router to open a port (UPnP) when it can.
- **Doubles online**, two ways: two players on each of two computers, or
  (new, experimental) four players on four computers. See
  [Playing doubles](#playing-doubles).
- **Ping display** in the top left corner during online matches (can be
  turned off).
- **Random P1 and P2:** when two computers connect, a coin flip decides who
  is P1. It stays that way until you disconnect.
- Rollback netcode from melee-pc.

<img width="1821" height="1437" alt="squad connect codes" src="https://github.com/user-attachments/assets/b822a044-1931-44db-bd35-b5878b05c466" />

<img width="1810" height="1437" alt="squad other online" src="https://github.com/user-attachments/assets/62da45d6-bf00-4907-a170-1abaa6347218" />

### Replays
- **Every match is saved**, offline and online, as a small replay file (a few
  hundred KB at most): the match setup plus each frame's controller inputs,
  like Slippi. Settings → Gameplay → Save replays turns it off.
- **Watch them** from **Replays** on the launcher's main menu. While one
  plays: **Start** pauses, **L / R** (or D-pad left/right) slow it down or speed
  it up from ¼x to 4x, **A** steps one frame while paused, **D-pad down**
  rewinds about 10 seconds, **B** goes back to the list.
- **Export Video** turns a replay into an MP4 (H.264 and AAC sound) in your
  Videos folder, under *Melee Replays*. Built in on Windows; on Linux and macOS
  it needs `ffmpeg` installed.
- A replay plays back on the same game files: your disc, and the same SD
  Remix files if SD Remix was on. If a playback ever drifts from what was
  recorded, the bar at the bottom says **OUT OF SYNC**.
- Replays are stored in the `replays` folder beside your settings (see
  [Settings](#settings)); **Open Folder** on the Replays page opens it.
- **A on a replay** opens its page: a **Summary** with a column per player
  (result, kills, deaths, self-destructs, damage dealt and taken, openings,
  damage per opening, openings per kill, neutral wins, best punish,
  L-cancels, ledge grabs, actions per minute), the **Stocks** each player
  lost (when, at what damage, to whom), and **Watch**. Games from before
  v1.0.11 have only their final stocks.
- **Stats** on the Replays page adds up your record: wins and losses, win
  rate, streak and last 10 games, and tables by fighter, opponent and stage.

### Updates
- The launcher looks for a new release each time it opens. On Windows,
  **Update now** downloads it, checks it, installs it and restarts; your
  settings, replays and disc stay. Linux AppImages update the same way;
  other builds open the release page. Settings → Launcher → Check for
  updates turns it off, and **Check now** looks again.

### Controllers
- Official and third-party **GameCube controller adapters**, including the
  HandHeldLegend RP2040 adapter in Slippi mode, at 1000 Hz.
- **Generic USB controllers** that SDL doesn't know get a sensible default
  mapping. The Raphnet N64 adapter is ignored.
- **Rumble is off by default.** Turn it on under Settings → Gameplay → Rumble.
  While it's on, a filter removes the one-report input glitches some
  adapters produce when the motor runs.
- Keyboard play (see [Controls](#controls)).

## How to install

1. **Download** the latest release from the
   [Releases page](https://github.com/x9x9x9x9x9/squad-strike/releases/latest)
   (no GitHub account needed): **Melee-Windows-x86_64.zip** for most Windows
   PCs, or the Windows ARM, Linux or macOS file for your system.
2. **Unzip** it to a folder of your choice. Keep everything in that folder
   together: `melee.exe`, the DLLs, `resources/` (which holds the ACE stages)
   and `sdremix/`.
3. **Run** `melee.exe`. If Windows SmartScreen warns you, click
   **More info → Run anyway**.
4. **Choose Disc** and pick your Melee 1.02 ISO (`.iso`, `.gcm`, `.ciso`,
   `.rvz`, `.gcz` or `.wia`). **Verify Disc** checks it against the original.
5. Press **Play**. The first launch compiles shaders for a moment.

If something goes wrong, run **RUN-AND-LOG.bat** instead of `melee.exe`. It
writes a log you can send along with your report.

To play online, both players need **the same build**. Send your friend the
same zip.

## Character select: building a squad

With Squad Strike on, in a 4 stock VS match:

| Button | What it does |
|---|---|
| **A** (drop coin) | Picks your fighter as usual. Where you drop your coin is where that fighter goes in your squad order. |
| **Z** on a portrait | Adds that fighter to your squad (up to four in total, coin pick included). The same fighter can go in more than once, e.g. Fox, Fox, Falco, Fox: Z on a fighter already in your squad adds another one. |
| **Z** on the random area | Adds a random fighter not already in your squad, without dropping your coin. Press it four times for a fully random squad. |
| **R** | Removes the last fighter you added. |
| Hold **L** (1 second) | Clears your whole squad. |
| **X / Y** on a squad member's portrait | Changes that member's colour (the last one you added, if that fighter is in twice). With the squad full, **Z** on a member changes its colour too. |

- Your squad is listed on screen in fighting order. A coin still in your
  hand isn't a pick yet; it joins the order where you drop it.
- Holding a CPU's coin, **Z** builds that CPU's squad.
- With three or four players (doubles or a free-for-all) a squad is two
  fighters: your coin pick and one more. In a team battle squad members wear
  their team's colour, and a stock taken from a teammate brings you back as
  the fighter you had last.
- An error sound means the squad is full or every colour of that fighter is
  taken.

## Playing doubles

Doubles is a 2v2 team battle. Squad Strike works in it too: with three or
four players each squad is **two fighters** (your coin pick and one more),
because a match can only load eight fighters at once.

### On one computer

1. Plug in four controllers and go to VS Mode → Melee.
2. Switch the battle type at the top of character select from **Melee** to
   **Team**, then press the team button on each player's panel to pick red,
   blue or green.
3. Pick your fighters (and with Squad Strike on, press **Z** on one more
   portrait each), then pick a stage.

### Two computers, two players each

1. On both computers: launcher → Settings → Online → **Players on this
   computer** = 2, and plug in a second controller.
2. One player calls the other from **Online → Direct Connect** (or both go to
   **LAN Play** on the same network), as for a 1v1.
3. One computer's players are **P1 and P3**, the other's **P2 and P4** (a
   coin flip on connecting decides which computer gets P1). Set
   teams on character select as above. Each computer picks its own player
   count, so 2v1 works too.

### Four computers, one player each (experimental)

1. Everyone needs **this same release**.
2. One player goes to **Online → Direct Connect → Host a party** and gives
   their connect code to the other three.
3. The other three go to **Online → Direct Connect** and call that code,
   exactly as they would for a 1v1. Everyone shows up on the party screen
   with their ping.
4. When three or four players are in, the host presses **START**.
5. The game connects every pair of computers directly. If two of you can't
   reach each other, one of the others passes your inputs along, and the
   party screen says so (for example "P2 and P4 are connected through P1").
   Nobody has to set anything up.
6. With four players, character select starts as a team battle, P1 + P3
   against P2 + P4. A pair that had to be connected through someone else
   starts on the same team, since they see each other a frame or so late.
   You can still change teams.

Input delay is picked automatically from the slowest connection. Known
limits for now: Direct Connect only (not LAN Play), quick chat is off in a
party, and if one player drops, the match ends for all four. This mode has
been tested with four copies of the game on one machine, but not yet by four
people on four home networks, so please report how it goes (run
**RUN-AND-LOG.bat** and send the log).

## Stage select

- Pick any stage as usual.
- **FROZEN** (under the grid): Pokémon Stadium with no transformations.
- **Metal Cavern, WarioWare** (next to FROZEN): the ACE stages.

<img width="1870" height="1437" alt="sq" src="https://github.com/user-attachments/assets/1975d134-72ae-45ed-9909-ac2690e354ca" />

## Controls

Any Gamecube adapter or GC Ultimate should work.

Keyboard: arrows or WASD = control stick, IJKL = C-stick, X = A, Z = B,
C = X, V = Y, Q / E = L / R, Tab = Z, Enter = Start, TFGH = D-pad.

**F1** or **Esc** (or Back/Select on a gamepad) opens the settings overlay in
game, with the game paused underneath. Its **Launcher** button leaves the game
and goes back to the launcher without closing Melee PC.

## Settings

The launcher's **Settings** button and the in-game F1 overlay share the same
settings:

- **Gameplay:** Unlock everything, Squad Strike, Stage Expansion, SD Remix
  characters, Free camera, Universal Controller Fix (UCF 0.8), Rumble, Post
  match results screen (online, the host's choice is used), Save replays.
- **Video:** Magnifying glass: **Widescreen (top only)** shows the bubble only
  for a fighter above the screen, since a widescreen view keeps fighters past
  the sides in sight.
- **Online:** your connect code, a friend's code, input delay, ping display,
  players on this computer.
- **Graphics / Audio / Controls:** resolution up to 10x, MSAA, filtering,
  post-processing, volume, button remapping.

Settings, memory cards and logs are stored in `%APPDATA%\melee-pc` on Windows
(`~/.local/share/melee-pc` on Linux, `~/Library/Application Support/melee-pc`
on macOS).

## Building from source

Every push is built for every platform by GitHub Actions
(`.github/workflows/build.yml`). To build it yourself, follow melee-pc's
[docs/building.md](docs/building.md). The unit tests are `ninja unit_tests`
then `ctest -L melee`.

## **Donations** 
Fuck no. This shit was made with Claude over the course of a week. Sure I had to sit here and babysit for hours and playtest for hours but this is made on the works of other people as all AI projects are. Donate to the people that deserve it or donate to a charity or something. 

## Credits

- **[melee-pc](https://github.com/999sian/melee-pc)** by **999sian**: the PC
  port this mod is built on, including rollback netplay, the launcher and the
  renderer. melee-pc itself builds on
  [doldecomp/melee](https://github.com/doldecomp/melee),
  [aurora](https://github.com/encounter/aurora) and SDL3.
- **[ACE build](https://github.com/Chri222k/ACE-BUILD-PUBLIC-)** by
  **Chri222k** and the ACE team: Metal Cavern and WarioWare, with their
  music and stage select icons.
- **[SD Remix](https://sdremix.games)**
  ([repository](https://github.com/underscoreglook/SD-Remix)) by the SD Remix
  team: the SD Remix character files and balance changes.
- **Stage Expansion** by **UnclePunch**: the hazard-free stage layouts,
  ported here from his Gecko code.
- **Squad Strike** is sort of ported from my buddy Tyler who tried to make squad strike work in Gecko
  codes for Dolphin but slippi netcode doesn't allow for the swapping of characters mid match 
  and it is pushing the dolphin memory limit as it's an emulator of a gamecube.


## License

This mod keeps melee-pc's license terms (see [licenses/](licenses/)). The
decompiled game code in `src/melee` and `src/sysdolphin` is not licensed and
belongs to its copyright holders. The port code is GPL-3.0-or-later. Bundled
third-party components and mod files keep their own terms. Super Smash Bros.
Melee is © Nintendo / HAL Laboratory. This project is not affiliated with
Nintendo.
