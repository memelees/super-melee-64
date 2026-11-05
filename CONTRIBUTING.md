# Contributing

Super Melee 64 combines the SM64 adventure with native Melee fighters. Keep the
SM64 and Smash Royale integrations independently buildable, and avoid changing
another local checkout to make this project run.

Forks and downstream integrations are welcome; contributing upstream is
optional. Start with [the integration tour](docs/INTEGRATION.md), then use
[the porting guide](docs/PORTING.md) if you want another game to host the
fighters. The original project code is GPL-3.0-or-later. You can make private
changes without publishing them, and distribute covered changes under the
licence's source and notice requirements. See [LICENSING.md](LICENSING.md).

Before submitting changes, read [LICENSING.md](LICENSING.md). Submit only work
you have the right to contribute under the applicable project license. Preserve
existing notices and identify the source, revision and license of third-party
changes. Do not add game images, extracted assets, generated game binaries or
credentials to a commit, pull request, issue or release.

## Reporting a bug

Use the bug report template and provide the source commit/native version,
operating system, character/costume, stage/star and the shortest reproduction.
Mention single-player or co-op, the host/client role, difficulty, recording,
custom input bindings and whether the problem occurs with a fresh save.
Include a small relevant log excerpt after removing personal paths, IP addresses
and identifiers. Never attach your ROM/disc, extracted game files or entire
`work/` directory. See [SECURITY.md](SECURITY.md) for security reports.

## Checks without game files

Use Python 3.12+ and a C compiler named `clang` (install Clang separately if your
system does not provide it). From the repository root:

```sh
python3 outputs/sm64-melee-lab/native/verify_repository.py --self-test
python3 outputs/sm64-melee-lab/native/verify_repository.py
python3 outputs/sm64-melee-lab/native/verify_bootstrap.py
python3 outputs/sm64-melee-lab/native/verify_rounds.py
python3 outputs/sm64-melee-lab/native/verify_coop_campaign.py
python3 outputs/sm64-melee-lab/native/verify_release.py --campaign-only
```

The stock air-reference and network-parser tests additionally need checkouts of
the pinned public SM64 sources, but no ROM or extracted assets:

```sh
python3 outputs/sm64-melee-lab/native/verify_air.py --stock-source /path/to/sm64ex
python3 outputs/sm64-melee-lab/native/verify_network_safety.py --source /path/to/sm64coopdx
```

GitHub Actions runs these checks without game images, private repositories or
gameplay artifacts. Its Windows job also runs the Python/PowerShell installer
tests and compiles the actual native launcher for its `--self-test`; that check
does not run the game. Repository auditing checks current tracked files, proposed
non-ignored files and all reachable Git history; it deliberately ignores local
game/build directories that are excluded from Git.

Release maintainers can create a new source-only staging directory with
`native/export_public_source.py` (under the source layout shown above). It
requires a new destination and audits the final manifest before creating it.
An optional ignored local policy can supply private aliases and replacements;
that policy and existing Git history are never copied. Historical receipts are
omitted unless deliberately selected. Re-run the audit after any later change.

## Gameplay changes

Build locally from your own verified game files using START HERE. Run the smoke
fixture related to the behavior you changed; record the commit, native protocol,
inputs, pass/fail checks and limits of the test. Co-op changes should exercise
both the local owner and a second peer. The `verify_coop_features.py all` suite
includes combat, projectiles, grabs, campaign handoff, partners and manual rounds.
Do not describe a cross-build as a Windows playtest or a bounded smoke test as
a complete adventure playthrough.

Keep pull requests focused. Explain the concrete behavior before and after the
change, relevant validation and remaining limitations. Update the source kit's
explicit packaging lists when adding a required build tool or regression check.
