# Beta 4 installer checks

The Mac installer omitted `mingw-w64` from **Prepare Build Tools** and its
prerequisite checks. The pinned single-player and co-op sources use that
package's GNU assembler and object tools to build SM64 sound data. A new Mac
could therefore reach step 8 and stop at
`gmake: i686-w64-mingw32-as: No such file or directory`.

The helper now installs the package and checks the assembler, `objcopy` and
`objdump` before downloads or compilation. Both native builders check the tools
as well and pass their resolved paths to Make. This is an audio build dependency;
the finished Mac game remains a native Apple Silicon executable.

The error dialog now identifies this missing package and gives a specific repair.
For other build failures, compiler and missing-command errors take precedence
over nested Python `CalledProcessError` tracebacks. An assembler input-file error
is kept distinct from a missing assembler executable.

## Validation

- 48 asset-free Python installer tests pass, including each missing sound tool,
  early failure of both builders, platform isolation, parallel build output,
  nested tracebacks and existing SDK/resume/input checks.
- The compiled Swift helper passes 12 diagnostic cases, including the assembler,
  object tools and prerequisite failures. PowerShell parsing and diagnostic
  checks also pass on Mac.
- In a separate source copy with no build directory or compiled build tools,
  a restricted executable path reproduced the beta 3 missing-assembler failure.
  The new prerequisite check detected all three absent tools. Adding those real
  Homebrew tools allowed the build to resume and compile the full native host.
  An existing output marker survived. The resulting sound sequence matched the
  known working build byte for byte, and the game executable was Mach-O arm64.
- The co-op host's actual sound-player sequence was also assembled and extracted
  into a new output directory with the selected GNU tools.

These build checks used locally prepared source and game assets. They are not a
complete fresh-machine installation or a retest on the reported laptop. No game
assets or raw user logs are distributed. Gameplay code is unchanged, and Windows
remains experimental.

## Resume an affected installation

Extract the new Mac kit and open its new installer. Select the original build
folder and game files, choose **Prepare Build Tools**, then **Build / Resume**.
Verified inputs, downloads and existing build outputs are kept.

For this specific beta 3 error, running `brew install mingw-w64` in Terminal and
then choosing **Build / Resume** in the existing installer also works. Do not
delete the build folder or reinstall the game files.

See [beta 3 checks](BETA3-TESTS.md) for GNU Make and diagnostic improvements, and
[beta 2 checks](BETA2-TESTS.md) for the earlier SDK and gameplay fixes.
