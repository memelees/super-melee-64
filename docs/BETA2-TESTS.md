# Beta 2 checks

This update fixes Mac SDK selection, Kirby copy-data loading for stage opponents,
and removes height lines and coloured ground rings while keeping circle shadows.

- **30 asset-free installer tests pass**, including stale SDK replacement,
  missing SDK/GCC, compiler timeout, licence errors, stopping before downloads,
  saved GUI diagnostics and safe resume of the verified adapter overrides.
- Real Mac GCC and Apple Clang header checks pass. A small CMake project with a
  stale SDK cache was reconfigured with the explicit SDK and compiled into C/C++
  objects. No game image was used for that check.
- The compiled Swift helper correctly identifies three fixture errors: Xcode
  licence, outdated Command Line Tools and missing system headers.
- Both extracted platform kits pass manifest, source/bootstrap and prerequisite
  validation. Archive privacy checks cover nested source, image metadata and the
  Mac installer executable. No game assets or game executable are distributed.

## Kirby and rendering

The live regression enters the native swallow-completion function with a copy
archive absent from the initial roster, then uses real special input and native
rendering. Explicit targets and costumes were checked against native log records.

| Copy target | Kirby costume | Result |
|---|---:|---|
| Mario | 0 | Passed |
| Fox | 0 | Passed |
| Donkey Kong | 2 | Passed |
| Link | 0 | Passed |
| Peach | 5 | Passed |
| Samus | 0 | Passed |
| Mr. Game & Watch | 0 | Passed |
| Sonic (existing no-copy fallback) | 0 | Passed |

The original cold Peach/costume-0 crash was also retested successfully. The
installed Mac app passed a separate 300-frame bundled-files-only test as Kirby
copying Peach with costume 5, native rendering, one game process and H.264/AAC
recording. Its explicit verification path used an isolated profile and did not
modify the player's live save.

A live two-fighter ground/HUD check passed with line rendering disabled. Old
saved line options are ignored. A real two-client co-op combat/shield regression
also passed. Windows native code cross-compiles and its PE imports/exports pass
the existing audit; Windows remains an experimental beta.

## Scope and fixture corrections

This is not a complete inhale/throw/multiplayer-copy matrix, a fresh-machine
Mac setup, an installation retest on the reported laptop, a full 120-star
playthrough, a sixteen-person session or a real Windows gameplay test.

The initial copy sweep discarded environment selectors and repeated Peach. It
is excluded; the eight counted cases were rerun with explicit CLI selections,
costumes and matching native confirmations. The ground/HUD check initially
required exactly 1.25x camera distance. It now accepts the existing bounded
1.25x–1.70x combat framing, and the actual two-fighter case was rerun.

[Previous beta acceptance](../outputs/sm64-melee-lab/verification/beta-acceptance.json)
records the broader stage, roster, integration and network baseline.
