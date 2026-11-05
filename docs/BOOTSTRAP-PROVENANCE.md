# Source adapter provenance and review boundary

The bundled source adapter is versioned as `super-melee64-adapter-v1`. The ZIP
has a member-by-member SHA-256 manifest. Setup verifies that manifest before
extracting any source. It contains native adapter source/patches, Python build
tools, symbol metadata, original generated title lettering and license notices.
It does not contain a complete engine source tree or game images/binaries.

The publicly fetched native engine, bridge-generator input, SM64 hosts and
official mod patches are separately pinned. Their revisions, download URLs and
permissions remain their upstream projects' own; see THIRD-PARTY.txt. Game art,
models, audio and title-scene assets are extracted only during a user's local
build from verified inputs. The generated `m3d_title_mesh.inc` identifies its
source as the adapter's original title-lettering tool; it is not an extracted
Nintendo title mesh.

The original generator is also supplied separately as
`outputs/sm64-melee-lab/native/title_lettering.py`. It retains the bundled
lettering's outline, triangulation and color calculations. Its default output is
`work/generated-title/m3d_title_mesh.inc`; `--output` selects another path.
Regenerating this optional mesh requires FontTools, Shapely, NumPy,
mapbox-earcut and locally supplied font files. The macOS font defaults are not
distributed, and the generator accepts replacement fonts. Review the selected
font's terms before distributing regenerated lettering. Normal setup uses the
verified bundled mesh and needs none of these optional tools or fonts.

## What the checks establish

`verify_bootstrap.py` verifies the builder's supported bundle identity, unique
safe member names, exact member hashes, source-only allowlists, UTF-8 payloads
and absence of binary assets. It reports patch context and upstream game-code
targets separately rather than treating text source as a redistribution grant.
`verify_repository.py` also checks nested members for credential formats and
configured private aliases without exposing the matched values.

The adapter currently needs its generated metadata and compiled translation
units even when their optional world features are unused by the SM64 host.
Removing files, changing patch application context or trimming symbol tables
changes the reproducible builder input. Any such change needs a new bundle
identity, updated member hashes and a fresh isolated build of both hosts.
This release preserves the validated source composition and unrelated upstream
notices instead of silently removing dependencies to reduce ZIP size.

## What still requires a distribution judgment

The adapter's patches include changes and surrounding context from upstream
decompiled game-code paths. Upstream's licensing explanation expressly grants
no redistribution permission for that game code. The GPL grant for our original
integration and adapter changes does not relicense the context or grant rights
over the games. A hash check, a minimal diff or a user-supplied disc does not
resolve that permissions question.

Preserve the upstream notices and review the final source/patch composition
before distribution. The automated audit is a content/provenance check, not
legal clearance. No compiled game engine or locally generated game assets
should be included in a public release download.
