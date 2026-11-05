# Super Melee 64

<img src="outputs/sm64-melee-lab/branding/Super%20Melee%2064%20Rendered.png" alt="Super Melee 64" width="320">

Play Super Mario 64 as a Smash Bros. Melee fighter, with Melee movement,
attacks, damage, character rendering and sounds inside the original adventure.
Both engines run in one process. The roster includes the Akaneia characters.

This is a beta. macOS on Apple Silicon is the primary target; Windows x64 is
experimental. Multiplayer targets 16 players, with bounded two-client tests.
A full 120-star playthrough and a sixteen-person session are not certified.

## Install

Get the source installer from [Releases](https://github.com/memelees/super-melee-64/releases).
The installer includes the setup helper and integration source. First setup
downloads build dependencies and creates your local game from your own files.

1. Open **Install Super Melee 64** on Mac, or **Setup Windows.cmd** on Windows.
2. Select or drop your original **US SM64 .z64 ROM** and **Melee USA Rev 2 disc image**.
3. Follow the prerequisite checks, then build. Akaneia patches and public source
   dependencies are obtained automatically. Your game images stay on your computer.

The helper records a setup log, supports cancellation and retries, and checks
available space. macOS requires Apple Silicon and macOS 15 or newer. Windows
uses WSL/Ubuntu to build; the resulting native launcher does not require Python
at gameplay startup. See [installation details](outputs/sm64-melee-lab/release/START%20HERE.txt)
for the exact inputs and platform requirements.

The Mac installer is an unsigned developer beta unless a release explicitly
states otherwise. Follow [Apple's instructions for apps from identified and
unidentified developers](https://support.apple.com/en-us/102445) if macOS blocks
it. The installer does not disable system security settings.

After setup, open **Super Melee 64**. New Playthrough starts a zero-star save;
Unlocked is a separate sandbox. Recommended stage starts keep their own earned
stars and return to the course's star selector after a star.

## Controls

| Action | Keyboard | Standard controller |
|---|---|---|
| Move | WASD | Left stick |
| Jump | L | X / Y |
| Attack / special | comma / K | A / B |
| Up / down attack modifier | Q / E | LB / LT |
| Grab / shield | left / right Shift | RB / RT |
| Camera | Arrow keys | D-pad |
| Change fighter and costume | F6 | Back |
| Summon next stage round | F8 | L3 |
| Record gameplay | F9 | Mac Recording menu |

F7 opens the enemy picker. F10 toggles universal wall jumps; F11 toggles enemy
height lines. Both toggles default off. In water, Q/LB rises and E/LT dives;
forward movement holds depth. Jumps and specials remain usable. Coins restore
SM64 air and heal ten damage percentage points per coin value.

F8/L3 replaces the current Melee opponents with the next authored stage round.
The sequence loops, one round per press. Manually summoned rounds wait for a
new press; ordinary story encounters retain their normal wave progression.

## Multiplayer beta

Choose **Gameplay > Host Co-op** or **Join Co-op** and use the host's IP address.
Every player needs the same Super Melee 64 beta build. Stock Coop Deluxe and
older Super Melee 64 clients are incompatible. Each player chooses their own
fighter and costume; players can fight each other and share stage encounters.

This fixed-content build rejects additional host-provided mods. Public lobby
browsing and easy Internet matchmaking are not yet exposed by this launcher.
Direct Internet play requires the usual router/firewall configuration; LAN is
the simplest first test. Two-client loopback tests do not establish WAN/NAT
behaviour, public-peer security, or sixteen-player performance. See
[security scope](SECURITY.md) and [reported test coverage](outputs/sm64-melee-lab/verification/beta-acceptance.json).

## Build and contribute

```sh
git clone https://github.com/memelees/super-melee-64.git
cd super-melee-64
python3 outputs/sm64-melee-lab/release/Setup.py --platform mac \
  --rom /path/to/game.z64 --melee /path/to/game.iso
```

The current integration, native adapter and build scripts are included. No
private repository access is needed. Public dependencies and patches are pinned
and verified. [CONTRIBUTING](CONTRIBUTING.md) describes asset-free checks and how
to report a reproducible bug without sharing game images or private saves.

Code lives in `outputs/sm64-melee-lab/native`; installer source is under
`outputs/sm64-melee-lab/release`. Generate source packages with
`python3 outputs/sm64-melee-lab/native/package_release.py` after building the
branding assets. Never upload the generated game app, build folder or recordings:
they contain local game data.

## Licensing and credits

Read [LICENSE](LICENSE), [LICENSING](LICENSING.md) and
[third-party notices](outputs/sm64-melee-lab/release/THIRD-PARTY.txt). The licence
for original integration work does not grant rights to upstream decompiled game
code, characters, audio or other game assets. Those are obtained and built
locally from your supplied images and the documented upstream sources.

This unofficial fan project is not affiliated with Nintendo, HAL Laboratory,
SEGA or the owners of the characters. SM64ex, Coop Deluxe, melee-pc, Akaneia,
the Melee decompilation projects and the 3D Melee reference mod provide the
underlying work; their notices and source pins are preserved.
