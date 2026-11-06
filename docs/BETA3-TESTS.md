# Beta 3 installer checks

The Mac installer now installs and requires Homebrew's GNU Make (`gmake`).
Apple's bundled Make 3.81 silently skips the shell assignments used by the
pinned SM64 sources to extract assets, build tools and create output folders.
That caused the `build/us_melee_bridge/level_rules.mk` error on a new install.
Both the single-player and co-op builders now reject that fallback. The
single-player builder also creates its selected output folder explicitly.

The helper's window title and log identify its version and new attempts.
**Open Setup Log** now opens the GUI log first, so errors before the backend
starts cannot be hidden behind an older `setup.log`.

Both desktop helpers now show a cause, repair steps, error code and version.
The backend saves diagnostics for failures before its build log opens, including
insufficient disk space and unsupported game images. Build failures include the
step number and the current compiler error. Only the failing child's recent
output is classified, so an older SDK failure cannot mask a newer network error.
On Mac, **Copy Error Details** copies the explanation for a support report.
On Windows, **Open Setup Log** prefers the current preparation transcript.

## Validation

- 40 asset-free installer tests pass on Mac. The suite covers missing GNU Make,
  rejection of the Apple Make fallback, fresh generated includes, incremental
  output preservation and independent Windows output folders.
- Sixteen diagnostic cases cover disk space, permissions, unsupported inputs,
  missing tools, source conflicts, certificates, network failures and unknown
  errors. The suite also checks pre-backend diagnostic saving, read-only check
  mode, bounded current-child output and step-specific compiler errors.
- The compiled Swift helper passes eight error-classification cases. PowerShell
  script parsing and pure diagnostic/log-selection tests pass in PowerShell on
  Mac. These checks do not install Windows tools or run Windows gameplay.
- In a separate copy of the prepared SM64 host with no build directory,
  `/usr/bin/make` reproduced the reported missing `level_rules.mk` error.
  The fixed builder selected `gmake` and compiled the entire host successfully
  from the absent output directory.
- Existing beta 2 SDK selection, compiler diagnostics and adapter-resume tests
  remain in the installer suite. Gameplay and the native bridge ABI are unchanged.

The host rebuild used locally prepared source and assets. It is not a complete
fresh-machine installation or a retest on the reported laptop. Windows remains
an experimental beta; native Windows gameplay has not been verified.

See [beta 2 checks](BETA2-TESTS.md) for the previous gameplay and SDK validation.

## Resume an affected installation

Extract the new kit and open its installer. Select the original build folder
and game files, choose **Prepare Build Tools**, then **Build / Resume**.
Verified inputs, downloads and existing build outputs are kept.

For an existing beta 2 installation, `brew install make` followed by
**Build / Resume** also repairs this specific failure.
