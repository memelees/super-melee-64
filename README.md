# Super Melee 64

<img src="outputs/sm64-melee-lab/branding/Super%20Melee%2064%20Rendered.png" alt="Super Melee 64" width="400">

**Play Super Mario 64 as your favourite Smash Bros. Melee fighter.**
Explore the adventure with Melee moves, character sounds and knockback, plus
Akaneia characters and optional enemy fighters.

<!-- DOWNLOAD_BUTTONS_START -->
<p align="center">
  <a href="https://github.com/memelees/super-melee-64/releases/download/v0.6.0-beta.3/SuperMelee64-0.6.0-beta.3-mac-source-kit.zip"><img src="https://img.shields.io/badge/Download_for_Mac-2563EB?style=for-the-badge" alt="Download for Mac" height="44"></a>
  <a href="https://github.com/memelees/super-melee-64/releases/download/v0.6.0-beta.3/SuperMelee64-0.6.0-beta.3-windows-experimental-source-kit.zip"><img src="https://img.shields.io/badge/Download_for_Windows-198754?style=for-the-badge" alt="Download for Windows" height="44"></a>
</p>
<p align="center">Beta installers · Mac: Apple Silicon, macOS 15+ · Windows x64: experimental beta · Linux: not available</p>
<p align="center"><a href="https://github.com/memelees/super-melee-64/releases/download/v0.6.0-beta.3/SuperMelee64-0.6.0-beta.3-mac-source-kit.zip">Mac ZIP</a> · <a href="https://github.com/memelees/super-melee-64/releases/download/v0.6.0-beta.3/SuperMelee64-0.6.0-beta.3-windows-experimental-source-kit.zip">Windows ZIP</a> · <a href="https://github.com/memelees/super-melee-64/releases/tag/v0.6.0-beta.3">Release notes — 0.6.0-beta.3 (beta)</a></p>
<!-- DOWNLOAD_BUTTONS_END -->

<a id="install"></a>

## Install and play

You'll need your own **US Super Mario 64 ROM** and **Melee USA Rev 2 disc image**.

1. **Download for your computer** above, then extract the ZIP.
2. **Open the installer:** `Install Super Melee 64.app` on Mac, or
   `Setup Windows.cmd` on Windows.
3. **Choose or drop in both game files**, follow the setup prompts and click
   **Build / Resume**. Then open **Super Melee 64** to play.

First setup prepares the game on your computer and guides any missing build
tools. Allow **12 GiB free**; **20 GiB** is recommended. Your game files stay
local. [Setup help and exact requirements](outputs/sm64-melee-lab/release/START%20HERE.txt)
· [Release notes and checksums](https://github.com/memelees/super-melee-64/releases).

<details>
<summary>Mac blocked the installer, or setup needs help?</summary>

The Mac beta is ad-hoc signed and has not been notarized by Apple. If macOS
blocks the downloaded helper, follow [Apple's instructions for opening a trusted
downloaded app](https://support.apple.com/en-us/102445). The installer does not
disable system security settings.

On Windows, the helper guides installation of the Visual C++ runtime and
WSL/Ubuntu build tools. Follow any restart and Ubuntu first-run prompts, then
return to the helper. The finished native game does not need Python or WSL
during gameplay. Windows gameplay remains experimental.

Setup supports cancellation and resume, retains verified downloads and records
a setup log. See [START HERE](outputs/sm64-melee-lab/release/START%20HERE.txt) for
platform-specific steps and troubleshooting.

</details>

## Play your way

**New Playthrough** starts from zero stars. **Continue** resumes your adventure.
**Unlocked** opens the sandbox, and **Recommended** offers character/level
pairings with their own earned-star saves. **No Melee Enemies** keeps the
original Mario enemies and bosses.

<details>
<summary>Controls and character switching</summary>

<a id="controls"></a>

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

F7 opens the enemy picker. F10 toggles universal wall jumps (default off).
Fighters use circular ground shadows without height lines. In water, Q/LB rises and E/LT dives;
forward movement holds depth. Jumps and specials remain usable. Coins restore
SM64 air and heal ten damage percentage points per coin value.

F8/L3 replaces the current Melee opponents with the next authored stage round.
The sequence loops, one round per press. Manually summoned rounds wait for a
new press; ordinary story encounters retain their normal wave progression.

</details>

<details>
<summary>Play with friends — experimental co-op</summary>

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

</details>

<details>
<summary>Beta status and tested coverage</summary>

The current beta passed 27 stage-start cases, 33 fighter-form cases and the
integration/network acceptance cases. Co-op has been exercised with two local
clients. A full 120-star playthrough, sixteen-person session and real Windows
gameplay remain unverified. See the [test report](outputs/sm64-melee-lab/verification/beta-acceptance.json)
for the exact scope and fixture corrections.

</details>

## Make it your own

Fork, modify and extend the original integration under [GPL-3.0-or-later](LICENSING.md).
Distributed covered versions must preserve the GPL's source and licence freedoms;
private edits can stay private. Game assets and third-party code retain their
own terms.

[How it works](docs/INTEGRATION.md) · [Adapt it to another game](docs/PORTING.md)
· [Contribute](CONTRIBUTING.md)

<details>
<summary>Build from source and package a release</summary>

Forks, modifications and experiments are welcome. The original integration is
**GPL-3.0-or-later**: you can change and reuse it without asking us, including
in another game integration. Distributed covered versions must preserve the
GPL's source and licence freedoms; private changes do not have to be published.
See [your rights and the third-party scope](LICENSING.md).

Developer guides:

- [How the integration works](docs/INTEGRATION.md): the engine boundary,
  simulation, collision, input, rendering, audio and multiplayer ownership.
- [Adapting it to another game](docs/PORTING.md): a staged implementation path,
  the host responsibilities and validation needed for a new integration.
- [Contributing](CONTRIBUTING.md): local setup, checks and change submission.

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

</details>

<details>
<summary>Licensing and credits</summary>

Read [LICENSE](LICENSE), [LICENSING](LICENSING.md) and
[third-party notices](outputs/sm64-melee-lab/release/THIRD-PARTY.txt). The licence
for original integration work does not grant rights to upstream decompiled game
code, characters, audio or other game assets. Those are obtained and built
locally from your supplied images and the documented upstream sources.

This unofficial fan project is not affiliated with Nintendo, HAL Laboratory,
SEGA or the owners of the characters. SM64ex, Coop Deluxe, melee-pc, Akaneia,
the Melee decompilation projects and the 3D Melee reference mod provide the
underlying work; their notices and source pins are preserved.

</details>
