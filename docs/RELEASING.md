# Publishing installer downloads

The README's Mac and Windows buttons download the actual installer ZIPs. They
are updated automatically after a complete release is published, including beta
releases. Linux does not currently have a supported installer.

## Prepare and publish a release

1. Build and audit the platform kits using the documented source packaging flow.
   Do not include game images, extracted assets, the generated game, saves or
   recordings. Keep beta and experimental platform labels accurate.
2. Create a release tagged `vVERSION`, such as `v0.6.0-beta.1`. Upload both kits
   **before publishing**. Expected names are
   `SuperMelee64-VERSION-mac-source-kit.zip` and
   `SuperMelee64-VERSION-windows-experimental-source-kit.zip`. The Windows
   `-experimental` component can be removed only when Windows is actually
   supported and its package name changes accordingly.
3. Put the logo, direct download buttons and short setup instructions first in
   the release notes. Use that release's exact asset URLs; an older release's
   notes should continue downloading its own files. Place developer guides and
   detailed test results after the installation section.
4. Publish from the Memelees account. **Update installer download buttons** runs
   against `main`, selects the newest published non-draft release with both
   uploaded installers, and commits the README update as Memelees using the
   GitHub noreply address. Its source audit checks the complete Git history.
5. Verify the workflow succeeded and both README buttons download the expected
   ZIP. If assets were uploaded late, a release was removed, or an asset was
   replaced, run the workflow manually from Actions to refresh the links.

The workflow deliberately runs only in `memelees/super-melee-64` and only when
started or rerun by Memelees. It uses the job's temporary `GITHUB_TOKEN` with
repository contents write permission. Checkout does not persist credentials;
the push receives an authorization header only through its subprocess
environment. No personal access token, local GitHub CLI login or force push is
used. Branch rules can reject the push; resolve that explicitly rather than
bypassing them.

## How the newest download is selected

GitHub's [`releases/latest` download URL](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)
selects a full release, and [excludes drafts and prereleases](https://docs.github.com/en/rest/releases/releases).
Using it during a beta-only release would produce broken buttons. The updater
instead reads the public release list, includes published prereleases, and
chooses the most recently **published** release containing both platform kits.
It uses the exact `browser_download_url` values after checking the GitHub host,
repository, tag and platform file names. An incomplete newer release leaves the
last complete release selected. Publication time, then release ID, determines
order; semantic version size does not.

The [`published` release event](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
also fires when a prerelease is published from a draft. Asset uploads by
themselves do not rerun the workflow, which is why manual dispatch is available.
The updater follows at most ten API pages of 100 releases and rejects oversized
metadata, unexpected URLs and ambiguous installer assets. Exhausting that
bound fails without changing the buttons.

## Preview or repair the README block

The README must contain exactly one ordered marker pair:

```html
<!-- DOWNLOAD_BUTTONS_START -->
<!-- DOWNLOAD_BUTTONS_END -->
```

Preview without changing files:

```sh
python3 outputs/sm64-melee-lab/native/update_download_links.py --dry-run
```

Apply the update:

```sh
python3 outputs/sm64-melee-lab/native/update_download_links.py --write
```

For an offline metadata fixture, add `--releases-json /path/to/releases.json`.
`--readme /path/to/README.md` selects an isolated checkout or fixture. The tool
changes only the marked block and, when present, the README digest in
`PUBLIC-SOURCE-MANIFEST.json`. It verifies every existing manifest digest before
writing; a stale source export must be audited and regenerated first. It never
rewrites game code, creates releases, uploads packages or edits release notes.

The image buttons use Shields; plain Mac ZIP and Windows ZIP links remain
visible if badge images cannot load.
